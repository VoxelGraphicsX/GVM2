#pragma once

#include "VKCommon.hpp"
#include "VKDescriptorPoolStore.hpp"
#include "VKDefines.hpp"
#include "VKHashBucketCache.hpp"
#include "VKPresentManager.hpp"
#include "VKRenderPassFramebufferCache.hpp"
#include "VKRenderToSwapchainExecutor.hpp"
#include "VKRetireManager.hpp"

#include "Private/GVMRHIDefines.hpp"

#include <atomic>
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
        /// Returns the diagnostics overlay configuration captured from the owning Vulkan instance.
        RuntimeDiagnosticsOverlayConfig getDiagnosticsOverlayConfig() const override;
        /// Returns a snapshot of live Vulkan buffers and textures for runtime diagnostics display.
        DiagnosticsResourceSnapshot getDiagnosticsResourceSnapshot() const override;
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
        Sampler createSampler(const SamplerDescriptor &descriptor) override;

        void freeBuffer(Buffer buffer) override;
        void freeTexture(Texture texture) override;
        void freeSampler(Sampler sampler) override;
        Logger getLogger() const override;
        void destroy() override;

        [[nodiscard]] vk::Device getNativeDevice() const;
        [[nodiscard]] vk::PhysicalDevice getPhysicalDevice() const;
        [[nodiscard]] VKInstance *getInstance() const;

        const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;

        [[nodiscard]] VmaAllocator getAllocator() const;
        [[nodiscard]] VKQueue *getMainQueueImpl() const;
        [[nodiscard]] bool supportsSwapchain() const;
        [[nodiscard]] TextureViewPool &getTextureViewPool();
        [[nodiscard]] VKPresentManager &getPresentManager();
        [[nodiscard]] const VKPresentManager &getPresentManager() const;

        vk::DescriptorSet allocateDescriptorSet(
            vk::DescriptorSetLayout layout,
            const eastl::vector<vk::DescriptorPoolSize> &poolSizesPerSet,
            vk::DescriptorPool &owningPool);
        void freeDescriptorSet(vk::DescriptorPool pool, vk::DescriptorSet descriptorSet);
        vk::RenderPass getOrCreateRenderPass(const RenderPassDescriptor &descriptor);
        vk::RenderPass getOrCreatePipelineRenderPass(const RenderPipelineDescriptor &descriptor);
        using FramebufferOwner = VKRenderPassFramebufferCache::FramebufferOwner;
        using FramebufferHandle = VKRenderPassFramebufferCache::FramebufferHandle;

        FramebufferHandle getOrCreateFramebuffer(
            vk::RenderPass renderPass,
            const eastl::vector<vk::ImageView> &attachments,
            uint32_t width,
            uint32_t height,
            uint32_t layers);

        void collectReleasedResources();
        void destroyTextureViewHandle(TextureView view);
        void noteBindGroupDestroyed();
        void retainBindGroupBufferReference(Buffer buffer);
        void retainBindGroupTextureReference(VKTexture *texture);
        void retainBindGroupSamplerReference(Sampler sampler);
        void enqueueBindGroupReferenceReleases(
            eastl::vector<VKBuffer *> buffers,
            eastl::vector<VKTexture *> textures,
            eastl::vector<VKSampler *> samplers);
        [[nodiscard]] uint64_t takeAndResetBindGroupCreateCountForDiagnostics();
        [[nodiscard]] uint64_t takeAndResetBindGroupDestroyCountForDiagnostics();
        [[nodiscard]] uint64_t takeAndResetDescriptorSetAllocateCountForDiagnostics();
        [[nodiscard]] uint64_t takeAndResetDescriptorSetFreeCountForDiagnostics();

        [[nodiscard]]
        size_t getFramebufferCacheSizeForTesting() const;

        [[nodiscard]]
        size_t getRenderPassCacheSizeForTesting() const;

        [[nodiscard]]
        size_t getPipelineCompatibleRenderPassCacheSizeForTesting() const;

        [[nodiscard]]
        size_t getPendingDestroyBufferCountForTesting() const;

        [[nodiscard]]
        uint32_t getBufferTotalReferenceCountForTesting(Buffer buffer) const;

        [[nodiscard]]
        uint32_t getBufferBindGroupReferenceCountForTesting(Buffer buffer) const;

        [[nodiscard]]
        bool isBufferHandleDestroyedForTesting(Buffer buffer) const;

        VKRenderToSwapchainExecutor getRenderToSwapchainExecutor() const;

        void retainPendingCommandBufferReference(Buffer buffer);
        void retainPendingCommandTextureReference(Texture texture);
        void releasePendingCommandBufferReference(Buffer buffer);
        void releasePendingCommandTextureReference(Texture texture);

        [[nodiscard]]
        Texture findTextureHandle(const VKTexture *texture) const;

        [[nodiscard]]
        Texture findTextureHandle(const VKTexture &texture) const;

        [[nodiscard]] bool supportsSamplerAnisotropy() const;
        [[nodiscard]] float getMaxSamplerAnisotropy() const;
        [[nodiscard]] bool supportsVertexPipelineStoresAndAtomics() const;
        [[nodiscard]] bool supportsFragmentShaderBarycentric() const;
        [[nodiscard]] bool supportsTessellationShaderFeature() const;
        [[nodiscard]] bool supportsShaderFloat16() const;
        /** Returns whether extended scalar types were enabled for subgroup instructions. */
        [[nodiscard]] bool supportsShaderSubgroupExtendedTypes() const { return mSupportsShaderSubgroupExtendedTypes; }
        /** Returns the independent 16-bit storage features enabled on this logical device. */
        [[nodiscard]] const vk::PhysicalDevice16BitStorageFeatures &getEnabled16BitStorageFeatures() const;
        /** Returns the core features actually enabled when the logical device was created. */
        [[nodiscard]] const vk::PhysicalDeviceFeatures &getEnabledShaderFeatures() const;
        /** Returns subgroup operations and stages reported by the selected physical device. */
        [[nodiscard]] const vk::PhysicalDeviceSubgroupProperties &getSubgroupProperties() const;
        [[nodiscard]] bool supportsMultiDrawIndirect() const;

    private:
        void ensureAlive(const char *apiName) const;
        void noteBindGroupCreated();
        void noteDescriptorSetAllocated();
        void noteDescriptorSetFreed();
        void trackBuffer(Buffer buffer);
        void trackTexture(Texture texture);
        void trackSampler(Sampler sampler);
        void releaseBindGroupBufferReference(VKBuffer *buffer);
        void releaseBindGroupTextureReference(VKTexture *texture);
        void releaseBindGroupSamplerReference(VKSampler *sampler);
        void invalidateFramebufferCacheForImageView(vk::ImageView imageView);
        void releaseBufferHandle(Buffer buffer);
        void releaseTextureHandle(Texture texture);
        void releaseSamplerHandle(Sampler sampler);
        [[nodiscard]]
        uint64_t getDescriptorPoolReuseRetiredSubmissionId() const;

        VKInstance *mInstance = nullptr;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        Logger mLogger;
        vk::PhysicalDevice mPhysicalDevice;
        vk::UniqueDevice mDevice;
        VmaAllocator mAllocator = nullptr;
        uint32_t mGraphicsQueueFamilyIndex = 0;
        VKQueue *mMainQueue = nullptr;
        Queue mDiagnosticsOverlayQueue = nullptr;
        BufferPool mBufferPool;
        TexturePool mTexturePool;
        TextureViewPool mTextureViewPool;
        VKRenderToSwapchainExecutor mRenderToSwapchainExecutor = nullptr;
        SamplerPool mSamplerPool;
        eastl::vector<Buffer> mLiveBuffers;
        eastl::vector<Texture> mLiveTextures;
        eastl::vector<Sampler> mLiveSamplers;
        VKPresentManager mPresentManager;
        VKRetireManager mRetireManager;
        mutable Mutex mCacheMutex;
        mutable Mutex mResourceLifetimeMutex;
        VKDescriptorPoolStore mDescriptorPoolStore;
        VKRenderPassFramebufferCache mRenderPassFramebufferCache;
        CacheDetail::VKHashBucketCache<ShaderModule> mShaderModuleCache;
        CacheDetail::VKHashBucketCache<BindGroupLayout> mBindGroupLayoutCache;
        CacheDetail::VKHashBucketCache<PipelineLayout> mPipelineLayoutCache;
        CacheDetail::VKHashBucketCache<ComputePipeline> mComputePipelineCache;
        CacheDetail::VKHashBucketCache<RenderPipeline> mRenderPipelineCache;
        bool mDestroyed = false;
        bool mSupportsSwapchain = false;
        bool mSupportsSamplerAnisotropy = false;
        bool mSupportsMultiDrawIndirect = false;
        bool mSupportsVertexPipelineStoresAndAtomics = false;
        bool mSupportsFragmentShaderBarycentric = false;
        bool mSupportsTessellationShaderFeature = false;
        bool mSupportsShaderFloat16 = false;
        bool mSupportsShaderSubgroupExtendedTypes = false;
        vk::PhysicalDevice16BitStorageFeatures mEnabled16BitStorageFeatures = {};
        vk::PhysicalDeviceFeatures mEnabledShaderFeatures = {};
        vk::PhysicalDeviceSubgroupProperties mSubgroupProperties = {};
        TimestampQuerySupport mTimestampQuerySupport = {};
        PassCounterQuerySupport mPassCounterQuerySupport = {};
        DeviceMemoryProperties mMemoryProperties = {};
        RuntimeDiagnosticsOverlayConfig mDiagnosticsOverlayConfig = {};
        float mMaxSamplerAnisotropy = 1.0f;
        std::atomic<uint64_t> mBindGroupCreateCount{0u};
        std::atomic<uint64_t> mBindGroupDestroyCount{0u};
        std::atomic<uint64_t> mDescriptorSetAllocateCount{0u};
        std::atomic<uint64_t> mDescriptorSetFreeCount{0u};
    };
} // namespace GVM::RHI::Vulkan
