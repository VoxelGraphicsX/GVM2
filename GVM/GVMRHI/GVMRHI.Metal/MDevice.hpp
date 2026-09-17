#pragma once
#include "MDefines.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include <Utils/ResourcePool.hpp>
#include <dispatch/dispatch.h>

#include "MUtilShaders/MIndirectBufferConvertShaderExecutor.hpp"
#include "MUtilShaders/MRenderToSwapchainExecutor.hpp"
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
        /// Returns the diagnostics overlay configuration captured from the owning Metal instance.
        virtual RuntimeDiagnosticsOverlayConfig getDiagnosticsOverlayConfig() const override;
        /// Returns a snapshot of live Metal buffers and textures for runtime diagnostics display.
        virtual DiagnosticsResourceSnapshot getDiagnosticsResourceSnapshot() const override;
        virtual Buffer createBuffer(const BufferDescriptor &descriptor) override;
        virtual QuerySet createQuerySet(const QuerySetDescriptor &descriptor) override;
        virtual Texture createTexture(const TextureDescriptor &descriptor) override;
        virtual ShaderModule createShaderModule(const ShaderModuleDescriptor &descriptor) override;
        virtual ComputePipeline createComputePipeline(const ComputePipelineDescriptor &descriptor) override;
        virtual RenderPipeline createRenderPipeline(const RenderPipelineDescriptor &descriptor) override;
        virtual BindGroupLayout createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor) override;
        virtual PipelineLayout createPipelineLayout(const PipelineLayoutDescriptor &descriptor) override;
        virtual BindGroup createBindGroup(const BindGroupDescriptor &descriptor) override;
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
        /// Returns the raw Metal main queue for backend-internal paths that must not observe wrapper queues.
        MQueue *getMainQueueImpl() const;
        uint64_t getFrameIndex() const;
        void beginFrame();
        void endFrame();
        static constexpr uint32_t MAX_FRAME_COUNT = 3;
        MIndirectBufferConvertShaderExecutor getIndirectIndexedRenderCommandConvertShaderExecutor() const;
        MRenderToSwapchainExecutor getRenderToSwapchainExecutor() const;

    private:
        void ensureResourcePoolsInitialized(const char *apiName) const;
        void trackBuffer(Buffer buffer);
        void trackTexture(Texture texture);
        void trackSampler(Sampler sampler);
        void untrackBuffer(Buffer buffer);
        void untrackTexture(Texture texture);
        void untrackSampler(Sampler sampler);

        MInstance *mInstance = nullptr;
        MTL::Device *mNativeDevice = nullptr;
        MTL::CounterSet *mTimestampCounterSet = nullptr;
        MTL::CounterSet *mStageUtilizationCounterSet = nullptr;
        MTL::CounterSet *mStatisticCounterSet = nullptr;
        TimestampQuerySupport mTimestampQuerySupport = {};
        PassCounterQuerySupport mPassCounterQuerySupport = {};
        DeviceMemoryProperties mMemoryProperties = {};
        RuntimeDiagnosticsOverlayConfig mDiagnosticsOverlayConfig = {};
        MQueue *mMainQueue = nullptr;
        Queue mDiagnosticsOverlayQueue = nullptr;
        Logger mLogger;
        uint64_t mFrameIndex = 0;
        dispatch_semaphore_t mFrameSemaphore = nullptr;
        static thread_local NS::AutoreleasePool *mMainPool;
        MIndirectBufferConvertShaderExecutor mIndirectIndexedRenderCommandConvertShaderExecutor = nullptr;
        MRenderToSwapchainExecutor mRenderToSwapchainExecutor = nullptr;
        eastl::vector<Buffer> mLiveBuffers;
        eastl::vector<Texture> mLiveTextures;
        eastl::vector<Sampler> mLiveSamplers;
        bool mResourcePoolsInitialized = false;
        bool mDestroyed = false;
    };

} // namespace GVM::RHI::Metal
