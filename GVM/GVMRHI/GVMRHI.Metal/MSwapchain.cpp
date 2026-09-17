#include "MSwapchain.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MQueue.hpp"
#include "MTexture.hpp"
#include <EASTL/make_intrusive.h>
#include <GVMRHI/GVMCpuProbe.hpp>
#include <GVMRHI/GVMLogging.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdexcept>

namespace GVM::RHI::Metal
{
    namespace
    {
        constexpr eastl::string_view SwapchainProbeCategory = "gvmrhi.metal.swapchain";
        constexpr uint64_t kHostGpuWaitTimeoutMilliseconds = 2000u;
        constexpr int64_t kHostGpuWaitTimeoutNanoseconds = 2'000'000'000ll;

        TextureFormat resolvePreferredFormat(CA::MetalLayer *layer)
        {
            if (layer != nullptr)
            {
                const TextureFormat configuredFormat = translateMTLTextureFormatToGVM(layer->pixelFormat());
                if (configuredFormat == TextureFormat::BGRA8Unorm ||
                    configuredFormat == TextureFormat::BGRA8UnormSrgb ||
                    configuredFormat == TextureFormat::RGBA8Unorm ||
                    configuredFormat == TextureFormat::RGBA8UnormSrgb)
                {
                    return configuredFormat;
                }
            }

            return TextureFormat::BGRA8Unorm;
        }

        GVM::RHI::Extent3D resolveDrawableExtent(CA::MetalLayer *layer, const SwapchainDescriptor &descriptor)
        {
            GVM::RHI::Extent3D extent = {.width = 0, .height = 0, .depth = 1};

            if (descriptor.B != nullptr)
            {
                const auto *hint = static_cast<const GVM::RHI::Extent3D *>(descriptor.B);
                extent.width = hint->width;
                extent.height = hint->height;
            }

            if (extent.width == 0 || extent.height == 0)
            {
                const CGSize drawableSize = layer->drawableSize();
                extent.width = extent.width == 0 ? static_cast<uint32_t>(drawableSize.width) : extent.width;
                extent.height = extent.height == 0 ? static_cast<uint32_t>(drawableSize.height) : extent.height;
            }

            extent.width = std::max<uint32_t>(extent.width, 1);
            extent.height = std::max<uint32_t>(extent.height, 1);
            return extent;
        }
    } // namespace

    // 辅助函数：读取
    NS::UInteger getDrawableCount(CA::MetalLayer *layer)
    {
        // 1. 获取 Selector (相当于 registerSelector)
        SEL sel = sel_registerName("maximumDrawableCount");

        // 2. 准备 objc_msgSend。
        // 注意：在 arm64 (M1/M2/M3) 上，必须强制转换 objc_msgSend 的函数指针类型，
        // 否则参数传递会崩溃。这是 C++ 调用 ObjC 的铁律。
        using FunctionType = NS::UInteger (*)(void *, SEL);
        FunctionType func = (FunctionType)objc_msgSend;

        // 3. 发送消息
        // layer 本质上就是 void* (id)，直接传进去
        return func(layer, sel);
    }

    // 辅助函数：设置
    void setDrawableCount(CA::MetalLayer *layer, NS::UInteger count)
    {
        // 1. 获取 Selector (注意冒号，代表有一个参数)
        SEL sel = sel_registerName("setMaximumDrawableCount:");

        // 2. 准备 objc_msgSend
        using FunctionType = void (*)(void *, SEL, NS::UInteger);
        FunctionType func = (FunctionType)objc_msgSend;

        // 3. 发送消息
        func(layer, sel, count);
    }

    // 辅助函数：设置 VSync
    // enabled = true  -> 开启垂直同步 (限制帧率，nextDrawable 会阻塞等待屏幕刷新)
    // enabled = false -> 关闭垂直同步 (FPS 尽可能高，可能有画面撕裂)
    void setVSyncEnabled(CA::MetalLayer *layer, bool enabled)
    {
        // 1. 获取 Selector
        // 注意：属性名是 displaySyncEnabled，setter 是 setDisplaySyncEnabled:
        SEL sel = sel_registerName("setDisplaySyncEnabled:");

        // 2. 准备 objc_msgSend
        // 参数类型是 BOOL (signed char 或 bool)
        using FunctionType = void (*)(void *, SEL, bool);
        FunctionType func = (FunctionType)objc_msgSend;

        // 3. 发送消息
        func(layer, sel, enabled);

        printf("VSync has been set to: %s\n", enabled ? "ON" : "OFF");
    }

    // 辅助函数：获取当前 VSync 状态 (可选)
    bool getVSyncEnabled(CA::MetalLayer *layer)
    {
        SEL sel = sel_registerName("displaySyncEnabled");
        using FunctionType = bool (*)(void *, SEL);
        FunctionType func = (FunctionType)objc_msgSend;
        return func(layer, sel);
    }
    MSwapchain::MSwapchain()
    {
    }

    void MSwapchain::init(MDevice *device, const SwapchainDescriptor &descriptor)
    {
        GVMCpuProbeScopeDetail(
            device,
            SwapchainProbeCategory,
            "MSwapchain::init",
            "has_layer={} has_extent_hint={}",
            descriptor.A != nullptr,
            descriptor.B != nullptr);
        this->mNativeSwapchain = (CA::MetalLayer *)descriptor.A;
        this->mDevice = device;
        this->mPresentCompletionGroup = dispatch_group_create();
        this->mPreferredFormat = resolvePreferredFormat(this->mNativeSwapchain);
        this->mNativeSwapchain->setDevice(device->getNativeDevice());
        this->mNativeSwapchain->setPixelFormat(translateTextureFormatToMTL(this->mPreferredFormat));
        this->mNativeSwapchain->setFramebufferOnly(false);

        const auto drawableExtent = resolveDrawableExtent(this->mNativeSwapchain, descriptor);
        this->mNativeSwapchain->setDrawableSize(CGSize{
            static_cast<CGFloat>(drawableExtent.width),
            static_cast<CGFloat>(drawableExtent.height),
        });

        setDrawableCount(this->mNativeSwapchain, 3);
        setVSyncEnabled(this->mNativeSwapchain, false);
        mQuadShaderExecutor = eastl::make_intrusive<MQuadShaderExecutorImpl>();
        createNewTexture(drawableExtent.width, drawableExtent.height);
        mQuadShaderExecutor->create(device, mTempTexture->getFormat());
    }

    MultipleElements<TextureFormat> MSwapchain::getSupportedFormats() const
    {
        return MultipleElements<TextureFormat>{mPreferredFormat};
    }

    TextureFormat MSwapchain::getPreferredFormat() const
    {
        return mPreferredFormat;
    }

    SwapchainQueryResult MSwapchain::queryNextTexture()
    {
        GVMCpuProbeScopeDetail(
            mDevice,
            SwapchainProbeCategory,
            "MSwapchain::queryNextTexture",
            "temp_texture_valid={}",
            !mTempTexture.isNull());
        // this->mCurrentCADrawble = this->mNativeSwapchain->nextDrawable();
        // this->mCurrentDrawable = (MTL::Drawable *)this->mCurrentCADrawble;
        // mTrueSwapchainTexture = this->mCurrentCADrawble->texture();

        /* MTexture *drawableTexture = new MTexture();
         drawableTexture->initWithNativeTexture(this->mDevice, this->mCurrentCADrawble->texture(), "SwapchainTexture"); */
        // mTrueSwapchainTexture = queryTrueSwapchainImage();
        // if (mTrueSwapchainTexture->width() != mTempTexture->getWidth() || mTempTexture->getHeight() != mTrueSwapchainTexture->height())
        {
            //    createNewTexture(mTrueSwapchainTexture->width(), mTrueSwapchainTexture->height());
        }
        SwapchainQueryResult result;
        result.status = SwapchainNextTextureQueryStatus::Success;
        result.texture = mTempTexture;
        this->mCurrentResult = result;

        return result;
    }

    void MSwapchain::present()
    {
        GVMCpuProbeScopeDetail(
            mDevice,
            SwapchainProbeCategory,
            "MSwapchain::present",
            "temp_texture_valid={}",
            !mTempTexture.isNull());
        intptr_t frameWaitResult = 0;
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::present.frameSemaphoreWait",
                "temp_texture_valid={}",
                !mTempTexture.isNull());
            frameWaitResult = dispatch_semaphore_wait(
                this->mDevice->getFrameSemaphore(),
                dispatch_time(DISPATCH_TIME_NOW, kHostGpuWaitTimeoutNanoseconds));
        }
        if (frameWaitResult != 0)
        {
            GVMLogError(
                mDevice,
                SwapchainProbeCategory,
                "event=metal_swapchain_frame_semaphore_timeout timeout_ms={} temp_texture_valid={}",
                kHostGpuWaitTimeoutMilliseconds,
                !mTempTexture.isNull());
            flushLogger(mDevice != nullptr ? mDevice->getLogger() : Logger{});
            throw std::runtime_error("MSwapchain::present timed out while waiting for an available Metal frame slot.");
        }
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::present.nextDrawable",
                "temp_texture_valid={}",
                !mTempTexture.isNull());
            this->mCurrentCADrawble = this->mNativeSwapchain->nextDrawable();
        }
        if (this->mCurrentCADrawble == nullptr)
        {
            GVMCpuProbeInstant(mDevice, SwapchainProbeCategory, "mswapchain.present.next_drawable_nil");
            std::fprintf(stderr, "[gvm-metal-swapchain] nextDrawable returned nil.\n");
            dispatch_semaphore_signal(this->mDevice->getFrameSemaphore());
            return;
        }
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::present.resolveDrawableTexture",
                "temp_texture_valid={}",
                !mTempTexture.isNull());
            this->mCurrentDrawable = (MTL::Drawable *)this->mCurrentCADrawble;
            mTrueSwapchainTexture = this->mCurrentCADrawble->texture();
            if (mTrueSwapchainTexture == nullptr)
            {
                GVMCpuProbeInstant(mDevice, SwapchainProbeCategory, "mswapchain.present.drawable_texture_nil");
                std::fprintf(stderr, "[gvm-metal-swapchain] Drawable texture is nil.\n");
                dispatch_semaphore_signal(this->mDevice->getFrameSemaphore());
                return;
            }
            if (mTrueSwapchainTexture->width() != mTempTexture->getWidth() || mTempTexture->getHeight() != mTrueSwapchainTexture->height())
            {
                createNewTexture(mTrueSwapchainTexture->width(), mTrueSwapchainTexture->height());
            }
        }
        auto presentCMD = this->mDevice->getMainQueueImpl()->getNativeQueue()->commandBuffer();
        auto frameSemaphore = this->mDevice->getFrameSemaphore();
        auto presentCompletionGroup = this->mPresentCompletionGroup;

        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::present.copyEncode",
                "has_drawable={} temp_texture_valid={}",
                this->mCurrentDrawable != nullptr,
                !mTempTexture.isNull());
            auto *drawable = this->mCurrentDrawable;


            mQuadShaderExecutor->execute(presentCMD, static_cast<MTexture *>(mTempTexture.get())->getNativeTexture(), mTrueSwapchainTexture);
            // presentCMD->presentDrawableAtTime(this->mCurrentDrawable, 1. / 60.);
            presentCMD->presentDrawable(this->mCurrentDrawable);
        }
        dispatch_group_enter(presentCompletionGroup);
        presentCMD->addCompletedHandler([frameSemaphore, presentCompletionGroup](MTL::CommandBuffer *commandBuffer) {
            if (auto *error = commandBuffer->error())
            {
                std::fprintf(
                    stderr,
                    "[gvm-metal-swapchain] present command buffer failed: %s\n",
                    error->localizedDescription() != nullptr
                        ? error->localizedDescription()->cString(NS::StringEncoding::UTF8StringEncoding)
                        : "unknown error");
            }
            dispatch_semaphore_signal(frameSemaphore);
            dispatch_group_leave(presentCompletionGroup);
        });
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::present.commitPresentCommandBuffer",
                "has_drawable={} temp_texture_valid={}",
                this->mCurrentDrawable != nullptr,
                !mTempTexture.isNull());
            presentCMD->commit();
        }
        presentCMD = nullptr;
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::present.frameLifecycle",
                "temp_texture_valid={}",
                !mTempTexture.isNull());
            this->mDevice->endFrame();
            mDevice->releaseGCPool();
            mDevice->activateGCPool();
            mDevice->beginFrame();
        }
    }

    void MSwapchain::destroy()
    {
        GVMCpuProbeScopeDetail(
            mDevice,
            SwapchainProbeCategory,
            "MSwapchain::destroy",
            "has_temp_texture={} has_present_completion_group={}",
            !mTempTexture.isNull(),
            mPresentCompletionGroup != nullptr);
        if (mPresentCompletionGroup != nullptr)
        {
            const intptr_t presentWaitResult = dispatch_group_wait(
                mPresentCompletionGroup,
                dispatch_time(DISPATCH_TIME_NOW, kHostGpuWaitTimeoutNanoseconds));
            if (presentWaitResult != 0)
            {
                GVMLogError(
                    mDevice,
                    SwapchainProbeCategory,
                    "event=metal_swapchain_present_completion_timeout timeout_ms={} has_temp_texture={} has_current_drawable={}",
                    kHostGpuWaitTimeoutMilliseconds,
                    !mTempTexture.isNull(),
                    mCurrentDrawable != nullptr);
                flushLogger(mDevice != nullptr ? mDevice->getLogger() : Logger{});
                throw std::runtime_error("MSwapchain::destroy timed out while waiting for pending Metal present completion.");
            }
            dispatch_release(mPresentCompletionGroup);
            mPresentCompletionGroup = nullptr;
        }
        if (mTempTexture.isNull() == false)
        {
            mDevice->freeTexture(mTempTexture);
            mTempTexture.reset();
        }
        mQuadShaderExecutor = nullptr;
        mCurrentDrawable = nullptr;
        mCurrentCADrawble = nullptr;
        mTrueSwapchainTexture = nullptr;
        mNativeSwapchain = nullptr;
        mDevice = nullptr;
    }

    void MSwapchain::createNewTexture(uint32_t w, uint32_t h)
    {
        GVMCpuProbeScopeDetail(
            mDevice,
            SwapchainProbeCategory,
            "MSwapchain::createNewTexture",
            "requested_size={}x{}",
            w,
            h);
        w = std::max<uint32_t>(w, 1);
        h = std::max<uint32_t>(h, 1);
        if (mTempTexture.isNull() == false)
        {
            mDevice->freeTexture(mTempTexture);
            mTempTexture.reset();
        }
        TextureDescriptor tempTextureDescriptor{};
        tempTextureDescriptor.label = "TempSwapchainImage";
        tempTextureDescriptor.usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc;
        tempTextureDescriptor.size = {.width = w, .height = h, .depth = 1};
        tempTextureDescriptor.format = mPreferredFormat;
        mTempTexture = mDevice->createTexture(tempTextureDescriptor);
    }

    CA::MetalDrawable *MSwapchain::queryTrueSwapchainImage()
    {
        // this->mCurrentCADrawble = this->mNativeSwapchain->nextDrawable();
        return this->mNativeSwapchain->nextDrawable()->retain();
        // this->mCurrentDrawable = (MTL::Drawable *)this->mCurrentCADrawble;
        // return this->mCurrentCADrawble->texture();
    }

} // namespace GVM::RHI::Metal
