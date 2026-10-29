#include "VKSwapchain.hpp"
#include "VKCommandEncoder.hpp"

#include "VKDescriptorChurnCounters.hpp"
#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKInstance.hpp"
#include "VKLogging.hpp"
#include "VKQueue.hpp"
#include "VKTaskDependencyResolver.hpp"
#include "VKSyncUtils.hpp"
#include "VKTexture.hpp"
#include "Private/TimeUtils.hpp"

#include <GVMRHI/GVMCpuProbe.hpp>
#include <GVMRHI/Private/VulkanNativeSurface.hpp>

#if defined(__APPLE__)
#include <vulkan/vulkan_metal.h>
#endif

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/string.h>
#include <EASTL/atomic.h>
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace GVM::RHI::Vulkan
{
        namespace
    {
        constexpr eastl::string_view SwapchainLogCategory = "gvmrhi.vulkan.swapchain";
        constexpr uint64_t kHostGpuWaitTimeoutMilliseconds = 2000u;
        constexpr uint64_t kHostGpuWaitTimeoutNanoseconds = 2'000'000'000ull;
    } // namespace

namespace
    {
        constexpr uint32_t PreferredSwapchainImageCount = 3u;
        constexpr uint32_t MaxFramesInFlight = 3u;
        constexpr uint32_t InvalidSwapchainImageIndex = eastl::numeric_limits<uint32_t>::max();

        TextureFormat translateSurfaceFormat(vk::Format format)
        {
            switch (format)
            {
            case vk::Format::eB8G8R8A8Unorm: return TextureFormat::BGRA8Unorm;
            case vk::Format::eB8G8R8A8Srgb: return TextureFormat::BGRA8UnormSrgb;
            case vk::Format::eR8G8B8A8Unorm: return TextureFormat::RGBA8Unorm;
            case vk::Format::eR8G8B8A8Srgb: return TextureFormat::RGBA8UnormSrgb;
            default: return TextureFormat::Undefined;
            }
        }

        eastl::vector<TextureFormat> collectSupportedFormats(const eastl::vector<vk::SurfaceFormatKHR> &formats)
        {
            eastl::vector<TextureFormat> supportedFormats;
            supportedFormats.reserve(formats.size());

            auto appendUnique = [&supportedFormats](TextureFormat format)
            {
                if (format == TextureFormat::Undefined)
                {
                    return;
                }

                if (eastl::find(supportedFormats.begin(), supportedFormats.end(), format) == supportedFormats.end())
                {
                    supportedFormats.push_back(format);
                }
            };

            constexpr TextureFormat preferredFormats[] = {
                TextureFormat::BGRA8Unorm,
                TextureFormat::BGRA8UnormSrgb,
                TextureFormat::RGBA8Unorm,
                TextureFormat::RGBA8UnormSrgb,
            };

            for (TextureFormat preferredFormat : preferredFormats)
            {
                for (const vk::SurfaceFormatKHR &format : formats)
                {
                    if (translateSurfaceFormat(format.format) == preferredFormat &&
                        format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                    {
                        appendUnique(preferredFormat);
                    }
                }
            }

            for (const vk::SurfaceFormatKHR &format : formats)
            {
                appendUnique(translateSurfaceFormat(format.format));
            }

            return supportedFormats;
        }

        vk::SurfaceFormatKHR selectSurfaceFormat(const eastl::vector<vk::SurfaceFormatKHR> &formats)
        {
            if (formats.empty())
            {
                throw std::runtime_error("VKSwapchain requires at least one supported surface format.");
            }

            constexpr vk::Format preferredFormats[] = {
                vk::Format::eB8G8R8A8Unorm,
                vk::Format::eB8G8R8A8Srgb,
                vk::Format::eR8G8B8A8Unorm,
                vk::Format::eR8G8B8A8Srgb,
            };

            for (vk::Format preferredFormat : preferredFormats)
            {
                for (const vk::SurfaceFormatKHR &format : formats)
                {
                    if (format.format == preferredFormat &&
                        translateSurfaceFormat(format.format) != TextureFormat::Undefined &&
                        format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                    {
                        return format;
                    }
                }
            }

            for (const vk::SurfaceFormatKHR &format : formats)
            {
                if (translateSurfaceFormat(format.format) != TextureFormat::Undefined)
                {
                    return format;
                }
            }

            throw std::runtime_error("VKSwapchain could not map any supported surface format to a GVM texture format.");
        }

        vk::PresentModeKHR selectPresentMode(const eastl::vector<vk::PresentModeKHR> &presentModes)
        {
            for (vk::PresentModeKHR presentMode : presentModes)
            {
                if (presentMode == vk::PresentModeKHR::eMailbox)
                {
                    return presentMode;
                }
            }
            for (vk::PresentModeKHR presentMode : presentModes)
            {
                if (presentMode == vk::PresentModeKHR::eImmediate)
                {
                    return presentMode;
                }
            }
            return vk::PresentModeKHR::eFifo;
        }

        const char *presentModeName(vk::PresentModeKHR presentMode)
        {
            switch (presentMode)
            {
            case vk::PresentModeKHR::eImmediate: return "Immediate";
            case vk::PresentModeKHR::eMailbox: return "Mailbox";
            case vk::PresentModeKHR::eFifo: return "Fifo";
            case vk::PresentModeKHR::eFifoRelaxed: return "FifoRelaxed";
            case vk::PresentModeKHR::eSharedDemandRefresh: return "SharedDemandRefresh";
            case vk::PresentModeKHR::eSharedContinuousRefresh: return "SharedContinuousRefresh";
            default: return "Unknown";
            }
        }

        eastl::string buildPresentModeList(const eastl::vector<vk::PresentModeKHR> &presentModes)
        {
            eastl::string text;
            for (size_t i = 0; i < presentModes.size(); ++i)
            {
                if (i != 0)
                {
                    text += ", ";
                }
                text += presentModeName(presentModes[i]);
            }
            return text;
        }

        uint64_t semaphoreDiagnosticHandle(vk::Semaphore semaphore)
        {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<VkSemaphore>(semaphore)));
        }

        /// Returns a stable diagnostic name for a native surface kind.
        const char *nativeSurfaceKindName(NativeSurfaceKind kind)
        {
            switch (kind)
            {
            case NativeSurfaceKind::Undefined: return "Undefined";
            case NativeSurfaceKind::MetalLayer: return "MetalLayer";
            case NativeSurfaceKind::AndroidWindow: return "AndroidWindow";
            case NativeSurfaceKind::OhosWindow: return "OhosWindow";
            case NativeSurfaceKind::Win32Window: return "Win32Window";
            case NativeSurfaceKind::WaylandSurface: return "WaylandSurface";
            case NativeSurfaceKind::XcbWindow: return "XcbWindow";
            case NativeSurfaceKind::XlibWindow: return "XlibWindow";
            default: return "Unknown";
            }
        }

        /// Converts the legacy Vulkan-private surface tag into the public native surface tag.
        NativeSurfaceKind toPublicSurfaceKind(GVM::RHI::Vulkan::Private::NativeSurfaceKind kind)
        {
            switch (kind)
            {
            case GVM::RHI::Vulkan::Private::NativeSurfaceKind::MetalLayer: return NativeSurfaceKind::MetalLayer;
            case GVM::RHI::Vulkan::Private::NativeSurfaceKind::Win32: return NativeSurfaceKind::Win32Window;
            case GVM::RHI::Vulkan::Private::NativeSurfaceKind::Android: return NativeSurfaceKind::AndroidWindow;
            case GVM::RHI::Vulkan::Private::NativeSurfaceKind::Wayland: return NativeSurfaceKind::WaylandSurface;
            case GVM::RHI::Vulkan::Private::NativeSurfaceKind::Xcb: return NativeSurfaceKind::XcbWindow;
            case GVM::RHI::Vulkan::Private::NativeSurfaceKind::Xlib: return NativeSurfaceKind::XlibWindow;
            default: return NativeSurfaceKind::Undefined;
            }
        }

        /// Resolves the typed native surface descriptor while preserving the legacy A/B compatibility path.
        NativeSurfaceDescriptor resolveNativeSurfaceDescriptor(const SwapchainDescriptor &descriptor)
        {
            if (descriptor.nativeSurface.kind != NativeSurfaceKind::Undefined)
            {
                return descriptor.nativeSurface;
            }

            NativeSurfaceDescriptor resolved = {};
            resolved.surface = descriptor.A;
            if (const auto *surfaceInfo = GVM::RHI::Vulkan::Private::tryGetNativeSurfaceInfo(descriptor.B);
                surfaceInfo != nullptr)
            {
                resolved.kind = toPublicSurfaceKind(surfaceInfo->kind);
                resolved.displayOrInstance = surfaceInfo->displayOrInstance;
                if (surfaceInfo->extentHint != nullptr)
                {
                    resolved.extentHint = *surfaceInfo->extentHint;
                    resolved.hasExtentHint = True;
                }
            }
            return resolved;
        }

        /// Builds a readable error when a swapchain receives the wrong native surface kind.
        std::runtime_error makeSurfaceKindError(const char *platformName,
                                                NativeSurfaceKind expectedKind,
                                                const NativeSurfaceDescriptor &surface)
        {
            eastl::string message = "VKSwapchain::createSurface on ";
            message += platformName;
            message += " expected native surface kind ";
            message += nativeSurfaceKindName(expectedKind);
            message += ", but received ";
            message += nativeSurfaceKindName(surface.kind);
            message += " with surface pointer ";
            message += (surface.surface != nullptr) ? "set." : "null.";
            return std::runtime_error((message).c_str());
        }

        vk::CompositeAlphaFlagBitsKHR selectCompositeAlpha(vk::CompositeAlphaFlagsKHR supportedCompositeAlpha)
        {
            constexpr vk::CompositeAlphaFlagBitsKHR preferredCompositeAlpha[] = {
                vk::CompositeAlphaFlagBitsKHR::eOpaque,
                vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                vk::CompositeAlphaFlagBitsKHR::eInherit,
            };

            for (vk::CompositeAlphaFlagBitsKHR candidate : preferredCompositeAlpha)
            {
                if ((supportedCompositeAlpha & candidate) == candidate)
                {
                    return candidate;
                }
            }
            return vk::CompositeAlphaFlagBitsKHR::eOpaque;
        }

        vk::Extent2D resolveSwapchainExtent(const vk::SurfaceCapabilitiesKHR &capabilities, const SwapchainDescriptor &descriptor)
        {
            if (capabilities.currentExtent.width != eastl::numeric_limits<uint32_t>::max())
            {
                return capabilities.currentExtent;
            }

            vk::Extent2D extent = {1u, 1u};
            const NativeSurfaceDescriptor nativeSurface = resolveNativeSurfaceDescriptor(descriptor);
            const auto *legacySurfaceInfo = GVM::RHI::Vulkan::Private::tryGetNativeSurfaceInfo(descriptor.B);
            if (nativeSurface.hasExtentHint != False)
            {
                extent.width = nativeSurface.extentHint.width;
                extent.height = nativeSurface.extentHint.height;
            }
            else if (descriptor.B != nullptr && legacySurfaceInfo == nullptr)
            {
                const auto *hint = static_cast<const Extent3D *>(descriptor.B);
                extent.width = hint->width;
                extent.height = hint->height;
            }

            extent.width = eastl::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = eastl::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            extent.width = eastl::max(extent.width, 1u);
            extent.height = eastl::max(extent.height, 1u);
            return extent;
        }

        uint32_t resolveSwapchainImageCount(const vk::SurfaceCapabilitiesKHR &capabilities, uint32_t preferredImageCount)
        {
            uint32_t imageCount = eastl::max(capabilities.minImageCount, preferredImageCount);
            if (capabilities.maxImageCount != 0u)
            {
                imageCount = eastl::min(imageCount, capabilities.maxImageCount);
            }
            return imageCount;
        }

        [[nodiscard]]
        double durationMilliseconds(std::chrono::steady_clock::time_point startTime)
        {
            return Internal::elapsedMilliseconds(startTime);
        }

        SwapchainNextTextureQueryStatus translateAcquireStatus(vk::Result result)
        {
            switch (result)
            {
            case vk::Result::eSuccess:
            case vk::Result::eSuboptimalKHR: return SwapchainNextTextureQueryStatus::Success;
            case vk::Result::eTimeout: return SwapchainNextTextureQueryStatus::Timeout;
            case vk::Result::eErrorOutOfDateKHR: return SwapchainNextTextureQueryStatus::Outdated;
            case vk::Result::eErrorOutOfHostMemory:
            case vk::Result::eErrorOutOfDeviceMemory: return SwapchainNextTextureQueryStatus::OutOfMemory;
            case vk::Result::eErrorDeviceLost: return SwapchainNextTextureQueryStatus::DeviceLost;
            default: return SwapchainNextTextureQueryStatus::Lost;
            }
        }

    } // namespace

    void VKSwapchain::init(VKDevice *device, const SwapchainDescriptor &descriptor)
    {
        const NativeSurfaceDescriptor nativeSurface = resolveNativeSurfaceDescriptor(descriptor);
        GVMCpuProbeScopeDetail(
            device,
            SwapchainLogCategory,
            "VKSwapchain::init",
            "descriptor_has_native_handle={} descriptor_has_typed_native_surface={} descriptor_has_surface_info={} native_surface_kind={}",
            descriptor.A != nullptr,
            descriptor.nativeSurface.kind != NativeSurfaceKind::Undefined,
            descriptor.B != nullptr,
            nativeSurfaceKindName(nativeSurface.kind));
        if (device == nullptr)
        {
            throw std::invalid_argument("VKSwapchain::init requires a valid Vulkan device.");
        }
        if (nativeSurface.surface == nullptr)
        {
            throw std::invalid_argument("VKSwapchain::init requires a valid native presentation handle in descriptor.nativeSurface.surface or legacy descriptor.A.");
        }
        if (!device->getCapabilities().supportsSwapchain())
        {
            throw std::runtime_error("VKSwapchain::init requires VK_KHR_swapchain support on the selected Vulkan device.");
        }

        mDevice = device;
        mLogger = device->getLogger();
        mLogContext = device->getLogContext();
        mQueue = device->getMainQueueImpl();
        mDescriptor = descriptor;
        mSurface = createSurface(descriptor);
        GVMLogInfo(
            this, SwapchainLogCategory,
            "event=swapchain_init_begin queue_family_index={} frames_in_flight={}",
            mQueue != nullptr ? mQueue->getQueueFamilyIndex() : 0u,
            MaxFramesInFlight);

        if (!mDevice->getPhysicalDevice().getSurfaceSupportKHR(mQueue->getQueueFamilyIndex(), mSurface.get()))
        {
            throw std::runtime_error("VKSwapchain::init requires a queue family that supports presentation to the requested surface.");
        }

        vk::SemaphoreCreateInfo semaphoreCreateInfo = {};
        mPresentFrames.clear();
        mPresentFrames.reserve(MaxFramesInFlight);
        for (uint32_t frameIndex = 0; frameIndex < MaxFramesInFlight; ++frameIndex)
        {
            PresentFrame frame = {};
            frame.imageAvailableSemaphore = mDevice->getNativeDevice().createSemaphoreUnique(semaphoreCreateInfo);
            frame.lastAcquiredImageIndex = InvalidSwapchainImageIndex;
            mPresentFrames.push_back(eastl::move(frame));
        }

        recreateSwapchain(querySurfaceConfig());
        GVMLogInfo(this, SwapchainLogCategory, "event=swapchain_init_end present_frame_count={}", mPresentFrames.size());
    }

    uint32_t VKSwapchain::acquirePresentFrameSlot()
    {
        GVMCpuProbeScopeDetail(
            this,
            SwapchainLogCategory,
            "VKSwapchain::queryNextTextureDirect.acquirePresentFrameSlot",
            "cursor={} present_frame_count={} outstanding_direct_present={}",
            mNextFrameSlotCursor,
            mPresentFrames.size(),
            mHasOutstandingDirectPresentImage);

        if (mPresentFrames.empty())
        {
            throw std::runtime_error("VKSwapchain::acquirePresentFrameSlot requires at least one present frame slot.");
        }

        const uint32_t frameCount = static_cast<uint32_t>(mPresentFrames.size());
        uint32_t selectedFrameSlot = InvalidSwapchainImageIndex;
        uint32_t oldestInFlightFrameSlot = InvalidSwapchainImageIndex;
        uint64_t oldestInFlightSubmissionId = eastl::numeric_limits<uint64_t>::max();

        for (uint32_t offset = 0u; offset < frameCount; ++offset)
        {
            const uint32_t frameSlot = (mNextFrameSlotCursor + offset) % frameCount;
            PresentFrame &frame = mPresentFrames[frameSlot];

            if (frame.submissionSerial == 0u)
            {
                selectedFrameSlot = frameSlot;
                break;
            }

            const uint64_t submissionId = frame.submissionSerial;
            if (submissionId < oldestInFlightSubmissionId)
            {
                oldestInFlightSubmissionId = submissionId;
                oldestInFlightFrameSlot = frameSlot;
            }

            if (mQueue != nullptr && mQueue->isRetired(frame.submissionSerial))
            {
                frame.submissionSerial = 0u;
                selectedFrameSlot = frameSlot;
                break;
            }

            VKQueue::SubmissionStatusSnapshot submissionStatus = {};
            if (mQueue != nullptr &&
                mQueue->trySampleSubmissionStatus(frame.submissionSerial, submissionStatus) &&
                submissionStatus.signaled)
            {
                mQueue->waitForSubmission(frame.submissionSerial, "swapchain_direct_completed_frame_context_reclaim");
                frame.submissionSerial = 0u;
                selectedFrameSlot = frameSlot;
                break;
            }
        }

        if (selectedFrameSlot == InvalidSwapchainImageIndex)
        {
            if (oldestInFlightFrameSlot == InvalidSwapchainImageIndex)
            {
                oldestInFlightFrameSlot = mNextFrameSlotCursor % frameCount;
            }

            PresentFrame &frame = mPresentFrames[oldestInFlightFrameSlot];
            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::queryNextTextureDirect.waitForAvailableFrameSlot",
                "frame_slot={} previous_submission_id={} previous_image_index={} previous_present_sequence={} all_frame_contexts_busy={}",
                oldestInFlightFrameSlot,
                frame.submissionSerial,
                frame.lastAcquiredImageIndex,
                frame.lastPresentSequence,
                true);

            if (mQueue != nullptr)
            {
                mQueue->waitForSubmission(frame.submissionSerial, "swapchain_direct_frame_context_exhausted");
            }
            frame.submissionSerial = 0u;
            selectedFrameSlot = oldestInFlightFrameSlot;
        }

        mCurrentFrameSlot = selectedFrameSlot;
        mNextFrameSlotCursor = (selectedFrameSlot + 1u) % frameCount;
        return selectedFrameSlot;
    }

    const eastl::shared_ptr<Internal::LogContext> &VKSwapchain::getLogContext() const
    {
        return mLogContext;
    }

    Logger VKSwapchain::getLogger() const
    {
        return mLogger;
    }

    MultipleElements<TextureFormat> VKSwapchain::getSupportedFormats() const
    {
        const eastl::vector<vk::SurfaceFormatKHR> surfaceFormats =
            toEastlVector(mDevice->getPhysicalDevice().getSurfaceFormatsKHR(mSurface.get()));
        MultipleElements<TextureFormat> supportedFormats;
        supportedFormats = collectSupportedFormats(surfaceFormats);
        return supportedFormats;
    }

    TextureFormat VKSwapchain::getPreferredFormat() const
    {
        if (mSwapchainTextureFormat != TextureFormat::Undefined)
        {
            return mSwapchainTextureFormat;
        }

        return querySurfaceConfig().format;
    }

    SwapchainQueryResult VKSwapchain::queryNextTexture()
    {
        GVMCpuProbeScopeDetail(
            this,
            SwapchainLogCategory,
            "VKSwapchain::queryNextTexture",
            "destroyed={} current_frame_slot={}",
            mDestroyed,
            mCurrentFrameSlot);
        if (mDestroyed)
        {
            return {.texture = {}, .status = SwapchainNextTextureQueryStatus::Lost};
        }

        recreateSwapchainIfNeeded();
        return queryNextTextureDirect();
    }

    void VKSwapchain::present()
    {
        mDevice->getLostState().throwIfLost("VKSwapchain::present");
        GVMCpuProbeScopeDetail(
            this,
            SwapchainLogCategory,
            "VKSwapchain::present",
            "current_frame_slot={} swapchain_images={}",
            mCurrentFrameSlot,
            mSwapchainImages.size());
        if (mDestroyed || !mSwapchain)
        {
            return;
        }

        presentDirectSwapchainImage();
    }

    void VKSwapchain::destroy()
    {
        GVMCpuProbeScopeDetail(
            this,
            SwapchainLogCategory,
            "VKSwapchain::destroy",
            "present_frames={} swapchain_images={}",
            mPresentFrames.size(),
            mSwapchainImages.size());
        if (mDestroyed)
        {
            return;
        }

        GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_destroy_begin");

        if (mQueue != nullptr)
        {
            mQueue->waitForSubmission(mQueue->getMostRecentSubmissionSerial(), "swapchain_destroy");
        }
        waitForAllPresentFrames();

        destroyDirectSwapchainTextures();

        mSwapchainImages.clear();
        mSwapchainImageStates.clear();
        mSwapchain.reset();
        mSurface.reset();
        mPresentFrames.clear();
        mQueue = nullptr;
        mDevice = nullptr;
        mDestroyed = true;
        GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_destroy_end");
        mLogger = nullptr;
        mLogContext.reset();
    }

    VKSwapchain::SurfaceConfig VKSwapchain::querySurfaceConfig() const
    {
        const vk::SurfaceCapabilitiesKHR capabilities = mDevice->getPhysicalDevice().getSurfaceCapabilitiesKHR(mSurface.get());
        if ((capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment) != vk::ImageUsageFlagBits::eColorAttachment)
        {
            throw std::runtime_error("Vulkan surface does not support color-attachment swapchain images, which the specification requires.");
        }
        const bool directImageCopySrcSupported =
            (capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc) == vk::ImageUsageFlagBits::eTransferSrc;

        const eastl::vector<vk::SurfaceFormatKHR> formats = toEastlVector(mDevice->getPhysicalDevice().getSurfaceFormatsKHR(mSurface.get()));
        const eastl::vector<vk::PresentModeKHR> presentModes = toEastlVector(mDevice->getPhysicalDevice().getSurfacePresentModesKHR(mSurface.get()));

        const vk::SurfaceFormatKHR surfaceFormat = selectSurfaceFormat(formats);
        SurfaceConfig config = {};
        config.format = translateSurfaceFormat(surfaceFormat.format);
        config.colorSpace = surfaceFormat.colorSpace;
        config.presentMode = selectPresentMode(presentModes);
        config.extent = resolveSwapchainExtent(capabilities, mDescriptor);
        config.imageCount = resolveSwapchainImageCount(capabilities, PreferredSwapchainImageCount);
        config.compositeAlpha = selectCompositeAlpha(capabilities.supportedCompositeAlpha);
        config.directImageCopySrcSupported = directImageCopySrcSupported;
        return config;
    }

    void VKSwapchain::recreateSwapchainIfNeeded()
    {
        const SurfaceConfig config = querySurfaceConfig();
        if (!mSwapchain ||
            mSwapchainExtent != config.extent ||
            mSwapchainTextureFormat != config.format)
        {
            recreateSwapchain(config);
        }
    }

    void VKSwapchain::recreateSwapchain(const SurfaceConfig &config)
    {
        GVMLogWarn(
            this, SwapchainLogCategory,
            "event=swapchain_recreate_begin extent={}x{} format={} present_mode={} image_count={}",
            config.extent.width,
            config.extent.height,
            static_cast<uint32_t>(config.format),
            presentModeName(config.presentMode),
            config.imageCount);
        if (mQueue != nullptr)
        {
            mQueue->waitForSubmission(mQueue->getMostRecentSubmissionSerial(), "swapchain_recreate");
        }
        waitForAllPresentFrames();
        vk::SwapchainKHR oldSwapchain = mSwapchain ? mSwapchain.get() : vk::SwapchainKHR{};

        const eastl::vector<vk::PresentModeKHR> presentModes =
            toEastlVector(mDevice->getPhysicalDevice().getSurfacePresentModesKHR(mSurface.get()));
        if (config.presentMode != vk::PresentModeKHR::eMailbox)
        {
            GVMLogWarn(
                this, SwapchainLogCategory,
                "event=swapchain_present_mode_fallback selected={} supported=[{}] image_count={}",
                presentModeName(config.presentMode),
                buildPresentModeList(presentModes).c_str(),
                config.imageCount);
        }
        GVMLogInfo(
            this, SwapchainLogCategory,
            "event=swapchain_present_mode_selected selected={} supported=[{}] image_count={}",
            presentModeName(config.presentMode),
            buildPresentModeList(presentModes).c_str(),
            config.imageCount);

        vk::SwapchainCreateInfoKHR createInfo = {};
        createInfo.surface = mSurface.get();
        createInfo.minImageCount = config.imageCount;
        createInfo.imageFormat = translateTextureFormat(config.format);
        createInfo.imageColorSpace = config.colorSpace;
        createInfo.imageExtent = config.extent;
        createInfo.imageArrayLayers = 1u;
        createInfo.imageUsage = vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment);
        if (config.directImageCopySrcSupported)
        {
            createInfo.imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
        }
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
        createInfo.preTransform = mDevice->getPhysicalDevice().getSurfaceCapabilitiesKHR(mSurface.get()).currentTransform;
        createInfo.compositeAlpha = config.compositeAlpha;
        createInfo.presentMode = config.presentMode;
        createInfo.clipped = vk::True;
        createInfo.oldSwapchain = oldSwapchain;

        vk::UniqueSwapchainKHR newSwapchain = mDevice->getNativeDevice().createSwapchainKHRUnique(createInfo);
        mDevice->setDebugObjectName(vk::ObjectType::eSwapchainKHR, reinterpret_cast<uint64_t>(static_cast<VkSwapchainKHR>(newSwapchain.get())), "VulkanSwapchain");
        const eastl::vector<vk::Image> swapchainImages = toEastlVector(mDevice->getNativeDevice().getSwapchainImagesKHR(newSwapchain.get()));

        mSwapchain = eastl::move(newSwapchain);
        mSwapchainImages.clear();
        mSwapchainImages.reserve(swapchainImages.size());
        for (size_t imageIndex = 0; imageIndex < swapchainImages.size(); ++imageIndex)
        {
            vk::Image image = swapchainImages[imageIndex];
            const eastl::string imageLabel = "VulkanSwapchain image " + eastl::to_string(imageIndex);
            mDevice->setDebugObjectName(vk::ObjectType::eImage, reinterpret_cast<uint64_t>(static_cast<VkImage>(image)), imageLabel.c_str());
            mSwapchainImages.push_back(image);
        }
        mSwapchainFormat = translateTextureFormat(config.format);
        mSwapchainTextureFormat = config.format;
        mSwapchainExtent = config.extent;
        destroyDirectSwapchainTextures();
        mSwapchainImageStates.clear();
        mSwapchainImageStates.reserve(mSwapchainImages.size());
        vk::SemaphoreCreateInfo semaphoreCreateInfo = {};
        for (size_t imageIndex = 0; imageIndex < mSwapchainImages.size(); ++imageIndex)
        {
            SwapchainImageState imageState = {};
            imageState.lastAcquireFrameSlot = InvalidSwapchainImageIndex;
            imageState.renderCompleteSemaphore = mDevice->getNativeDevice().createSemaphoreUnique(semaphoreCreateInfo);
            mSwapchainImageStates.push_back(eastl::move(imageState));
        }
        mCurrentImageIndex = 0u;
        mCurrentFrameSlot = 0u;
        mNextFrameSlotCursor = 0u;
        mHasOutstandingDirectPresentImage = false;
        for (PresentFrame &frame : mPresentFrames)
        {
            frame.submissionSerial = 0u;
            frame.imageAvailableGeneration = 0u;
            frame.lastSubmittedSubmissionId = 0u;
            frame.lastPresentSequence = 0u;
            frame.lastAcquiredImageIndex = InvalidSwapchainImageIndex;
        }

        recreateDirectSwapchainTextures(config.format, config.extent, config.directImageCopySrcSupported);
        mCurrentResult.texture = {};
        mCurrentResult.status = SwapchainNextTextureQueryStatus::Success;
        GVMLogWarn(
            this, SwapchainLogCategory,
            "event=swapchain_recreate_end image_count={} extent={}x{} direct_swapchain_images={}",
            mSwapchainImages.size(),
            mSwapchainExtent.width,
            mSwapchainExtent.height,
            mDirectSwapchainTextures.size());
    }

    void VKSwapchain::recreateDirectSwapchainTextures(TextureFormat format, const vk::Extent2D &extent, bool copySrcSupported)
    {
        destroyDirectSwapchainTextures();
        mDirectSwapchainTextures.reserve(mSwapchainImages.size());

        for (size_t imageIndex = 0; imageIndex < mSwapchainImages.size(); ++imageIndex)
        {
            TextureUsageFlags usage = TextureUsage::RenderAttachment;
            if (copySrcSupported)
            {
                usage |= TextureUsage::CopySrc;
            }
            Texture texture = mDevice->createSwapchainImageTexture({
                .label = "VulkanSwapchainImage_" + eastl::to_string(imageIndex),
                .usage = usage,
                .dimension = TextureDimension::e2D,
                .size = {extent.width, extent.height, 1u},
                .format = format,
                .mipLevelCount = 1u,
                .arrayLayerCount = 1u,
            },
                mSwapchainImages[imageIndex]);
            auto *textureImpl = static_cast<VKTexture *>(texture.get());
            mDevice->getPresentManager().registerDirectPresentTexture(textureImpl);
            if (mQueue != nullptr)
            {
                mQueue->getResourceStateDB().recordExternalTextureState(
                    *textureImpl,
                    TextureAspect::All,
                    0u,
                    textureImpl->getMipLevelCount(),
                    0u,
                    textureImpl->getCachedArrayLayerCount(),
                    mSwapchainImageStates[imageIndex].layout,
                    mSwapchainImageStates[imageIndex].stageMask,
                    mSwapchainImageStates[imageIndex].accessMask);
            }
            mDirectSwapchainTextures.push_back(texture);
        }
    }

    void VKSwapchain::destroyDirectSwapchainTextures()
    {
        for (Texture &texture : mDirectSwapchainTextures)
        {
            if (!texture.isNull())
            {
                if (mDevice != nullptr)
                {
                    mDevice->getPresentManager().unregisterDirectPresentTexture(static_cast<VKTexture *>(texture.get()));
                }
                mDevice->freeTexture(texture);
                texture.reset();
            }
        }
        mDirectSwapchainTextures.clear();
        mHasOutstandingDirectPresentImage = false;
    }

    vk::UniqueSurfaceKHR VKSwapchain::createSurface(const SwapchainDescriptor &descriptor)
    {
        const NativeSurfaceDescriptor nativeSurface = resolveNativeSurfaceDescriptor(descriptor);
#if defined(__APPLE__)
        if (nativeSurface.kind != NativeSurfaceKind::Undefined &&
            nativeSurface.kind != NativeSurfaceKind::MetalLayer)
        {
            throw makeSurfaceKindError("Apple", NativeSurfaceKind::MetalLayer, nativeSurface);
        }
        const void *metalLayer = nativeSurface.surface != nullptr ? nativeSurface.surface : descriptor.A;
        if (metalLayer == nullptr)
        {
            throw makeSurfaceKindError("Apple", NativeSurfaceKind::MetalLayer, nativeSurface);
        }

        vk::MetalSurfaceCreateInfoEXT createInfo = {};
        createInfo.pLayer = static_cast<const CAMetalLayer *>(metalLayer);
        return mDevice->getInstance()->getNativeInstance().createMetalSurfaceEXTUnique(createInfo);
#elif defined(_WIN32)
        if (nativeSurface.kind != NativeSurfaceKind::Win32Window || nativeSurface.surface == nullptr)
        {
            throw makeSurfaceKindError("Win32", NativeSurfaceKind::Win32Window, nativeSurface);
        }

        vk::Win32SurfaceCreateInfoKHR createInfo = {};
        createInfo.hwnd = static_cast<HWND>(nativeSurface.surface);
        createInfo.hinstance = static_cast<HINSTANCE>(nativeSurface.displayOrInstance != nullptr ? nativeSurface.displayOrInstance : GetModuleHandleW(nullptr));
        return mDevice->getInstance()->getNativeInstance().createWin32SurfaceKHRUnique(createInfo);
#elif defined(__ANDROID__)
        if (nativeSurface.kind != NativeSurfaceKind::AndroidWindow || nativeSurface.surface == nullptr)
        {
            throw makeSurfaceKindError("Android", NativeSurfaceKind::AndroidWindow, nativeSurface);
        }

        vk::AndroidSurfaceCreateInfoKHR createInfo = {};
        createInfo.window = static_cast<ANativeWindow *>(nativeSurface.surface);
        return mDevice->getInstance()->getNativeInstance().createAndroidSurfaceKHRUnique(createInfo);
#elif defined(__OHOS__)
        if (nativeSurface.kind != NativeSurfaceKind::OhosWindow || nativeSurface.surface == nullptr)
        {
            throw makeSurfaceKindError("OHOS", NativeSurfaceKind::OhosWindow, nativeSurface);
        }

        VkSurfaceCreateInfoOHOS createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;
        createInfo.window = static_cast<OHNativeWindow *>(nativeSurface.surface);

        const VkInstance instance = static_cast<VkInstance>(mDevice->getInstance()->getNativeInstance());
        const auto createSurfaceOHOS = reinterpret_cast<PFN_vkCreateSurfaceOHOS>(
            vkGetInstanceProcAddr(instance, "vkCreateSurfaceOHOS"));
        if (createSurfaceOHOS == nullptr)
        {
            throw std::runtime_error("VKSwapchain::createSurface on OHOS could not load vkCreateSurfaceOHOS.");
        }

        VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
        const VkResult result = createSurfaceOHOS(
            instance,
            &createInfo,
            nullptr,
            &rawSurface);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("VKSwapchain::createSurface on OHOS failed to create VK_OHOS_surface.");
        }
        const vk::Instance nativeInstance = mDevice->getInstance()->getNativeInstance();
        return vk::UniqueSurfaceKHR(
            vk::SurfaceKHR(rawSurface),
            vk::ObjectDestroy<vk::Instance, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>(nativeInstance));
#else
        if (nativeSurface.kind == NativeSurfaceKind::Undefined)
        {
            throw makeSurfaceKindError("this platform", NativeSurfaceKind::WaylandSurface, nativeSurface);
        }

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
        if (nativeSurface.kind == NativeSurfaceKind::WaylandSurface)
        {
            vk::WaylandSurfaceCreateInfoKHR createInfo = {};
            createInfo.display = static_cast<wl_display *>(nativeSurface.displayOrInstance);
            createInfo.surface = static_cast<wl_surface *>(nativeSurface.surface);
            return mDevice->getInstance()->getNativeInstance().createWaylandSurfaceKHRUnique(createInfo);
        }
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
        if (nativeSurface.kind == NativeSurfaceKind::XcbWindow)
        {
            vk::XcbSurfaceCreateInfoKHR createInfo = {};
            createInfo.connection = static_cast<xcb_connection_t *>(nativeSurface.displayOrInstance);
            createInfo.window = static_cast<xcb_window_t>(reinterpret_cast<uintptr_t>(nativeSurface.surface));
            return mDevice->getInstance()->getNativeInstance().createXcbSurfaceKHRUnique(createInfo);
        }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
        if (nativeSurface.kind == NativeSurfaceKind::XlibWindow)
        {
            vk::XlibSurfaceCreateInfoKHR createInfo = {};
            createInfo.dpy = static_cast<Display *>(nativeSurface.displayOrInstance);
            createInfo.window = static_cast<Window>(reinterpret_cast<uintptr_t>(nativeSurface.surface));
            return mDevice->getInstance()->getNativeInstance().createXlibSurfaceKHRUnique(createInfo);
        }
#endif

        throw std::runtime_error((eastl::string("VKSwapchain::createSurface received unsupported native surface kind ") + nativeSurfaceKindName(nativeSurface.kind) + " for this platform/build.").c_str());
#endif
    }

    SwapchainQueryResult VKSwapchain::queryNextTextureDirect()
    {
        GVMCpuProbeScopeDetail(
            this,
            SwapchainLogCategory,
            "VKSwapchain::queryNextTextureDirect",
            "current_frame_slot={} outstanding_direct_present={}",
            mCurrentFrameSlot,
            mHasOutstandingDirectPresentImage);
        if (mHasOutstandingDirectPresentImage && !mCurrentResult.texture.isNull())
        {
            return mCurrentResult;
        }

        if (mPresentFrames.empty())
        {
            throw std::runtime_error("VKSwapchain::queryNextTextureDirect requires at least one present frame slot.");
        }

        mCurrentFrameSlot = acquirePresentFrameSlot();
        PresentFrame &frame = mPresentFrames[mCurrentFrameSlot];

        uint32_t acquiredImageIndex = 0u;
        const vk::Result acquireResult = [&]()
        {
            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::queryNextTextureDirect.acquireNextImageKHR",
                "frame_slot={} acquire_semaphore={} acquire_generation={}",
                mCurrentFrameSlot,
                semaphoreDiagnosticHandle(frame.imageAvailableSemaphore.get()),
                frame.imageAvailableGeneration + 1u);
            return static_cast<vk::Result>(VULKAN_HPP_DEFAULT_DISPATCHER.vkAcquireNextImageKHR(
                static_cast<VkDevice>(mDevice->getNativeDevice()),
                static_cast<VkSwapchainKHR>(mSwapchain.get()),
                kHostGpuWaitTimeoutNanoseconds,
                static_cast<VkSemaphore>(frame.imageAvailableSemaphore.get()),
                VK_NULL_HANDLE,
                &acquiredImageIndex));
        }();

        if (acquireResult == vk::Result::eErrorOutOfDateKHR)
        {
            recreateSwapchain(querySurfaceConfig());
            mCurrentResult = {.texture = {}, .status = SwapchainNextTextureQueryStatus::Outdated};
            return mCurrentResult;
        }

        if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR)
        {
            mCurrentResult = {.texture = {}, .status = translateAcquireStatus(acquireResult)};
            if (mCurrentResult.status == SwapchainNextTextureQueryStatus::DeviceLost)
            {
                mDevice->getLostState().markLost(DeviceLostReason::GpuFault, "acquireNextImageKHR returned VK_ERROR_DEVICE_LOST");
            }
            return mCurrentResult;
        }

        mCurrentImageIndex = acquiredImageIndex;
        if (mCurrentImageIndex >= mSwapchainImageStates.size() || mCurrentImageIndex >= mDirectSwapchainTextures.size())
        {
            throw std::runtime_error("VKSwapchain::queryNextTextureDirect acquired an out-of-range swapchain image index.");
        }

        SwapchainImageState &imageState = mSwapchainImageStates[mCurrentImageIndex];
        frame.imageAvailableGeneration += 1u;
        frame.lastAcquiredImageIndex = mCurrentImageIndex;

        Texture directTextureHandle = mDirectSwapchainTextures[mCurrentImageIndex];
        auto *directTexture = static_cast<VKTexture *>(directTextureHandle.get());
        VKPresentManager &presentManager = mDevice->getPresentManager();
        presentManager.resetDirectPresentSyncState(directTexture);
        mQueue->getResourceStateDB().recordExternalTextureState(
            *directTexture,
            TextureAspect::All,
            0u,
            directTexture->getMipLevelCount(),
            0u,
            directTexture->getCachedArrayLayerCount(),
            imageState.layout,
            imageState.stageMask,
            imageState.accessMask);
        presentManager.setDirectPresentAcquireState(
            directTexture,
            frame.imageAvailableSemaphore.get(),
            {},
            imageState.renderCompleteSemaphore.get());

        mHasOutstandingDirectPresentImage = true;
        mCurrentResult = {.texture = directTextureHandle, .status = SwapchainNextTextureQueryStatus::Success};
        GVMLogDebug(
            this, SwapchainLogCategory,
            "event=swapchain_direct_acquire_end frame_slot={} image_index={} acquire_semaphore={} acquire_generation={} render_complete_semaphore={} render_complete_generation={} previous_submission_id={} previous_present_sequence={} image_last_signal_submission_id={} image_last_present_submission_id={}",
            mCurrentFrameSlot,
            mCurrentImageIndex,
            semaphoreDiagnosticHandle(frame.imageAvailableSemaphore.get()),
            frame.imageAvailableGeneration,
            semaphoreDiagnosticHandle(imageState.renderCompleteSemaphore.get()),
            imageState.renderCompleteGeneration + 1u,
            frame.lastSubmittedSubmissionId,
            frame.lastPresentSequence,
            imageState.lastSignalSubmissionId,
            imageState.lastPresentedSubmissionId);
        return mCurrentResult;
    }

    void VKSwapchain::presentDirectSwapchainImage()
    {
        GVMCpuProbeScopeDetail(
            this,
            SwapchainLogCategory,
            "VKSwapchain::presentDirectSwapchainImage",
            "current_frame_slot={} image_index={} outstanding_direct_present={}",
            mCurrentFrameSlot,
            mCurrentImageIndex,
            mHasOutstandingDirectPresentImage);
        if (!mHasOutstandingDirectPresentImage || mCurrentResult.texture.isNull())
        {
            return;
        }
        if (mPresentFrames.empty())
        {
            throw std::runtime_error("VKSwapchain::presentDirectSwapchainImage requires at least one present frame slot.");
        }

        auto *directTexture = static_cast<VKTexture *>(mCurrentResult.texture.get());
        VKPresentManager &presentManager = mDevice->getPresentManager();
        if (directTexture == nullptr || !presentManager.isDirectPresentTexture(directTexture))
        {
            throw std::runtime_error("VKSwapchain::presentDirectSwapchainImage requires a direct-present swapchain texture.");
        }
        if (!presentManager.isDirectPresentSubmissionArmed(directTexture))
        {
            VKPresentManager::DirectPresentSubmitSync submitSync = {};
            if (!presentManager.tryGetDirectPresentSubmitSync(directTexture, submitSync))
            {
                throw std::runtime_error("VKSwapchain::presentDirectSwapchainImage cannot arm presentation because the acquired swapchain image has no pending acquire semaphore.");
            }

            // Even an untouched first-acquired image needs a real transition to PRESENT.
            CommandEncoder encoder = mQueue->createCommandEncoder();
            static_cast<VKCommandEncoder *>(encoder.get())->retainTexture(mCurrentResult.texture);
            encoder->end();
            mQueue->submit({encoder});
            mPresentFrames[mCurrentFrameSlot].submissionSerial = mQueue->getMostRecentSubmissionSerial();
        }

        PresentFrame &frame = mPresentFrames[mCurrentFrameSlot];
        if (frame.submissionSerial == 0u)
        {
            frame.submissionSerial = mQueue->getMostRecentSubmissionSerial();
        }
        frame.lastSubmittedSubmissionId = frame.submissionSerial;
        frame.lastPresentSequence = mPresentSequence + 1u;

        SwapchainImageState &imageState = mSwapchainImageStates[mCurrentImageIndex];
        const vk::Semaphore renderCompleteSemaphore = presentManager.getDirectPresentRenderCompleteSemaphore(directTexture);
        const uint64_t renderCompleteGeneration = imageState.renderCompleteGeneration + 1u;
        const vk::SwapchainKHR swapchainHandle = mSwapchain.get();
        vk::PresentInfoKHR presentInfo = {};
        presentInfo.waitSemaphoreCount = 1u;
        presentInfo.pWaitSemaphores = &renderCompleteSemaphore;
        presentInfo.swapchainCount = 1u;
        presentInfo.pSwapchains = &swapchainHandle;
        presentInfo.pImageIndices = &mCurrentImageIndex;

        GVMLogTrace(
            this, SwapchainLogCategory,
            "event=swapchain_present_direct_begin frame_slot={} present_sequence={} image_index={} submission_id={} wait_semaphore={} wait_generation={} acquire_semaphore={} acquire_generation={}",
            mCurrentFrameSlot,
            frame.lastPresentSequence,
            mCurrentImageIndex,
            frame.lastSubmittedSubmissionId,
            semaphoreDiagnosticHandle(renderCompleteSemaphore),
            renderCompleteGeneration,
            semaphoreDiagnosticHandle(frame.imageAvailableSemaphore.get()),
            frame.imageAvailableGeneration);
        const vk::Result presentResult = [&]()
        {
            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::presentDirectSwapchainImage.presentKHR",
                "frame_slot={} image_index={} submission_id={} wait_semaphore={} wait_generation={}",
                mCurrentFrameSlot,
                mCurrentImageIndex,
                frame.lastSubmittedSubmissionId,
                semaphoreDiagnosticHandle(renderCompleteSemaphore),
                renderCompleteGeneration);
            return static_cast<vk::Result>(VULKAN_HPP_DEFAULT_DISPATCHER.vkQueuePresentKHR(
                static_cast<VkQueue>(mQueue->getNativeQueue()),
                reinterpret_cast<const VkPresentInfoKHR *>(&presentInfo)));
        }();
        mHasOutstandingDirectPresentImage = false;
        presentManager.setDirectPresentPresented(directTexture);

        if (presentResult == vk::Result::eErrorDeviceLost)
        {
            mDevice->getLostState().markLost(DeviceLostReason::GpuFault, "presentKHR returned VK_ERROR_DEVICE_LOST");
            throw DeviceLostError("presentKHR returned VK_ERROR_DEVICE_LOST");
        }
        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
        {
            recreateSwapchain(querySurfaceConfig());
            mCurrentResult = {.texture = {}, .status = SwapchainNextTextureQueryStatus::Outdated};
            return;
        }
        if (presentResult != vk::Result::eSuccess)
        {
            throw std::runtime_error(
                "VKSwapchain::presentDirectSwapchainImage failed to queue presentation (" + vk::to_string(presentResult) + ").");
        }

        imageState.layout = vk::ImageLayout::ePresentSrcKHR;
        imageState.stageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
        imageState.accessMask = {};
        imageState.renderCompleteGeneration = renderCompleteGeneration;
        imageState.lastSignalSubmissionId = frame.lastSubmittedSubmissionId;
        imageState.lastPresentedSubmissionId = frame.lastSubmittedSubmissionId;
        imageState.lastAcquireGeneration = frame.imageAvailableGeneration;
        imageState.lastAcquireFrameSlot = mCurrentFrameSlot;
        imageState.lastPresentSequence = frame.lastPresentSequence;
        mQueue->getResourceStateDB().recordExternalTextureState(
            *directTexture,
            TextureAspect::All,
            0u,
            directTexture->getMipLevelCount(),
            0u,
            directTexture->getCachedArrayLayerCount(),
            imageState.layout,
            imageState.stageMask,
            imageState.accessMask);
        mPresentSequence = frame.lastPresentSequence;
        mCurrentResult = {.texture = {}, .status = SwapchainNextTextureQueryStatus::Success};
        GVMLogDebug(
            this, SwapchainLogCategory,
            "event=swapchain_present_direct_end frame_slot={} image_index={} submission_id={} wait_semaphore={} wait_generation={} acquire_semaphore={} acquire_generation={} present_sequence={}",
            mCurrentFrameSlot,
            mCurrentImageIndex,
            frame.lastSubmittedSubmissionId,
            semaphoreDiagnosticHandle(renderCompleteSemaphore),
            renderCompleteGeneration,
            semaphoreDiagnosticHandle(frame.imageAvailableSemaphore.get()),
            frame.imageAvailableGeneration,
            mPresentSequence);
        const VKDescriptorChurnSnapshot descriptorChurn =
            mDevice != nullptr ? mDevice->getDescriptorChurnCounters().takeAndReset() : VKDescriptorChurnSnapshot{};
        GVMCpuProbeInstantDetail(
            this,
            SwapchainLogCategory,
            "swapchain.direct.descriptor_churn",
            "present_sequence={} frame_slot={} image_index={} bind_groups_created={} bind_groups_destroyed={} descriptor_sets_allocated={} descriptor_sets_freed={}",
            mPresentSequence,
            mCurrentFrameSlot,
            mCurrentImageIndex,
            descriptorChurn.bindGroupCreated,
            descriptorChurn.bindGroupDestroyed,
            descriptorChurn.descriptorSetAllocated,
            descriptorChurn.descriptorSetFreed);
    }

    void VKSwapchain::waitForAllPresentFrames()
    {
        GVMCpuProbeScopeDetail(
            this,
            SwapchainLogCategory,
            "VKSwapchain::waitForAllPresentFrames",
            "present_frame_count={} current_frame_slot={}",
            mPresentFrames.size(),
            mCurrentFrameSlot);
        if (mDevice == nullptr)
        {
            return;
        }

        bool hasPendingPresentCompletion = false;
        for (const PresentFrame &frame : mPresentFrames)
        {
            if (frame.submissionSerial != 0u)
            {
                hasPendingPresentCompletion = true;
                break;
            }
        }
        if (hasPendingPresentCompletion && mQueue != nullptr)
        {
            GVMLogWarn(
                this, SwapchainLogCategory,
                "event=swapchain_wait_all_present_frames_begin pending_present_completion=true last_submission_id={}",
                mQueue->getMostRecentSubmissionSerial());
            mQueue->waitForSubmission(mQueue->getMostRecentSubmissionSerial(), "swapchain_wait_all_present_frames");
            GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_wait_all_present_frames_submission_wait_end");
        }
        if (mQueue != nullptr)
        {
            // Vulkan core does not provide a host-visible fence for "present retired".
            // Draining the present queue is the conservative teardown/recreate point.
            const auto waitIdleStartTime = std::chrono::steady_clock::now();
            GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_wait_all_present_frames_queue_idle_begin");
            try
            {
                (void)mQueue->getNativeQueue().waitIdle();
            }
            catch (const vk::DeviceLostError &error)
            {
                mDevice->getLostState().markLost(DeviceLostReason::GpuFault, error.what());
                GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_destroy_wait_idle_device_lost reason={}", error.what());
            }
            GVMLogWarn(
                this, SwapchainLogCategory,
                "event=swapchain_wait_all_present_frames_queue_idle_end duration_ms={:.3f}",
                durationMilliseconds(waitIdleStartTime));
        }
        for (PresentFrame &frame : mPresentFrames)
        {
            frame.submissionSerial = 0u;
        }
        GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_wait_all_present_frames_end");
    }
} // namespace GVM::RHI::Vulkan
