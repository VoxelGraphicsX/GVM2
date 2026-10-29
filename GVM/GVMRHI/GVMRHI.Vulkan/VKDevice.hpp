#pragma once

#include "VKCommon.hpp"
#include "VKPipelineCache.hpp"
#include "VKDeviceCapabilities.hpp"
#include "VKDescriptorChurnCounters.hpp"
#include "VKArgumentResources.hpp"
#include <GVMRHI/Private/DeviceLostState.hpp>
#include "VKArguments.hpp"
#include "VKDescriptorPoolStore.hpp"
#include "VKDefines.hpp"
#include "VKPresentManager.hpp"
#include "VKRenderPassFramebufferCache.hpp"
#include "VKResourceRegistry.hpp"
#include <GVMRHI/Private/FullscreenTextureExecutor.hpp>

#include <GVMRHI/Private/DeviceObjectCache.hpp>
#include "Private/GVMRHIDefines.hpp"

#include <EASTL/atomic.h>
#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <cstdint>
#include <EASTL/shared_ptr.h>

namespace GVM::RHI::Vulkan
{
    class VKDevice final : public DeviceImpl
    {
    public:
        VKDevice() = default;

        void init(VKInstance *instance);

        Queue getMainQueue() const override;
        TimestampQuerySupport getTimestampQuerySupport() const override;
        PassCounterQuerySupport getPassCounterQuerySupport() const override;
        /// Returns Vulkan memory topology inferred from the selected physical device memory types.
        DeviceMemoryProperties getMemoryProperties() const override;
        /** Returns the current per-heap budget and usage; reportedByDriver reflects VK_EXT_memory_budget. */
        DeviceMemoryBudget getMemoryBudget() const override;
        /// Returns the diagnostics overlay configuration captured from the owning Vulkan instance.
        RuntimeDiagnosticsOverlayConfig getDiagnosticsOverlayConfig() const override;
        /// Returns a snapshot of live Vulkan buffers and textures for runtime diagnostics display.
        DiagnosticsResourceSnapshot getDiagnosticsResourceSnapshot() const override;
        /// Returns the VkPipelineCache contents with the GVM header for the caller to persist.
        eastl::vector<uint8_t> getPipelineCacheData() const override;
        /// Returns the native pipeline cache every Vulkan pipeline creation call must use.
        [[nodiscard]] vk::PipelineCache getNativePipelineCache() const;
        Buffer createBuffer(const BufferDescriptor &descriptor) override;
        QuerySet createQuerySet(const QuerySetDescriptor &descriptor) override;
        Texture createTexture(const TextureDescriptor &descriptor) override;
        Texture createSwapchainImageTexture(const TextureDescriptor &descriptor, vk::Image image);
        ComputePipeline createComputePipeline(const ComputePipelineDescriptor &descriptor) override;
        RenderPipeline createRenderPipeline(const RenderPipelineDescriptor &descriptor) override;
        ShaderModule createShaderModule(const ShaderModuleDescriptor &descriptor) override;
        BindGroupLayout createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor) override;
        PipelineLayout createPipelineLayout(const PipelineLayoutDescriptor &descriptor) override;
        BindGroup createBindGroup(const BindGroupDescriptor &descriptor) override;
        /** Creates an immutable Args snapshot, retaining its reachable native allocations immediately. */
        Arguments createArguments(const ArgumentsDescriptor &descriptor) override;
        /** Sets the parameter-page byte limit; nonzero; may change at any time and applies to later page allocations. */
        void setArgumentMemoryBudget(uint64_t bytes) override;
        /** Returns current Args snapshot and parameter-page usage. */
        ArgumentMemoryStatistics getArgumentMemoryStatistics() const override { return mArgumentResources.statistics(); }
        /** Returns the owner of the Args page pool and shared descriptor table. */
        [[nodiscard]] VKArgumentResources &getArgumentResources();
        Sampler createSampler(const SamplerDescriptor &descriptor) override;

        void freeBuffer(Buffer buffer) override;
        void freeTexture(Texture texture) override;
        void freeSampler(Sampler sampler) override;
        Logger getLogger() const override;
        void destroy() override;

        [[nodiscard]] vk::Device getNativeDevice() const;
        /** Names a native Vulkan object for debuggers when VK_EXT_debug_utils is enabled; a no-op otherwise. */
        void setDebugObjectName(vk::ObjectType type, uint64_t handle, const char *name) const;
        [[nodiscard]] vk::PhysicalDevice getPhysicalDevice() const;
        /** Returns the device capability survey collected during initialization. */
        [[nodiscard]] const VKDeviceCapabilities &getCapabilities() const { return mCapabilities; }
        [[nodiscard]] VKInstance *getInstance() const;

        const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;

        [[nodiscard]] VmaAllocator getAllocator() const;
        [[nodiscard]] VKQueue *getMainQueueImpl() const;
        /** Returns the registry that owns resource pools, live tracking, and retire draining. */
        [[nodiscard]] VKResourceRegistry &getResourceRegistry();
        /** Throws std::runtime_error naming apiName when the device has been destroyed. */
        /** Throws std::runtime_error when the device is destroyed or has no native device. */
        void ensureAlive(const char *apiName) const;
        /** Throws DeviceLostError when the device is lost, or std::runtime_error when destroyed. */
        void ensureUsable(const char *apiName) const;
        /** Returns the shared device-lost state reached by queues, swapchains, and readbacks. */
        [[nodiscard]] DeviceLostState &getLostState();
        /** Returns the recorded device loss; empty while the device is healthy. */
        eastl::optional<DeviceLostInfo> getLostInfo() const override;
        [[nodiscard]] VKPresentManager &getPresentManager();
        [[nodiscard]] const VKPresentManager &getPresentManager() const;
        /** Returns the shared device-object cache so queues can release unreferenced entries after submit. */
        [[nodiscard]] Private::DeviceObjectCache &getObjectCache() { return mObjectCache; }

        vk::DescriptorSet allocateDescriptorSet(
            vk::DescriptorSetLayout layout,
            const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet,
            vk::DescriptorPoolCreateFlags poolFlags,
            vk::DescriptorPool &owningPool);
        void freeDescriptorSet(vk::DescriptorPool pool, vk::DescriptorSet descriptorSet);
        /** Returns the render-pass/framebuffer cache that owns its own lock. */
        [[nodiscard]] VKRenderPassFramebufferCache &getRenderPassFramebufferCache();
        using FramebufferOwner = VKRenderPassFramebufferCache::FramebufferOwner;
        using FramebufferHandle = VKRenderPassFramebufferCache::FramebufferHandle;

        /** Returns the shared descriptor-churn counters drained by the swapchain descriptor-churn CPU probe. */
        VKDescriptorChurnCounters &getDescriptorChurnCounters();

        [[nodiscard]] Private::FullscreenTextureExecutor &getFullscreenTextureExecutor();

    private:
        /** Surveys instance physical devices and records the selected device's capabilities. */
        void selectAndSurveyPhysicalDevice();
        /** Builds the feature chain, creates the logical device, and loads device-level function pointers. */
        void createNativeDevice();
        /** Creates the VMA allocator used by every device allocation. */
        void createVulkanMemoryAllocator();
        /** Initializes the resource pools and device-side caches. */
        void initResourcePoolsAndCaches();
        /** Creates the main queue and, when enabled, the diagnostics overlay queue. */
        void createMainQueueAndOverlay();
        /** Creates the internal fullscreen-texture pipeline used by swapchain presentation. */
        void initFullscreenTextureExecutor();
        /** Emits the device_init_end capability summary log. */
        void logDeviceInitEnd();
        [[nodiscard]]
        uint64_t getDescriptorPoolReuseRetiredSubmissionId() const;

        VKInstance *mInstance = nullptr;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        Logger mLogger;
        VKDeviceCapabilities mCapabilities = {};
        vk::UniqueDevice mDevice;
        VmaAllocator mAllocator = nullptr;
        VKArgumentResources mArgumentResources{*this};
        VKQueue *mMainQueue = nullptr;
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
        Queue mDiagnosticsOverlayQueue = nullptr;
#endif
        Private::FullscreenTextureExecutor mFullscreenTextureExecutor;
        VKPresentManager mPresentManager;
        VKResourceRegistry mResourceRegistry{*this};
        DeviceLostState mLostState;
        mutable Mutex mDescriptorPoolMutex;
        VKDescriptorPoolStore mDescriptorPoolStore;
        VKPipelineCache mPipelineCache;
        VKRenderPassFramebufferCache mRenderPassFramebufferCache;
        Private::DeviceObjectCache mObjectCache;
        RuntimeDiagnosticsOverlayConfig mDiagnosticsOverlayConfig = {};
        VKDescriptorChurnCounters mDescriptorChurnCounters = {};
    };
} // namespace GVM::RHI::Vulkan
