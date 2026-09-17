#include "MDevice.hpp"
#include "MBindGroup.hpp"
#include "MBindGroupLayout.hpp"
#include "MBuffer.hpp"
#include "MComputePipeline.hpp"
#include "MDiagnosticsOverlayShaders.hpp"
#include "MInstance.hpp"
#include "MPipelineLayout.hpp"
#include "MQuerySet.hpp"
#include "MQueue.hpp"
#include "MRenderPipeline.hpp"
#include "MSampler.hpp"
#include "MShaderModule.hpp"
#include "MTexture.hpp"
#include <GVMRHI/Private/GEnumUtils.hpp>
#include <GVMRHI/Private/RHIDiagnosticsOverlay.hpp>
#include <EASTL/make_intrusive.h>
#include <stdexcept>
namespace GVM::RHI::Metal
{
    namespace
    {
        template <typename HandleType>
        void eraseTrackedHandle(eastl::vector<HandleType> &handles, const HandleType &handle)
        {
            auto it = eastl::find(handles.begin(), handles.end(), handle);
            if (it != handles.end())
            {
                handles.erase(it);
            }
        }

        MTL::CounterSet *findCounterSet(MTL::Device *device, NS::String *counterSetName)
        {
            if (device == nullptr || counterSetName == nullptr)
            {
                return nullptr;
            }
            NS::Array *counterSets = device->counterSets();
            const NS::UInteger counterSetCount = counterSets != nullptr ? counterSets->count() : 0u;
            for (NS::UInteger index = 0; index < counterSetCount; ++index)
            {
                auto *counterSet = counterSets->object<MTL::CounterSet>(index);
                if (counterSet != nullptr &&
                    counterSet->name() != nullptr &&
                    counterSet->name()->isEqualToString(counterSetName))
                {
                    return counterSet;
                }
            }
            return nullptr;
        }

        /** Rounds a texel extent up to a compressed-format block count. */
        uint64_t divideRoundUp(uint64_t value, uint64_t divisor)
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        /** Estimates storage bytes for a live texture using public texture dimensions and format block metadata. */
        uint64_t estimateTextureStorageBytes(Texture texture)
        {
            if (texture.isNull())
            {
                return 0u;
            }

            const Private::BlockInfo block = Private::getTextureBlockInfo(texture->getFormat());
            uint64_t estimatedBytes = 0u;
            for (uint32_t mipLevel = 0u; mipLevel < texture->getMipLevelCount(); ++mipLevel)
            {
                const uint64_t mipWidth = eastl::max(1u, texture->getWidth() >> mipLevel);
                const uint64_t mipHeight = eastl::max(1u, texture->getHeight() >> mipLevel);
                const uint64_t mipDepth = eastl::max(1u, texture->getDepth() >> mipLevel);
                const uint64_t blockColumns = divideRoundUp(mipWidth, block.width);
                const uint64_t blockRows = divideRoundUp(mipHeight, block.height);
                estimatedBytes += blockColumns * blockRows * mipDepth * texture->getArrayLayerCount() * block.bytes;
            }
            return estimatedBytes;
        }

        /** Adds a live buffer handle to a diagnostics snapshot when the handle is valid. */
        void appendBufferSnapshotEntry(DiagnosticsResourceSnapshot &snapshot, Buffer buffer)
        {
            if (buffer.isNull())
            {
                return;
            }

            DiagnosticsResourceSnapshotEntry entry = {};
            entry.kind = DiagnosticsResourceKind::Buffer;
            entry.label = buffer->getLabelName();
            entry.estimatedBytes = buffer->getStorageSize();
            snapshot.totalEstimatedBytes += entry.estimatedBytes;
            snapshot.entries.push_back(entry);
        }

        /** Adds a live texture handle to a diagnostics snapshot when the handle is valid. */
        void appendTextureSnapshotEntry(DiagnosticsResourceSnapshot &snapshot, Texture texture)
        {
            if (texture.isNull())
            {
                return;
            }

            DiagnosticsResourceSnapshotEntry entry = {};
            entry.kind = DiagnosticsResourceKind::Texture;
            entry.label = texture->getLabelName();
            entry.estimatedBytes = estimateTextureStorageBytes(texture);
            entry.width = texture->getWidth();
            entry.height = texture->getHeight();
            entry.depth = texture->getDepth();
            entry.mipLevelCount = texture->getMipLevelCount();
            entry.arrayLayerCount = texture->getArrayLayerCount();
            entry.format = texture->getFormat();
            snapshot.totalEstimatedBytes += entry.estimatedBytes;
            snapshot.entries.push_back(entry);
        }

    } // namespace

    void MDevice::ensureResourcePoolsInitialized(const char *apiName) const
    {
        if (!mResourcePoolsInitialized)
        {
            throw std::logic_error(std::string(apiName) + " was called before MDevice resource pools finished initialization.");
        }
    }

    MDevice::MDevice()
    {
    }
    thread_local NS::AutoreleasePool *MDevice::mMainPool = nullptr;
    void MDevice::init(MInstance *instance)
    {
        mInstance = instance;
        mLogger = instance != nullptr ? instance->getLogger() : Logger{};
        mDiagnosticsOverlayConfig = instance != nullptr ? instance->getDescriptor().diagnosticsOverlay : RuntimeDiagnosticsOverlayConfig{};
        this->activateGCPool();
        this->mNativeDevice = MTL::CreateSystemDefaultDevice();
        if (this->mNativeDevice == nullptr)
        {
            NS::Array *devices = MTL::CopyAllDevices();
            const NS::UInteger deviceCount = devices != nullptr ? devices->count() : 0u;
            for (NS::UInteger i = 0; i < deviceCount; ++i)
            {
                if (auto *candidate = devices->object<MTL::Device>(i))
                {
                    this->mNativeDevice = candidate->retain();
                    break;
                }
            }
            if (devices != nullptr)
            {
                devices->release();
            }
        }
        if (this->mNativeDevice == nullptr)
        {
            throw std::runtime_error("MDevice::init failed to acquire a native Metal device.");
        }
        mMemoryProperties.unifiedMemory = this->mNativeDevice->hasUnifiedMemory() ? True : False;
        mTimestampCounterSet = findCounterSet(this->mNativeDevice, MTL::CommonCounterSetTimestamp);
        if (mTimestampCounterSet != nullptr)
        {
            mTimestampCounterSet = mTimestampCounterSet->retain();
        }
        mStageUtilizationCounterSet = findCounterSet(this->mNativeDevice, MTL::CommonCounterSetStageUtilization);
        if (mStageUtilizationCounterSet != nullptr)
        {
            mStageUtilizationCounterSet = mStageUtilizationCounterSet->retain();
        }
        mStatisticCounterSet = findCounterSet(this->mNativeDevice, MTL::CommonCounterSetStatistic);
        if (mStatisticCounterSet != nullptr)
        {
            mStatisticCounterSet = mStatisticCounterSet->retain();
        }
        const bool supportsStageBoundaryCounters =
            this->mNativeDevice->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary);
        mTimestampQuerySupport.supported =
            mTimestampCounterSet != nullptr && supportsStageBoundaryCounters ? True : False;
        mTimestampQuerySupport.validBits = mTimestampQuerySupport.supported ? 64u : 0u;
        mTimestampQuerySupport.tickPeriodNs = 1.0;
        mTimestampQuerySupport.passTimestampWritesSupported = mTimestampQuerySupport.supported;
        mPassCounterQuerySupport.backendName = "metal";
        mPassCounterQuerySupport.requiresExclusiveProfilingLock = False;
        mPassCounterQuerySupport.supportsSingleSubmitCounterPass = True;
        mPassCounterQuerySupport.supported =
            mStageUtilizationCounterSet != nullptr && mStatisticCounterSet != nullptr && supportsStageBoundaryCounters ? True : False;
        if (mPassCounterQuerySupport.supported == False)
        {
            mPassCounterQuerySupport.unsupportedReason =
                "Metal device does not expose stage utilization/statistic counter sets with stage-boundary sampling.";
        }

        mDestroyed = false;
        mResourcePoolsInitialized = false;
        mBufferPool.init();
        mTexturePool.init();
        mTextureViewPool.init();
        mSamplerPool.init();
        mLiveBuffers.clear();
        mLiveTextures.clear();
        mLiveSamplers.clear();
        mResourcePoolsInitialized = true;
        mFrameSemaphore = dispatch_semaphore_create(MAX_FRAME_COUNT);

        // Pools must be initialized before any subsystem allocates pooled handles.
        // MQueue::init creates persistent write-buffer blocks immediately.
        this->mMainQueue = new MQueue();
        this->mMainQueue->init(this, "MainQueue");
        if (mDiagnosticsOverlayConfig.enabled != False)
        {
            mDiagnosticsOverlayQueue = GVM::RHI::Private::createDiagnosticsOverlayQueue(this, mMainQueue, createMetalDiagnosticsOverlayShaderProvider());
        }

        mIndirectIndexedRenderCommandConvertShaderExecutor = eastl::make_intrusive<MIndirectBufferConvertShaderExecutorImpl>();
        mIndirectIndexedRenderCommandConvertShaderExecutor->init(this, {});

        mRenderToSwapchainExecutor = eastl::make_intrusive<MRenderToSwapchainExecutorImpl>();
        mRenderToSwapchainExecutor->create(this);
    }

    Queue MDevice::getMainQueue() const
    {
        return mDiagnosticsOverlayQueue != nullptr ? mDiagnosticsOverlayQueue : mMainQueue;
    }

    TimestampQuerySupport MDevice::getTimestampQuerySupport() const
    {
        return mTimestampQuerySupport;
    }

    PassCounterQuerySupport MDevice::getPassCounterQuerySupport() const
    {
        return mPassCounterQuerySupport;
    }

    /** Returns cached memory topology for the selected Metal device. */
    DeviceMemoryProperties MDevice::getMemoryProperties() const
    {
        return mMemoryProperties;
    }

    /** Returns the diagnostics overlay configuration captured from the owning Metal instance. */
    RuntimeDiagnosticsOverlayConfig MDevice::getDiagnosticsOverlayConfig() const
    {
        return mDiagnosticsOverlayConfig;
    }

    /** Returns a snapshot of live Metal resources for runtime diagnostics. */
    DiagnosticsResourceSnapshot MDevice::getDiagnosticsResourceSnapshot() const
    {
        DiagnosticsResourceSnapshot snapshot = {};
        snapshot.entries.reserve(mLiveBuffers.size() + mLiveTextures.size());
        for (Buffer buffer : mLiveBuffers)
        {
            appendBufferSnapshotEntry(snapshot, buffer);
        }
        for (Texture texture : mLiveTextures)
        {
            appendTextureSnapshotEntry(snapshot, texture);
        }
        return snapshot;
    }

    Buffer MDevice::createBuffer(const BufferDescriptor &descriptor)
    {
        ensureResourcePoolsInitialized("MDevice::createBuffer");

        auto pBuffer = new MBuffer();
        pBuffer->init(this, descriptor);
        auto handle = mBufferPool.alloc(pBuffer);
        trackBuffer(handle);
        return handle;
    }

    QuerySet MDevice::createQuerySet(const QuerySetDescriptor &descriptor)
    {
        eastl::intrusive_ptr<MQuerySet> querySet = eastl::make_intrusive<MQuerySet>();
        MTL::CounterSet *counterSet = nullptr;
        switch (descriptor.type)
        {
        case QueryType::Timestamp:
            counterSet = mTimestampCounterSet;
            break;
        case QueryType::PassCounterStageUtilization:
            counterSet = mStageUtilizationCounterSet;
            break;
        case QueryType::PassCounterStatistic:
            counterSet = mStatisticCounterSet;
            break;
        default:
            break;
        }
        querySet->init(this, descriptor, counterSet);
        return querySet;
    }

    Texture MDevice::createTexture(const TextureDescriptor &descriptor)
    {
        ensureResourcePoolsInitialized("MDevice::createTexture");

        auto pTexture = new MTexture();
        pTexture->init(this, descriptor);
        auto handle = mTexturePool.alloc(pTexture);
        trackTexture(handle);
        return handle;
    }

    ShaderModule MDevice::createShaderModule(const ShaderModuleDescriptor &descriptor)
    {
        eastl::intrusive_ptr<MShaderModule> shaderModule = eastl::make_intrusive<MShaderModule>();
        shaderModule->init(this, descriptor);
        return shaderModule;
    }

    ComputePipeline MDevice::createComputePipeline(const ComputePipelineDescriptor &descriptor)
    {
        eastl::intrusive_ptr<MComputePipeline> pipeline = eastl::make_intrusive<MComputePipeline>();
        pipeline->init(this, descriptor);
        return pipeline;
    }

    RenderPipeline MDevice::createRenderPipeline(const RenderPipelineDescriptor &descriptor)
    {
        eastl::intrusive_ptr<MRenderPipeline> pipeline = eastl::make_intrusive<MRenderPipeline>();
        pipeline->init(this, descriptor);
        return pipeline;
    }

    BindGroupLayout MDevice::createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor)
    {
        eastl::intrusive_ptr<MBindGroupLayout> bindGroupLayout = eastl::make_intrusive<MBindGroupLayout>();
        bindGroupLayout->init(this, descriptor);
        return bindGroupLayout;
    }

    PipelineLayout MDevice::createPipelineLayout(const PipelineLayoutDescriptor &descriptor)
    {
        eastl::intrusive_ptr<MPipelineLayout> pipelineLayout = eastl::make_intrusive<MPipelineLayout>();
        pipelineLayout->init(this, descriptor);
        return pipelineLayout;
    }

    BindGroup MDevice::createBindGroup(const BindGroupDescriptor &descriptor)
    {
        eastl::intrusive_ptr<MBindGroup> bindGroup = eastl::make_intrusive<MBindGroup>();
        bindGroup->init(this, descriptor);
        return bindGroup;
    }

    Sampler MDevice::createSampler(const SamplerDescriptor &descriptor)
    {
        ensureResourcePoolsInitialized("MDevice::createSampler");
        auto sampler = new MSampler();
        sampler->init(this, descriptor);
        auto handle = mSamplerPool.alloc(sampler);
        trackSampler(handle);
        return handle;
    }

    void MDevice::freeBuffer(Buffer buffer)
    {
        if (buffer.isNull())
        {
            return;
        }
        // GVMRHI exposes immediate logical destruction to user code. On Metal, resources that
        // are still referenced by in-flight command buffers remain retained by the runtime, so
        // this path is safe to use as the backend's retirement entry point.
        buffer.get()->destroy();
        untrackBuffer(buffer);
        mBufferPool.freeByHandle(buffer);
    }

    void MDevice::freeTexture(Texture texture)
    {
        if (texture.isNull())
        {
            return;
        }
        // Same lifetime contract as freeBuffer(): user code can retire the handle immediately,
        // while Metal keeps native objects alive until command-buffer references are gone.
        texture.get()->destroy();
        untrackTexture(texture);
        mTexturePool.freeByHandle(texture);
    }

    void MDevice::freeSampler(Sampler sampler)
    {
        if (sampler.isNull())
        {
            return;
        }
        untrackSampler(sampler);
        mSamplerPool.freeByHandle(sampler);
    }

    void MDevice::destroy()
    {
        if (mDestroyed)
        {
            return;
        }
        mDestroyed = true;
        mResourcePoolsInitialized = false;

        if (this->mMainQueue != nullptr)
        {
            if (mDiagnosticsOverlayQueue != nullptr)
            {
                mDiagnosticsOverlayQueue->destroy();
                delete mDiagnosticsOverlayQueue;
                mDiagnosticsOverlayQueue = nullptr;
            }
            this->mMainQueue->destroy();
            delete this->mMainQueue;
            this->mMainQueue = nullptr;
        }

        mIndirectIndexedRenderCommandConvertShaderExecutor = nullptr;
        mRenderToSwapchainExecutor = nullptr;

        eastl::vector<Sampler> liveSamplers;
        liveSamplers.swap(mLiveSamplers);
        for (auto &sampler : liveSamplers)
        {
            freeSampler(sampler);
        }

        eastl::vector<Texture> liveTextures;
        liveTextures.swap(mLiveTextures);
        for (auto &texture : liveTextures)
        {
            freeTexture(texture);
        }

        eastl::vector<Buffer> liveBuffers;
        liveBuffers.swap(mLiveBuffers);
        for (auto &buffer : liveBuffers)
        {
            freeBuffer(buffer);
        }

        if (mTimestampCounterSet != nullptr)
        {
            mTimestampCounterSet->release();
            mTimestampCounterSet = nullptr;
        }
        if (mStageUtilizationCounterSet != nullptr)
        {
            mStageUtilizationCounterSet->release();
            mStageUtilizationCounterSet = nullptr;
        }
        if (mStatisticCounterSet != nullptr)
        {
            mStatisticCounterSet->release();
            mStatisticCounterSet = nullptr;
        }
        if (mNativeDevice != nullptr)
        {
            mNativeDevice->release();
            mNativeDevice = nullptr;
        }
        if (mFrameSemaphore != nullptr)
        {
            dispatch_release(mFrameSemaphore);
            mFrameSemaphore = nullptr;
        }
        RHI::flushLogger(mLogger);
        mLogger = nullptr;
        mInstance = nullptr;
        this->releaseGCPool();
    }

    const dispatch_semaphore_t &MDevice::getFrameSemaphore() const
    {
        return mFrameSemaphore;
    }

    MTL::Device *MDevice::getNativeDevice() const
    {
        return mNativeDevice;
    }

    MQueue *MDevice::getMainQueueImpl() const
    {
        return mMainQueue;
    }

    Logger MDevice::getLogger() const
    {
        return mLogger;
    }

    void MDevice::beginFrame()
    {
    }

    void MDevice::endFrame()
    {
        mFrameIndex = (mFrameIndex + 1) % MAX_FRAME_COUNT;
    }

    MIndirectBufferConvertShaderExecutor MDevice::getIndirectIndexedRenderCommandConvertShaderExecutor() const
    {
        return this->mIndirectIndexedRenderCommandConvertShaderExecutor;
    }

    MRenderToSwapchainExecutor MDevice::getRenderToSwapchainExecutor() const
    {
        return mRenderToSwapchainExecutor;
    }

    uint64_t MDevice::getFrameIndex() const
    {
        return mFrameIndex;
    }

    void MDevice::trackBuffer(Buffer buffer)
    {
        if (!buffer.isNull())
        {
            mLiveBuffers.push_back(buffer);
        }
    }

    void MDevice::trackTexture(Texture texture)
    {
        if (!texture.isNull())
        {
            mLiveTextures.push_back(texture);
        }
    }

    void MDevice::trackSampler(Sampler sampler)
    {
        if (!sampler.isNull())
        {
            mLiveSamplers.push_back(sampler);
        }
    }

    void MDevice::untrackBuffer(Buffer buffer)
    {
        eraseTrackedHandle(mLiveBuffers, buffer);
    }

    void MDevice::untrackTexture(Texture texture)
    {
        eraseTrackedHandle(mLiveTextures, texture);
    }

    void MDevice::untrackSampler(Sampler sampler)
    {
        eraseTrackedHandle(mLiveSamplers, sampler);
    }

} // namespace GVM::RHI::Metal
