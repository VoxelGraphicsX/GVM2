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
        if (mDevice->getLostState().isLost())
        {
            SwapchainQueryResult result;
            result.status = SwapchainNextTextureQueryStatus::DeviceLost;
            return result;
        }
        if (mHasOutstandingAcquire)
        {
            return mCurrentResult;
        }
        GVMCpuProbeScopeDetail(
            mDevice,
            SwapchainProbeCategory,
            "MSwapchain::queryNextTexture",
            "has_outstanding_acquire={}",
            mHasOutstandingAcquire);
        releaseDrawableTexture();
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::queryNextTexture.frameSemaphoreWait",
                "has_outstanding_acquire={}",
                mHasOutstandingAcquire);
            const intptr_t frameWaitResult = dispatch_semaphore_wait(
                this->mDevice->getFrameSemaphore(),
                dispatch_time(DISPATCH_TIME_NOW, kHostGpuWaitTimeoutNanoseconds));
            if (frameWaitResult != 0)
            {
                GVMLogError(
                    mDevice,
                    SwapchainProbeCategory,
                    "event=metal_swapchain_frame_semaphore_timeout timeout_ms={} has_outstanding_acquire={}",
                    kHostGpuWaitTimeoutMilliseconds,
                    mHasOutstandingAcquire);
                flushLogger(mDevice != nullptr ? mDevice->getLogger() : Logger{});
                throw std::runtime_error("MSwapchain::queryNextTexture timed out while waiting for an available Metal frame slot.");
            }
        }
        CA::MetalDrawable *drawable = nullptr;
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::queryNextTexture.nextDrawable",
                "has_outstanding_acquire={}",
                mHasOutstandingAcquire);
            drawable = this->mNativeSwapchain->nextDrawable();
        }
        if (drawable == nullptr || drawable->texture() == nullptr)
        {
            GVMCpuProbeInstant(mDevice, SwapchainProbeCategory, "mswapchain.acquire.drawable_nil");
            std::fprintf(stderr, "[gvm-metal-swapchain] nextDrawable or its texture is nil.\n");
            dispatch_semaphore_signal(this->mDevice->getFrameSemaphore());
            const CGSize drawableSize = mNativeSwapchain->drawableSize();
            const uint32_t drawableWidth = std::max<uint32_t>(static_cast<uint32_t>(drawableSize.width), 1u);
            const uint32_t drawableHeight = std::max<uint32_t>(static_cast<uint32_t>(drawableSize.height), 1u);
            if (mFallbackTexture.isNull() ||
                mFallbackTexture->getWidth() != drawableWidth ||
                mFallbackTexture->getHeight() != drawableHeight)
            {
                recreateFallbackTexture(drawableWidth, drawableHeight);
            }
            SwapchainQueryResult result;
            result.status = SwapchainNextTextureQueryStatus::Success;
            result.texture = mFallbackTexture;
            this->mCurrentResult = result;
            this->mHasOutstandingAcquire = true;
            return result;
        }
        this->mAcquiredDrawable = NS::RetainPtr(drawable);
        this->mAcquiredNativeTexture = NS::RetainPtr(drawable->texture());
        this->mDrawableTexture = mDevice->wrapNativeTexture(
            *this->mAcquiredNativeTexture.get(),
            TextureUsage::TextureBinding | TextureUsage::StorageBinding | TextureUsage::RenderAttachment | TextureUsage::CopyDst | TextureUsage::CopySrc,
            "SwapchainDrawableTexture");
        if (mFallbackTexture.isNull() == false)
        {
            mDevice->freeTexture(mFallbackTexture);
            mFallbackTexture.reset();
        }
        SwapchainQueryResult result;
        result.status = SwapchainNextTextureQueryStatus::Success;
        result.texture = mDrawableTexture;
        this->mCurrentResult = result;
        this->mHasOutstandingAcquire = true;
        return result;
    }

    void MSwapchain::present()
    {
        mDevice->getLostState().throwIfLost("MSwapchain::present");
        if (!mHasOutstandingAcquire)
        {
            return;
        }
        mHasOutstandingAcquire = false;
        if (mAcquiredDrawable.get() == nullptr)
        {
            return;
        }
        auto presentCMD = this->mDevice->getMainQueueImpl()->getNativeQueue()->commandBuffer();
        auto frameSemaphore = this->mDevice->getFrameSemaphore();
        auto presentCompletionGroup = this->mPresentCompletionGroup;

        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::present.encodePresent",
                "has_drawable={}",
                this->mAcquiredDrawable.get() != nullptr);
            // presentCMD->presentDrawableAtTime(this->mAcquiredDrawable.get(), 1. / 60.);
            presentCMD->presentDrawable(this->mAcquiredDrawable.get());
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
                "has_drawable={}",
                this->mAcquiredDrawable.get() != nullptr);
            presentCMD->commit();
        }
        presentCMD = nullptr;
        mAcquiredDrawable.reset();
        {
            GVMCpuProbeScopeDetail(
                mDevice,
                SwapchainProbeCategory,
                "MSwapchain::present.frameLifecycle",
                "has_drawable_texture={}",
                !mDrawableTexture.isNull());
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
            "has_outstanding_acquire={} has_present_completion_group={}",
            mHasOutstandingAcquire,
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
                    "event=metal_swapchain_present_completion_timeout timeout_ms={} has_outstanding_acquire={}",
                    kHostGpuWaitTimeoutMilliseconds,
                    mHasOutstandingAcquire);
                flushLogger(mDevice != nullptr ? mDevice->getLogger() : Logger{});
                throw std::runtime_error("MSwapchain::destroy timed out while waiting for pending Metal present completion.");
            }
            dispatch_release(mPresentCompletionGroup);
            mPresentCompletionGroup = nullptr;
        }
        if (mHasOutstandingAcquire && mAcquiredDrawable.get() != nullptr)
        {
            dispatch_semaphore_signal(this->mDevice->getFrameSemaphore());
        }
        mAcquiredDrawable.reset();
        mHasOutstandingAcquire = false;
        releaseDrawableTexture();
        if (mFallbackTexture.isNull() == false)
        {
            mDevice->freeTexture(mFallbackTexture);
            mFallbackTexture.reset();
        }
        mNativeSwapchain = nullptr;
        mDevice = nullptr;
    }

    void MSwapchain::releaseDrawableTexture()
    {
        if (mDrawableTexture.isNull() == false)
        {
            mDevice->freeTexture(mDrawableTexture);
            mDrawableTexture.reset();
        }
        mAcquiredNativeTexture.reset();
    }

    void MSwapchain::recreateFallbackTexture(uint32_t w, uint32_t h)
    {
        GVMCpuProbeScopeDetail(
            mDevice,
            SwapchainProbeCategory,
            "MSwapchain::recreateFallbackTexture",
            "requested_size={}x{}",
            w,
            h);
        w = std::max<uint32_t>(w, 1);
        h = std::max<uint32_t>(h, 1);
        if (mFallbackTexture.isNull() == false)
        {
            mDevice->freeTexture(mFallbackTexture);
            mFallbackTexture.reset();
        }
        TextureDescriptor tempTextureDescriptor{};
        tempTextureDescriptor.label = "SwapchainFallbackTexture";
        tempTextureDescriptor.usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc;
        tempTextureDescriptor.size = {.width = w, .height = h, .depth = 1};
        tempTextureDescriptor.format = mPreferredFormat;
        mFallbackTexture = mDevice->createTexture(tempTextureDescriptor);
    }


} // namespace GVM::RHI::Metal
