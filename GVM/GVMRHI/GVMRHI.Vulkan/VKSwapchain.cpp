#include "VKSwapchain.hpp"

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
                throw makeRuntimeError("VKSwapchain requires at least one supported surface format.");
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

            throw makeRuntimeError("VKSwapchain could not map any supported surface format to a GVM texture format.");
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
            return makeRuntimeError(message);
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

        void transitionExternalTexture(
            vk::CommandBuffer commandBuffer,
            VKQueue &queue,
            VKTexture &texture,
            vk::ImageLayout newLayout,
            vk::PipelineStageFlags dstStageMask,
            vk::AccessFlags dstAccessMask)
        {
            vk::ImageLayout oldLayout = vk::ImageLayout::eUndefined;
            vk::PipelineStageFlags oldStageMask = {};
            vk::AccessFlags oldAccessMask = {};
            if (!queue.getResourceStateDB().tryGetSharedTextureStateForTransition(
                    texture,
                    TextureAspect::All,
                    0u,
                    texture.getMipLevelCount(),
                    0u,
                    texture.getCachedArrayLayerCount(),
                    oldLayout,
                    oldStageMask,
                    oldAccessMask))
            {
                throw makeRuntimeError("VKSwapchain::transitionTexture could not resolve a uniform texture state for external transition.");
            }
            if (VKTaskDependencyResolver::transitionImageState(
                    commandBuffer,
                    texture.getNativeImage(),
                    texture.buildSubresourceRange(
                        TextureAspect::All,
                        0u,
                        texture.getMipLevelCount(),
                        0u,
                        texture.is3D() ? 1u : texture.getArrayLayerCount()),
                    oldLayout,
                    oldStageMask,
                    oldAccessMask,
                    newLayout,
                    dstStageMask,
                    dstAccessMask))
            {
                queue.getResourceStateDB().recordExternalTextureState(
                    texture,
                    TextureAspect::All,
                    0u,
                    texture.getMipLevelCount(),
                    0u,
                    texture.getCachedArrayLayerCount(),
                    newLayout,
                    dstStageMask,
                    dstAccessMask);
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
            throw makeInvalidArgument("VKSwapchain::init requires a valid Vulkan device.");
        }
        if (nativeSurface.surface == nullptr)
        {
            throw makeInvalidArgument("VKSwapchain::init requires a valid native presentation handle in descriptor.nativeSurface.surface or legacy descriptor.A.");
        }
        if (!device->supportsSwapchain())
        {
            throw makeRuntimeError("VKSwapchain::init requires VK_KHR_swapchain support on the selected Vulkan device.");
        }

        mDevice = device;
        mLogger = device->getLogger();
        mLogContext = device->getLogContext();
        mQueue = device->getMainQueueImpl();
        mDescriptor = descriptor;
        mSurface = createSurface(descriptor);
        mRenderDirectToAcquiredSwapchainImage = true;
        GVMLogInfo(
            this, SwapchainLogCategory,
            "event=swapchain_init_begin queue_family_index={} frames_in_flight={} direct_swapchain_image_requested={}",
            mQueue != nullptr ? mQueue->getQueueFamilyIndex() : 0u,
            MaxFramesInFlight,
            mRenderDirectToAcquiredSwapchainImage);

        if (!mDevice->getPhysicalDevice().getSurfaceSupportKHR(mQueue->getQueueFamilyIndex(), mSurface.get()))
        {
            throw makeRuntimeError("VKSwapchain::init requires a queue family that supports presentation to the requested surface.");
        }

        vk::CommandPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolCreateInfo.queueFamilyIndex = mQueue->getQueueFamilyIndex();
        mPresentCommandPool = mDevice->getNativeDevice().createCommandPoolUnique(poolCreateInfo);

        vk::CommandBufferAllocateInfo allocateInfo = {};
        allocateInfo.commandPool = mPresentCommandPool.get();
        allocateInfo.level = vk::CommandBufferLevel::ePrimary;
        allocateInfo.commandBufferCount = MaxFramesInFlight;
        eastl::vector<vk::CommandBuffer> commandBuffers = toEastlVector(mDevice->getNativeDevice().allocateCommandBuffers(allocateInfo));

        vk::SemaphoreCreateInfo semaphoreCreateInfo = {};
        mPresentFrames.clear();
        mPresentFrames.reserve(MaxFramesInFlight);
        for (uint32_t frameIndex = 0; frameIndex < MaxFramesInFlight; ++frameIndex)
        {
            PresentFrame frame = {};
            frame.commandBuffer = commandBuffers[frameIndex];
            frame.imageAvailableSemaphore = mDevice->getNativeDevice().createSemaphoreUnique(semaphoreCreateInfo);
            frame.lastAcquiredImageIndex = InvalidSwapchainImageIndex;
            mPresentFrames.push_back(eastl::move(frame));
        }

        recreateSwapchain(querySurfaceConfig());
        GVMLogInfo(this, SwapchainLogCategory, "event=swapchain_init_end present_frame_count={}", mPresentFrames.size());
    }

    uint32_t VKSwapchain::acquirePresentFrameSlot(bool directPresent)
    {
        if (directPresent)
        {
            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::queryNextTextureDirect.acquirePresentFrameSlot",
                "cursor={} present_frame_count={} outstanding_direct_present={}",
                mNextFrameSlotCursor,
                mPresentFrames.size(),
                mHasOutstandingDirectPresentImage);
        }
        else
        {
            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::present.acquirePresentFrameSlot",
                "cursor={} present_frame_count={} outstanding_direct_present={}",
                mNextFrameSlotCursor,
                mPresentFrames.size(),
                mHasOutstandingDirectPresentImage);
        }

        if (mPresentFrames.empty())
        {
            throw makeRuntimeError("VKSwapchain::acquirePresentFrameSlot requires at least one present frame slot.");
        }

        const uint32_t frameCount = static_cast<uint32_t>(mPresentFrames.size());
        uint32_t selectedFrameSlot = InvalidSwapchainImageIndex;
        uint32_t oldestInFlightFrameSlot = InvalidSwapchainImageIndex;
        uint64_t oldestInFlightSubmissionId = eastl::numeric_limits<uint64_t>::max();

        for (uint32_t offset = 0u; offset < frameCount; ++offset)
        {
            const uint32_t frameSlot = (mNextFrameSlotCursor + offset) % frameCount;
            PresentFrame &frame = mPresentFrames[frameSlot];

            if (!frame.submissionCompletion)
            {
                selectedFrameSlot = frameSlot;
                break;
            }

            const uint64_t submissionId = submissionDiagnosticId(frame.submissionCompletion);
            if (submissionId < oldestInFlightSubmissionId)
            {
                oldestInFlightSubmissionId = submissionId;
                oldestInFlightFrameSlot = frameSlot;
            }

            if (frame.submissionCompletion->completed.load(std::memory_order_acquire))
            {
                frame.submissionCompletion.reset();
                selectedFrameSlot = frameSlot;
                break;
            }

            VKQueue::SubmissionFenceStatusSnapshot fenceStatus = {};
            if (mQueue != nullptr &&
                mQueue->trySampleSubmissionFenceStatus(frame.submissionCompletion, fenceStatus) &&
                fenceStatus.signaled)
            {
                if (directPresent)
                {
                    mQueue->waitForSubmission(frame.submissionCompletion, "swapchain_direct_completed_frame_context_reclaim");
                }
                else
                {
                    mQueue->waitForSubmission(frame.submissionCompletion, "swapchain_present_completed_frame_context_reclaim");
                }
                frame.submissionCompletion.reset();
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
            if (directPresent)
            {
                GVMCpuProbeScopeDetail(
                    this,
                    SwapchainLogCategory,
                    "VKSwapchain::queryNextTextureDirect.waitForAvailableFrameSlot",
                    "frame_slot={} previous_submission_id={} previous_image_index={} previous_present_sequence={} all_frame_contexts_busy={}",
                    oldestInFlightFrameSlot,
                    submissionDiagnosticId(frame.submissionCompletion),
                    frame.lastAcquiredImageIndex,
                    frame.lastPresentSequence,
                    true);
            }
            else
            {
                GVMCpuProbeScopeDetail(
                    this,
                    SwapchainLogCategory,
                    "VKSwapchain::present.waitForAvailableFrameSlot",
                    "frame_slot={} previous_submission_id={} previous_image_index={} previous_present_sequence={} all_frame_contexts_busy={}",
                    oldestInFlightFrameSlot,
                    submissionDiagnosticId(frame.submissionCompletion),
                    frame.lastAcquiredImageIndex,
                    frame.lastPresentSequence,
                    true);
            }

            if (mQueue != nullptr)
            {
                if (directPresent)
                {
                    mQueue->waitForSubmission(frame.submissionCompletion, "swapchain_direct_frame_context_exhausted");
                }
                else
                {
                    mQueue->waitForSubmission(frame.submissionCompletion, "swapchain_present_frame_context_exhausted");
                }
            }
            frame.submissionCompletion.reset();
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
            "destroyed={} direct_present={} current_frame_slot={}",
            mDestroyed,
            mRenderDirectToAcquiredSwapchainImage,
            mCurrentFrameSlot);
        if (mDestroyed)
        {
            return {.texture = {}, .status = SwapchainNextTextureQueryStatus::Lost};
        }

        recreateSwapchainIfNeeded();
        if (mRenderDirectToAcquiredSwapchainImage)
        {
            return queryNextTextureDirect();
        }
        mCurrentResult.texture = mPresentTexture;
        mCurrentResult.status = SwapchainNextTextureQueryStatus::Success;
        return mCurrentResult;
    }

    void VKSwapchain::present()
    {
        GVMCpuProbeScopeDetail(
            this,
            SwapchainLogCategory,
            "VKSwapchain::present",
            "current_frame_slot={} direct_present={} swapchain_images={}",
            mCurrentFrameSlot,
            mRenderDirectToAcquiredSwapchainImage,
            mSwapchainImages.size());
        if (mDestroyed || !mSwapchain)
        {
            return;
        }

        if (mRenderDirectToAcquiredSwapchainImage)
        {
            presentDirectSwapchainImage();
            return;
        }

        if (mPresentTexture.isNull() || mPresentFrames.empty())
        {
            return;
        }

        mCurrentFrameSlot = acquirePresentFrameSlot(false);
        GVMLogTrace(
            this, SwapchainLogCategory,
            "event=swapchain_present_begin frame_slot={} last_submission_id={} swapchain_images={} present_texture_label={}",
            mCurrentFrameSlot,
            submissionDiagnosticId(mQueue != nullptr ? mQueue->getMostRecentSubmissionCompletion() : VKSubmissionCompletion{}),
            mSwapchainImages.size(),
            safeLogLabel(static_cast<VKTexture *>(mPresentTexture.get())->getLabelName()));
        PresentFrame &frame = mPresentFrames[mCurrentFrameSlot];

        const uint64_t presentSequence = mPresentSequence + 1u;
        const uint64_t acquireGeneration = frame.imageAvailableGeneration + 1u;
        const vk::ResultValue<uint32_t> acquireResult = [&]() {
            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::present.acquireNextImageKHR",
                "frame_slot={} acquire_semaphore={} acquire_generation={}",
                mCurrentFrameSlot,
                semaphoreDiagnosticHandle(frame.imageAvailableSemaphore.get()),
                acquireGeneration);
            return mDevice->getNativeDevice().acquireNextImageKHR(mSwapchain.get(), kHostGpuWaitTimeoutNanoseconds, frame.imageAvailableSemaphore.get(), nullptr);
        }();
        GVMLogDebug(
            this, SwapchainLogCategory,
            "event=swapchain_present_acquire_end frame_slot={} result={} image_index={}",
            mCurrentFrameSlot,
            static_cast<int32_t>(acquireResult.result),
            acquireResult.result == vk::Result::eSuccess || acquireResult.result == vk::Result::eSuboptimalKHR ? acquireResult.value : 0u);
        if (acquireResult.result == vk::Result::eTimeout)
        {
            GVMLogError(
                this,
                SwapchainLogCategory,
                "event=swapchain_present_acquire_timeout frame_slot={} timeout_ms={} acquire_semaphore={} acquire_generation={}",
                mCurrentFrameSlot,
                kHostGpuWaitTimeoutMilliseconds,
                semaphoreDiagnosticHandle(frame.imageAvailableSemaphore.get()),
                acquireGeneration);
            flushLogger(mLogger);
            throw makeRuntimeError("VKSwapchain::present timed out while acquiring a swapchain image.");
        }
        if (acquireResult.result == vk::Result::eErrorOutOfDateKHR)
        {
            GVMCpuProbeInstantDetail(this, SwapchainLogCategory, "swapchain.present.acquire_out_of_date", "frame_slot={}", mCurrentFrameSlot);
            GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_present_acquire_out_of_date frame_slot={}", mCurrentFrameSlot);
            recreateSwapchain(querySurfaceConfig());
            return;
        }
        if (acquireResult.result != vk::Result::eSuccess && acquireResult.result != vk::Result::eSuboptimalKHR)
        {
            throw makeRuntimeError("VKSwapchain::present failed to acquire a swapchain image.");
        }

        mCurrentImageIndex = acquireResult.value;
        if (mCurrentImageIndex >= mSwapchainImageStates.size())
        {
            throw makeRuntimeError("VKSwapchain::present acquired an out-of-range swapchain image index.");
        }
        mPresentSequence = presentSequence;
        frame.imageAvailableGeneration = acquireGeneration;
        frame.lastAcquiredImageIndex = mCurrentImageIndex;
        SwapchainImageState &imageState = mSwapchainImageStates[mCurrentImageIndex];
        GVMLogTrace(
            this, SwapchainLogCategory,
            "event=swapchain_present_acquire_binding frame_slot={} present_sequence={} image_index={} acquire_semaphore_handle={} acquire_generation={} render_complete_semaphore_handle={} render_complete_generation={} image_last_signal_submission_id={} image_last_present_submission_id={} image_last_acquire_generation={} image_last_acquire_frame_slot={}",
            mCurrentFrameSlot,
            presentSequence,
            mCurrentImageIndex,
            semaphoreDiagnosticHandle(frame.imageAvailableSemaphore.get()),
            frame.imageAvailableGeneration,
            semaphoreDiagnosticHandle(imageState.renderCompleteSemaphore.get()),
            imageState.renderCompleteGeneration,
            imageState.lastSignalSubmissionId,
            imageState.lastPresentedSubmissionId,
            imageState.lastAcquireGeneration,
            imageState.lastAcquireFrameSlot);

        {
            frame.commandBuffer.reset();

            vk::CommandBufferBeginInfo beginInfo = {};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            frame.commandBuffer.begin(beginInfo);
            recordPresentCopy(frame.commandBuffer, mCurrentImageIndex);
            frame.commandBuffer.end();
        }

        const vk::PipelineStageFlags waitStageMask = vk::PipelineStageFlagBits::eTransfer;
        const vk::Semaphore waitSemaphore = frame.imageAvailableSemaphore.get();
        const vk::Semaphore signalSemaphore = imageState.renderCompleteSemaphore.get();
        const uint64_t signalGeneration = imageState.renderCompleteGeneration + 1u;
        vk::SubmitInfo submitInfo = {};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSemaphore;
        submitInfo.pWaitDstStageMask = &waitStageMask;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &frame.commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSemaphore;

        // The queue submission completion token protects frame-slot resources such as the command
        // buffer and acquire semaphore. The render-complete semaphore is owned per swapchain
        // image so it is only reused after that image becomes acquirable again, which avoids
        // re-signaling a semaphore while presentation may still be waiting on it.
        frame.submissionCompletion = [&]() {
            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::present.submitPresentWork",
                "frame_slot={} present_sequence={} image_index={} wait_stage_mask_bits={} acquire_semaphore_handle={} acquire_generation={} signal_semaphore_handle={} signal_generation={}",
                mCurrentFrameSlot,
                presentSequence,
                mCurrentImageIndex,
                static_cast<uint32_t>(waitStageMask),
                semaphoreDiagnosticHandle(waitSemaphore),
                frame.imageAvailableGeneration,
                semaphoreDiagnosticHandle(signalSemaphore),
                signalGeneration);
            GVMLogTrace(
                this, SwapchainLogCategory,
                "event=swapchain_present_submit_plan frame_slot={} present_sequence={} image_index={} acquire_semaphore_handle={} acquire_generation={} wait_stage_mask_bits={} signal_semaphore_handle={} signal_generation={} previous_signal_submission_id={} previous_present_submission_id={}",
                mCurrentFrameSlot,
                presentSequence,
                mCurrentImageIndex,
                semaphoreDiagnosticHandle(waitSemaphore),
                frame.imageAvailableGeneration,
                static_cast<uint32_t>(waitStageMask),
                semaphoreDiagnosticHandle(signalSemaphore),
                signalGeneration,
                imageState.lastSignalSubmissionId,
                imageState.lastPresentedSubmissionId);
            return mQueue->submitPresentTracked(submitInfo);
        }();
        frame.lastSubmittedSubmissionId = submissionDiagnosticId(frame.submissionCompletion);
        frame.lastPresentSequence = presentSequence;
        imageState.renderCompleteGeneration = signalGeneration;
        imageState.lastSignalSubmissionId = frame.lastSubmittedSubmissionId;
        imageState.lastAcquireGeneration = frame.imageAvailableGeneration;
        imageState.lastAcquireFrameSlot = mCurrentFrameSlot;
        GVMLogDebug(
            this, SwapchainLogCategory,
            "event=swapchain_present_submit_end frame_slot={} present_sequence={} image_index={} submission_id={} acquire_semaphore_handle={} acquire_generation={} signal_semaphore_handle={} signal_generation={}",
            mCurrentFrameSlot,
            presentSequence,
            mCurrentImageIndex,
            frame.lastSubmittedSubmissionId,
            semaphoreDiagnosticHandle(waitSemaphore),
            frame.imageAvailableGeneration,
            semaphoreDiagnosticHandle(signalSemaphore),
            imageState.renderCompleteGeneration);
        const vk::SwapchainKHR swapchainHandle = mSwapchain.get();
        vk::PresentInfoKHR presentInfo = {};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &signalSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchainHandle;
        presentInfo.pImageIndices = &mCurrentImageIndex;

        vk::Result presentResult = vk::Result::eSuccess;
        presentResult = [&]()
        {
            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::present.presentKHR",
                "frame_slot={} present_sequence={} image_index={} submission_id={} wait_semaphore_handle={} wait_generation={}",
                mCurrentFrameSlot,
                presentSequence,
                mCurrentImageIndex,
                frame.lastSubmittedSubmissionId,
                semaphoreDiagnosticHandle(signalSemaphore),
                imageState.renderCompleteGeneration);
            GVMLogTrace(
                this, SwapchainLogCategory,
                "event=swapchain_present_queue_plan frame_slot={} present_sequence={} image_index={} wait_semaphore_handle={} wait_generation={} signal_submission_id={} acquire_semaphore_handle={} acquire_generation={}",
                mCurrentFrameSlot,
                presentSequence,
                mCurrentImageIndex,
                semaphoreDiagnosticHandle(signalSemaphore),
                imageState.renderCompleteGeneration,
                frame.lastSubmittedSubmissionId,
                semaphoreDiagnosticHandle(waitSemaphore),
                frame.imageAvailableGeneration);
            return mQueue->getNativeQueue().presentKHR(presentInfo);
        }();
        imageState.lastPresentedSubmissionId = frame.lastSubmittedSubmissionId;
        imageState.lastPresentSequence = presentSequence;
        GVMLogDebug(
            this, SwapchainLogCategory,
            "event=swapchain_present_present_end frame_slot={} present_sequence={} image_index={} result={} wait_semaphore_handle={} wait_generation={} signal_submission_id={}",
            mCurrentFrameSlot,
            presentSequence,
            mCurrentImageIndex,
            static_cast<int32_t>(presentResult),
            semaphoreDiagnosticHandle(signalSemaphore),
            imageState.renderCompleteGeneration,
            frame.lastSubmittedSubmissionId);
        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
        {
            recreateSwapchain(querySurfaceConfig());
        }
        else if (presentResult != vk::Result::eSuccess)
        {
            throw makeRuntimeError("VKSwapchain::present failed to queue presentation.");
        }

        GVMCpuProbeInstantDetail(
            this,
            SwapchainLogCategory,
            "swapchain.present.descriptor_churn",
            "present_sequence={} frame_slot={} image_index={} bind_groups_created={} bind_groups_destroyed={} descriptor_sets_allocated={} descriptor_sets_freed={}",
            presentSequence,
            mCurrentFrameSlot,
            mCurrentImageIndex,
            mDevice != nullptr ? mDevice->takeAndResetBindGroupCreateCountForDiagnostics() : 0u,
            mDevice != nullptr ? mDevice->takeAndResetBindGroupDestroyCountForDiagnostics() : 0u,
            mDevice != nullptr ? mDevice->takeAndResetDescriptorSetAllocateCountForDiagnostics() : 0u,
            mDevice != nullptr ? mDevice->takeAndResetDescriptorSetFreeCountForDiagnostics() : 0u);
        GVMLogTrace(
            this, SwapchainLogCategory,
            "event=swapchain_present_end presented_image_index={} next_frame_slot={} submission_id={}",
            mCurrentImageIndex,
            mNextFrameSlotCursor,
            submissionDiagnosticId(frame.submissionCompletion));
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
            mQueue->waitForSubmission(mQueue->getMostRecentSubmissionCompletion(), "swapchain_destroy");
        }
        waitForAllPresentFrames();

        destroyDirectSwapchainTextures();
        if (!mPresentTexture.isNull())
        {
            mDevice->freeTexture(mPresentTexture);
            mPresentTexture.reset();
        }

        mSwapchainImages.clear();
        mSwapchainImageStates.clear();
        mSwapchain.reset();
        mSurface.reset();
        mPresentFrames.clear();
        mPresentCommandPool.reset();
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
        const bool directPresentSupported =
            (capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment) == vk::ImageUsageFlagBits::eColorAttachment;
        mRenderDirectToAcquiredSwapchainImage = directPresentSupported;
        const bool directImageCopySrcSupported =
            directPresentSupported &&
            (capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc) == vk::ImageUsageFlagBits::eTransferSrc;
        const vk::ImageUsageFlagBits requiredUsage =
            mRenderDirectToAcquiredSwapchainImage
            ? vk::ImageUsageFlagBits::eColorAttachment
            : vk::ImageUsageFlagBits::eTransferDst;
        if ((capabilities.supportedUsageFlags & requiredUsage) != requiredUsage)
        {
            throw makeRuntimeError("VKSwapchain requires swapchain images with the requested Vulkan image-usage support.");
        }

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
            mQueue->waitForSubmission(mQueue->getMostRecentSubmissionCompletion(), "swapchain_recreate");
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
        createInfo.imageUsage =
            mRenderDirectToAcquiredSwapchainImage
            ? vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment)
            : vk::ImageUsageFlags(vk::ImageUsageFlagBits::eTransferDst);
        if (mRenderDirectToAcquiredSwapchainImage && config.directImageCopySrcSupported)
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
        const eastl::vector<vk::Image> swapchainImages = toEastlVector(mDevice->getNativeDevice().getSwapchainImagesKHR(newSwapchain.get()));

        mSwapchain = eastl::move(newSwapchain);
        mSwapchainImages.clear();
        mSwapchainImages.reserve(swapchainImages.size());
        for (vk::Image image : swapchainImages)
        {
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
            frame.submissionCompletion.reset();
            frame.imageAvailableGeneration = 0u;
            frame.lastSubmittedSubmissionId = 0u;
            frame.lastPresentSequence = 0u;
            frame.lastAcquiredImageIndex = InvalidSwapchainImageIndex;
        }

        if (mRenderDirectToAcquiredSwapchainImage)
        {
            if (!mPresentTexture.isNull())
            {
                mDevice->freeTexture(mPresentTexture);
                mPresentTexture.reset();
            }
            recreateDirectSwapchainTextures(config.format, config.extent, config.directImageCopySrcSupported);
            mCurrentResult.texture = {};
        }
        else
        {
            recreatePresentTexture(config.format, config.extent);
            mCurrentResult.texture = mPresentTexture;
        }
        mCurrentResult.status = SwapchainNextTextureQueryStatus::Success;
        GVMLogWarn(
            this, SwapchainLogCategory,
            "event=swapchain_recreate_end image_count={} extent={}x{} present_texture_label=\"{}\" direct_swapchain_images={}",
            mSwapchainImages.size(),
            mSwapchainExtent.width,
            mSwapchainExtent.height,
            mPresentTexture.isNull() ? "<null>" : safeLogLabel(static_cast<VKTexture *>(mPresentTexture.get())->getLabelName()),
            mDirectSwapchainTextures.size());
    }

    void VKSwapchain::recreatePresentTexture(TextureFormat format, const vk::Extent2D &extent)
    {
        if (!mPresentTexture.isNull())
        {
            auto *presentTexture = static_cast<VKTexture *>(mPresentTexture.get());
            if (presentTexture->getWidth() == extent.width &&
                presentTexture->getHeight() == extent.height &&
                presentTexture->getFormat() == format)
            {
                return;
            }

            mDevice->freeTexture(mPresentTexture);
            mPresentTexture.reset();
        }

        mPresentTexture = mDevice->createTexture({
            .label = "VulkanSwapchainPresentTexture",
            .usage = TextureUsage::TextureBinding |
                TextureUsage::RenderAttachment |
                TextureUsage::CopyDst |
                TextureUsage::CopySrc,
            .dimension = TextureDimension::e2D,
            .size = {extent.width, extent.height, 1u},
            .format = format,
            .mipLevelCount = 1u,
            .arrayLayerCount = 1u,
        });
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
            throw makeRuntimeError("VKSwapchain::createSurface on OHOS could not load vkCreateSurfaceOHOS.");
        }

        VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
        const VkResult result = createSurfaceOHOS(
            instance,
            &createInfo,
            nullptr,
            &rawSurface);
        if (result != VK_SUCCESS)
        {
            throw makeRuntimeError("VKSwapchain::createSurface on OHOS failed to create VK_OHOS_surface.");
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

        throw makeRuntimeError(eastl::string("VKSwapchain::createSurface received unsupported native surface kind ") + nativeSurfaceKindName(nativeSurface.kind) + " for this platform/build.");
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
            throw makeRuntimeError("VKSwapchain::queryNextTextureDirect requires at least one present frame slot.");
        }

        mCurrentFrameSlot = acquirePresentFrameSlot(true);
        PresentFrame &frame = mPresentFrames[mCurrentFrameSlot];

        const vk::ResultValue<uint32_t> acquireResult = [&]()
        {
            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::queryNextTextureDirect.acquireNextImageKHR",
                "frame_slot={} acquire_semaphore={} acquire_generation={}",
                mCurrentFrameSlot,
                semaphoreDiagnosticHandle(frame.imageAvailableSemaphore.get()),
                frame.imageAvailableGeneration + 1u);
            return mDevice->getNativeDevice().acquireNextImageKHR(
                mSwapchain.get(),
                kHostGpuWaitTimeoutNanoseconds,
                frame.imageAvailableSemaphore.get(),
                nullptr);
        }();

        if (acquireResult.result == vk::Result::eErrorOutOfDateKHR)
        {
            recreateSwapchain(querySurfaceConfig());
            mCurrentResult = {.texture = {}, .status = SwapchainNextTextureQueryStatus::Outdated};
            return mCurrentResult;
        }

        if (acquireResult.result != vk::Result::eSuccess && acquireResult.result != vk::Result::eSuboptimalKHR)
        {
            mCurrentResult = {.texture = {}, .status = translateAcquireStatus(acquireResult.result)};
            return mCurrentResult;
        }

        mCurrentImageIndex = acquireResult.value;
        if (mCurrentImageIndex >= mSwapchainImageStates.size() || mCurrentImageIndex >= mDirectSwapchainTextures.size())
        {
            throw makeRuntimeError("VKSwapchain::queryNextTextureDirect acquired an out-of-range swapchain image index.");
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
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
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
            throw makeRuntimeError("VKSwapchain::presentDirectSwapchainImage requires at least one present frame slot.");
        }

        auto *directTexture = static_cast<VKTexture *>(mCurrentResult.texture.get());
        VKPresentManager &presentManager = mDevice->getPresentManager();
        if (directTexture == nullptr || !presentManager.isDirectPresentTexture(directTexture))
        {
            throw makeRuntimeError("VKSwapchain::presentDirectSwapchainImage requires a direct-present swapchain texture.");
        }
        if (!presentManager.isDirectPresentSubmissionArmed(directTexture))
        {
            VKPresentManager::DirectPresentSubmitSync submitSync = {};
            if (!presentManager.tryGetDirectPresentSubmitSync(directTexture, submitSync))
            {
                throw makeRuntimeError("VKSwapchain::presentDirectSwapchainImage cannot arm presentation because the acquired swapchain image has no pending acquire semaphore.");
            }

            const vk::PipelineStageFlags waitStageMask =
                submitSync.acquireStageMask == vk::PipelineStageFlags{}
                ? vk::PipelineStageFlagBits::eColorAttachmentOutput
                : submitSync.acquireStageMask;
            const vk::Semaphore acquireSemaphore = submitSync.acquireSemaphore;
            const vk::Semaphore pendingRenderCompleteSemaphore = submitSync.renderCompleteSemaphore;
            if (pendingRenderCompleteSemaphore == vk::Semaphore{})
            {
                throw makeRuntimeError("VKSwapchain::presentDirectSwapchainImage cannot arm presentation because the acquired swapchain image has no render-complete semaphore.");
            }

            vk::SubmitInfo armSubmitInfo = {};
            armSubmitInfo.waitSemaphoreCount = 1u;
            armSubmitInfo.pWaitSemaphores = &acquireSemaphore;
            armSubmitInfo.pWaitDstStageMask = &waitStageMask;
            armSubmitInfo.signalSemaphoreCount = 1u;
            armSubmitInfo.pSignalSemaphores = &pendingRenderCompleteSemaphore;

            GVMCpuProbeScopeDetail(
                this,
                SwapchainLogCategory,
                "VKSwapchain::presentDirectSwapchainImage.armEmptySubmit",
                "frame_slot={} image_index={} acquire_semaphore={} signal_semaphore={} wait_stage_mask_bits={}",
                mCurrentFrameSlot,
                mCurrentImageIndex,
                semaphoreDiagnosticHandle(acquireSemaphore),
                semaphoreDiagnosticHandle(pendingRenderCompleteSemaphore),
                static_cast<uint32_t>(waitStageMask));
            GVMLogWarn(
                this, SwapchainLogCategory,
                "event=swapchain_present_direct_arm_empty_submit frame_slot={} image_index={} acquire_semaphore={} signal_semaphore={} wait_stage_mask_bits={}",
                mCurrentFrameSlot,
                mCurrentImageIndex,
                semaphoreDiagnosticHandle(acquireSemaphore),
                semaphoreDiagnosticHandle(pendingRenderCompleteSemaphore),
                static_cast<uint32_t>(waitStageMask));

            mPresentFrames[mCurrentFrameSlot].submissionCompletion = mQueue->submitTracked(armSubmitInfo, true);
            presentManager.setDirectPresentSubmissionArmed(directTexture);
        }

        PresentFrame &frame = mPresentFrames[mCurrentFrameSlot];
        if (!frame.submissionCompletion)
        {
            frame.submissionCompletion = mQueue->getMostRecentSubmissionCompletion();
        }
        frame.lastSubmittedSubmissionId = submissionDiagnosticId(frame.submissionCompletion);
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
            return mQueue->getNativeQueue().presentKHR(presentInfo);
        }();
        mHasOutstandingDirectPresentImage = false;
        presentManager.setDirectPresentPresented(directTexture);

        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
        {
            recreateSwapchain(querySurfaceConfig());
            mCurrentResult = {.texture = {}, .status = SwapchainNextTextureQueryStatus::Outdated};
            return;
        }
        if (presentResult != vk::Result::eSuccess)
        {
            throw makeRuntimeError("VKSwapchain::presentDirectSwapchainImage failed to queue presentation.");
        }

        imageState.layout = vk::ImageLayout::ePresentSrcKHR;
        imageState.stageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
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
        GVMCpuProbeInstantDetail(
            this,
            SwapchainLogCategory,
            "swapchain.direct.descriptor_churn",
            "present_sequence={} frame_slot={} image_index={} bind_groups_created={} bind_groups_destroyed={} descriptor_sets_allocated={} descriptor_sets_freed={}",
            mPresentSequence,
            mCurrentFrameSlot,
            mCurrentImageIndex,
            mDevice != nullptr ? mDevice->takeAndResetBindGroupCreateCountForDiagnostics() : 0u,
            mDevice != nullptr ? mDevice->takeAndResetBindGroupDestroyCountForDiagnostics() : 0u,
            mDevice != nullptr ? mDevice->takeAndResetDescriptorSetAllocateCountForDiagnostics() : 0u,
            mDevice != nullptr ? mDevice->takeAndResetDescriptorSetFreeCountForDiagnostics() : 0u);
    }

    void VKSwapchain::recordPresentCopy(vk::CommandBuffer commandBuffer, uint32_t imageIndex)
    {
        auto *presentTexture = static_cast<VKTexture *>(mPresentTexture.get());
        if (presentTexture == nullptr)
        {
            throw makeRuntimeError("VKSwapchain::recordPresentCopy requires a valid present texture.");
        }

        transitionExternalTexture(
            commandBuffer,
            *mQueue,
            *presentTexture,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eTransferRead);
        SwapchainImageState &swapchainImageState = mSwapchainImageStates[imageIndex];
        const vk::ImageSubresourceRange swapchainSubresourceRange = {
            vk::ImageAspectFlagBits::eColor,
            0u,
            1u,
            0u,
            1u,
        };
        VKTaskDependencyResolver::transitionImageState(
            commandBuffer,
            mSwapchainImages[imageIndex],
            swapchainSubresourceRange,
            swapchainImageState.layout,
            swapchainImageState.stageMask,
            swapchainImageState.accessMask,
            vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eTransferWrite);

        const vk::ImageCopy copyRegion = {
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u},
            vk::Offset3D{0, 0, 0},
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u},
            vk::Offset3D{0, 0, 0},
            vk::Extent3D{mSwapchainExtent.width, mSwapchainExtent.height, 1u},
        };
        commandBuffer.copyImage(
            presentTexture->getNativeImage(),
            vk::ImageLayout::eTransferSrcOptimal,
            mSwapchainImages[imageIndex],
            vk::ImageLayout::eTransferDstOptimal,
            vk::ArrayProxy<const vk::ImageCopy>(copyRegion));

        transitionExternalTexture(
            commandBuffer,
            *mQueue,
            *presentTexture,
            presentTexture->getSteadyStateLayout(),
            resolveStageMaskForLayout(presentTexture->getSteadyStateLayout()),
            resolveAccessMaskForLayout(presentTexture->getSteadyStateLayout()));
        VKTaskDependencyResolver::transitionImageState(
            commandBuffer,
            mSwapchainImages[imageIndex],
            swapchainSubresourceRange,
            swapchainImageState.layout,
            swapchainImageState.stageMask,
            swapchainImageState.accessMask,
            vk::ImageLayout::ePresentSrcKHR,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            {});
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
            if (frame.submissionCompletion)
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
                submissionDiagnosticId(mQueue->getMostRecentSubmissionCompletion()));
            mQueue->waitForSubmission(mQueue->getMostRecentSubmissionCompletion(), "swapchain_wait_all_present_frames");
            GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_wait_all_present_frames_submission_wait_end");
        }
        if (mQueue != nullptr)
        {
            // Vulkan core does not provide a host-visible fence for "present retired".
            // Draining the present queue is the conservative teardown/recreate point.
            const auto waitIdleStartTime = std::chrono::steady_clock::now();
            GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_wait_all_present_frames_queue_idle_begin");
            (void)mQueue->getNativeQueue().waitIdle();
            GVMLogWarn(
                this, SwapchainLogCategory,
                "event=swapchain_wait_all_present_frames_queue_idle_end duration_ms={:.3f}",
                durationMilliseconds(waitIdleStartTime));
        }
        for (PresentFrame &frame : mPresentFrames)
        {
            frame.submissionCompletion.reset();
        }
        GVMLogWarn(this, SwapchainLogCategory, "event=swapchain_wait_all_present_frames_end");
    }
} // namespace GVM::RHI::Vulkan
