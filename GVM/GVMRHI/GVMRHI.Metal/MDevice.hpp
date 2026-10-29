#pragma once
#include "MDefines.hpp"
#include "MArguments.hpp"
#include <GVMRHI/Private/DeviceLostState.hpp>
#include <GVMRHI/Private/DeviceObjectCache.hpp>
#include <GVMRHI/Private/FullscreenTextureExecutor.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include <Utils/ResourcePool.hpp>
#include <dispatch/dispatch.h>

#include "MUtilShaders/MIndirectBufferConvertShaderExecutor.hpp"
namespace GVM::RHI::Metal
{

    class MDevice final : public DeviceImpl
    {
    public:
        MDevice();
        void init(MInstance *instance);
        virtual Queue getMainQueue() const override;
        virtual TimestampQuerySupport getTimestampQuerySupport() const override;
        virtual PassCounterQuerySupport getPassCounterQuerySupport() const override;
        /// Returns Metal device memory topology as reported by the selected native device.
        virtual DeviceMemoryProperties getMemoryProperties() const override;
        /** Wraps a Metal texture owned by another object as a GVM texture handle; ownership is not
            taken — the caller must keep the native texture alive until the handle is released. */
        Texture wrapNativeTexture(MTL::Texture &nativeTexture, TextureUsageFlags usage, const eastl::string &label);
        /** Returns the current per-heap budget and usage (one heap: the Metal working set). */
        DeviceMemoryBudget getMemoryBudget() const override;
        /// Returns the diagnostics overlay configuration captured from the owning Metal instance.
        virtual RuntimeDiagnosticsOverlayConfig getDiagnosticsOverlayConfig() const override;
        /// Returns a snapshot of live Metal buffers and textures for runtime diagnostics display.
        virtual DiagnosticsResourceSnapshot getDiagnosticsResourceSnapshot() const override;
        /// Returns empty data: Metal pipeline state compilation relies on the system shader cache and is not persisted by GVM.
        eastl::vector<uint8_t> getPipelineCacheData() const override;
        virtual Buffer createBuffer(const BufferDescriptor &descriptor) override;
        virtual QuerySet createQuerySet(const QuerySetDescriptor &descriptor) override;
        virtual Texture createTexture(const TextureDescriptor &descriptor) override;
        virtual ShaderModule createShaderModule(const ShaderModuleDescriptor &descriptor) override;
        virtual ComputePipeline createComputePipeline(const ComputePipelineDescriptor &descriptor) override;
        virtual RenderPipeline createRenderPipeline(const RenderPipelineDescriptor &descriptor) override;
        virtual BindGroupLayout createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor) override;
        virtual PipelineLayout createPipelineLayout(const PipelineLayoutDescriptor &descriptor) override;
        virtual BindGroup createBindGroup(const BindGroupDescriptor &descriptor) override;
        /** Captures Args values and native buffer allocations when an execution task is created. */
        Arguments createArguments(const ArgumentsDescriptor &descriptor) override;
        /** Sets the parameter-page byte limit; nonzero; may change at any time and applies to later page allocations. */
        void setArgumentMemoryBudget(uint64_t bytes) override;
        /** Returns current Args snapshot and parameter-page usage. */
        ArgumentMemoryStatistics getArgumentMemoryStatistics() const override { return mArgumentPagePool->statistics(); }
        /** Returns the shared page pool retained by snapshots and submission storages. */
        const eastl::intrusive_ptr<MArgumentPagePool> &getArgumentPagePool() const { return mArgumentPagePool; }
        virtual Sampler createSampler(const SamplerDescriptor &descriptor) override;

        virtual void freeBuffer(Buffer buffer) override;
        virtual void freeTexture(Texture texture) override;
        virtual void freeSampler(Sampler sampler) override;
        Logger getLogger() const override;
        virtual void destroy() override;
        BufferPool mBufferPool;
        TexturePool mTexturePool;
        TextureViewPool mTextureViewPool;
        SamplerPool mSamplerPool;
        const dispatch_semaphore_t &getFrameSemaphore() const;

        static void activateGCPool()
        {
            mMainPool = NS::AutoreleasePool::alloc()->init();
        }
        static void releaseGCPool()
        {
            if (mMainPool != nullptr)
            {
                mMainPool->release();
                mMainPool = nullptr;
            }
        }

        MTL::Device *getNativeDevice() const;
        /** Returns the argument-buffer tier cached when the Metal device was initialized. */
        MTL::ArgumentBuffersTier getArgumentBuffersTier() const { return mArgumentBuffersTier; }
        /** Returns the cached maximum command range accepted by native GPU-count ICB execution. */
        uint32_t getMaxIndirectCountDraws() const { return mMaxIndirectCountDraws; }
        /// Returns the raw Metal main queue for backend-internal paths that must not observe wrapper queues.
        MQueue *getMainQueueImpl() const;
        uint64_t getFrameIndex() const;
        void beginFrame();
        void endFrame();
        static constexpr uint32_t MAX_FRAME_COUNT = 3;
        MIndirectBufferConvertShaderExecutor getIndirectIndexedRenderCommandConvertShaderExecutor() const;
        [[nodiscard]] Private::FullscreenTextureExecutor &getFullscreenTextureExecutor();
        /** Throws DeviceLostError when the device is lost, or std::runtime_error when destroyed. */
        void ensureUsable(const char *apiName) const;
        /** Returns the shared device-lost state reached by queues, swapchains, and readbacks. */
        [[nodiscard]] DeviceLostState &getLostState();
        eastl::optional<DeviceLostInfo> getLostInfo() const override;
        /** Returns the shared device-object cache so queues can release unreferenced entries after submit. */
        [[nodiscard]] Private::DeviceObjectCache &getObjectCache() { return mObjectCache; }

    private:
        uint32_t mMaxIndirectCountDraws = UINT32_MAX;
        eastl::intrusive_ptr<MArgumentPagePool> mArgumentPagePool;
        void ensureResourcePoolsInitialized(const char *apiName) const;
        void trackBuffer(Buffer buffer);
        void trackTexture(Texture texture);
        void trackSampler(Sampler sampler);
        void untrackBuffer(Buffer buffer);
        void untrackTexture(Texture texture);
        void untrackSampler(Sampler sampler);

        MInstance *mInstance = nullptr;
        MTL::Device *mNativeDevice = nullptr;
        MTL::ArgumentBuffersTier mArgumentBuffersTier = MTL::ArgumentBuffersTier1;
        MTL::CounterSet *mTimestampCounterSet = nullptr;
        MTL::CounterSet *mStageUtilizationCounterSet = nullptr;
        MTL::CounterSet *mStatisticCounterSet = nullptr;
        TimestampQuerySupport mTimestampQuerySupport = {};
        PassCounterQuerySupport mPassCounterQuerySupport = {};
        DeviceMemoryProperties mMemoryProperties = {};
        RuntimeDiagnosticsOverlayConfig mDiagnosticsOverlayConfig = {};
        MQueue *mMainQueue = nullptr;
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
        Queue mDiagnosticsOverlayQueue = nullptr;
#endif
        Logger mLogger;
        uint64_t mFrameIndex = 0;
        dispatch_semaphore_t mFrameSemaphore = nullptr;
        static thread_local NS::AutoreleasePool *mMainPool;
        MIndirectBufferConvertShaderExecutor mIndirectIndexedRenderCommandConvertShaderExecutor = nullptr;
        Private::FullscreenTextureExecutor mFullscreenTextureExecutor;
        Private::DeviceObjectCache mObjectCache;
        eastl::vector<Buffer> mLiveBuffers;
        eastl::vector<Texture> mLiveTextures;
        eastl::vector<Sampler> mLiveSamplers;
        bool mResourcePoolsInitialized = false;
        DeviceLostState mLostState;
        bool mDestroyed = false;
    };

} // namespace GVM::RHI::Metal
