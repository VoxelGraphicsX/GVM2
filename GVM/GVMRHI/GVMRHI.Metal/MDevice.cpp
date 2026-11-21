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
#include "MUtilShaders/MUtilShaders.hpp"
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
        mArgumentPagePool = eastl::make_intrusive<MArgumentPagePool>(*this);
    }
    thread_local NS::AutoreleasePool *MDevice::mMainPool = nullptr;
    void MDevice::init(MInstance *instance)
    {
        mInstance = instance;
        mLostState->setCallback(instance->getDescriptor().deviceLostCallback);
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
        mArgumentBuffersTier = mNativeDevice->argumentBuffersSupport();
        mMaxIndirectCountDraws = !mNativeDevice->supportsFamily(MTL::GPUFamilyApple1) &&
            mNativeDevice->supportsFamily(MTL::GPUFamilyMac2) ? 16384u : UINT32_MAX;
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
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
            mDiagnosticsOverlayQueue = GVM::RHI::Private::createDiagnosticsOverlayQueue(this, mMainQueue, createMetalDiagnosticsOverlayShaderProvider());
#else
            throw std::runtime_error("Diagnostics overlay was requested, but this build was compiled with GVM_DIAGNOSTICS_OVERLAY_ENABLED=0.");
#endif
        }

        mIndirectIndexedRenderCommandConvertShaderExecutor = eastl::make_intrusive<MIndirectBufferConvertShaderExecutorImpl>();
        mIndirectIndexedRenderCommandConvertShaderExecutor->init(this, {});

        const ShaderModule fullscreenVertexShader = createShaderModule({
            .label = "FullscreenTextureVertexShader",
            .code = getFullscreenTextureVertexShader(),
        });
        const ShaderModule fullscreenFragmentShader = createShaderModule({
            .label = "FullscreenTextureFragmentShader",
            .code = getFullscreenTextureFragmentShader(),
        });
        mFullscreenTextureExecutor.init(*this, {
            .vertex = fullscreenVertexShader,
            .vertexEntryPoint = "vertexMain",
            .fragment = fullscreenFragmentShader,
            .fragmentEntryPoint = "fragmentMain",
            .flippedFragment = fullscreenFragmentShader,
            .flippedFragmentEntryPoint = "fragmentMainFlipped",
        });
    }

    Queue MDevice::getQueue(const QueueDescriptor &descriptor)
    {
        if (descriptor.index != 0)
        {
            return DeviceImpl::getQueue(descriptor);
        }
        if (descriptor.prefer == QueuePreference::Transfer)
        {
            {
                std::lock_guard lock(mQueueMutex);
                if (mTransferQueue == nullptr && !mTransferUnavailable)
                {
                    uint32_t maxCmdBuffers = MQueue::DefaultMaxCommandBufferCount;
                    auto native = NS::TransferPtr(mNativeDevice->newCommandQueue(maxCmdBuffers));
                    if (!native)
                    {
                        maxCmdBuffers = 64u;
                        native = NS::TransferPtr(mNativeDevice->newCommandQueue());
                    }
                    if (native)
                    {
                        auto candidate = eastl::make_unique<MQueue>();
                        candidate->init(this, "TransferQueue", native.get(), maxCmdBuffers);
                        mTransferQueue = candidate.release();
                    }
                    else
                    {
                        mTransferUnavailable = true;
                    }
                }
                if (mTransferQueue != nullptr)
                {
                    return mTransferQueue;
                }
            }
            return getQueue({.prefer = QueuePreference::Compute});
        }
        if (descriptor.prefer != QueuePreference::Compute)
        {
            return DeviceImpl::getQueue(descriptor);
        }
        std::lock_guard lock(mQueueMutex);
        if (mComputeQueue == nullptr && !mComputeUnavailable)
        {
            uint32_t maxCmdBuffers = MQueue::DefaultMaxCommandBufferCount;
            auto native = NS::TransferPtr(mNativeDevice->newCommandQueue(maxCmdBuffers));
            if (!native)
            {
                maxCmdBuffers = 64u;
                native = NS::TransferPtr(mNativeDevice->newCommandQueue());
            }
            if (!native)
            {
                mComputeUnavailable = true;
                return getMainQueue();
            }
            auto candidate = eastl::make_unique<MQueue>();
            candidate->init(this, "ComputeQueue", native.get(), maxCmdBuffers);
            mComputeQueue = candidate.release();
        }
        return mComputeQueue != nullptr ? mComputeQueue : getMainQueue();
    }

    Queue MDevice::getMainQueue() const
    {
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
        return mDiagnosticsOverlayQueue != nullptr ? mDiagnosticsOverlayQueue : mMainQueue;
#else
        return mMainQueue;
#endif
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
    DeviceMemoryBudget MDevice::getMemoryBudget() const
    {
        DeviceMemoryBudget budget = {};
        budget.reportedByDriver = True;
        MemoryHeapBudget heap = {};
        heap.budgetBytes = mNativeDevice->recommendedMaxWorkingSetSize();
        heap.usageBytes = mNativeDevice->currentAllocatedSize();
        heap.deviceLocal = True;
        budget.heaps.push_back(heap);
        return budget;
    }

    DeviceMemoryProperties MDevice::getMemoryProperties() const
    {
        return mMemoryProperties;
    }

    /** Returns the diagnostics overlay configuration captured from the owning Metal instance. */
    RuntimeDiagnosticsOverlayConfig MDevice::getDiagnosticsOverlayConfig() const
    {
        return mDiagnosticsOverlayConfig;
    }

    eastl::vector<uint8_t> MDevice::getPipelineCacheData() const
    {
        return {};
    }

    /** Copies live buffer and texture metadata without waiting for GPU work. */
    DiagnosticsResourceSnapshot MDevice::getDiagnosticsResourceSnapshot() const
    {
        DiagnosticsResourceSnapshot snapshot = {};
        std::lock_guard lock(mResourceSnapshotMutex);
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

    void MDevice::ensureUsable(const char *apiName) const
    {
        if (mDestroyed || mNativeDevice == nullptr)
        {
            throw std::runtime_error((eastl::string(apiName) + " was called on a destroyed Metal device.").c_str());
        }
        mLostState->throwIfLost(apiName);
    }

    DeviceLostState &MDevice::getLostState()
    {
        return *mLostState;
    }

    const eastl::shared_ptr<DeviceLostState> &MDevice::getLostStateHandle() const
    {
        return mLostState;
    }

    eastl::optional<DeviceLostInfo> MDevice::getLostInfo() const
    {
        return mLostState->getInfo();
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

    Texture MDevice::wrapNativeTexture(MTL::Texture &nativeTexture, TextureUsageFlags usage, const eastl::string &label)
    {
        ensureResourcePoolsInitialized("MDevice::wrapNativeTexture");

        auto pTexture = new MTexture();
        pTexture->initWithNativeTexture(this, &nativeTexture, usage, label);
        auto handle = mTexturePool.alloc(pTexture);
        trackTexture(handle);
        return handle;
    }

    ShaderModule MDevice::createShaderModule(const ShaderModuleDescriptor &descriptor)
    {
        if (ShaderModule cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        eastl::intrusive_ptr<MShaderModule> shaderModule = eastl::make_intrusive<MShaderModule>();
        shaderModule->init(this, descriptor);
        return mObjectCache.insert(descriptor, shaderModule);
    }

    ComputePipeline MDevice::createComputePipeline(const ComputePipelineDescriptor &descriptor)
    {
        if (ComputePipeline cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        eastl::intrusive_ptr<MComputePipeline> pipeline = eastl::make_intrusive<MComputePipeline>();
        pipeline->init(this, descriptor);
        return mObjectCache.insert(descriptor, pipeline);
    }

    RenderPipeline MDevice::createRenderPipeline(const RenderPipelineDescriptor &descriptor)
    {
        if (RenderPipeline cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        eastl::intrusive_ptr<MRenderPipeline> pipeline = eastl::make_intrusive<MRenderPipeline>();
        pipeline->init(this, descriptor);
        return mObjectCache.insert(descriptor, pipeline);
    }

    BindGroupLayout MDevice::createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor)
    {
        if (BindGroupLayout cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        eastl::intrusive_ptr<MBindGroupLayout> bindGroupLayout = eastl::make_intrusive<MBindGroupLayout>();
        bindGroupLayout->init(this, descriptor);
        return mObjectCache.insert(descriptor, bindGroupLayout);
    }

    PipelineLayout MDevice::createPipelineLayout(const PipelineLayoutDescriptor &descriptor)
    {
        if (PipelineLayout cached = mObjectCache.find(descriptor))
        {
            return cached;
        }
        eastl::intrusive_ptr<MPipelineLayout> pipelineLayout = eastl::make_intrusive<MPipelineLayout>();
        pipelineLayout->init(this, descriptor);
        return mObjectCache.insert(descriptor, pipelineLayout);
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
        untrackBuffer(buffer);
        buffer.get()->destroy();
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
        untrackTexture(texture);
        texture.get()->destroy();
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
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
            if (mDiagnosticsOverlayQueue != nullptr)
            {
                mDiagnosticsOverlayQueue->destroy();
                delete mDiagnosticsOverlayQueue;
                mDiagnosticsOverlayQueue = nullptr;
            }
#endif
            if (mTransferQueue != nullptr)
            {
                mTransferQueue->destroy();
                delete mTransferQueue;
                mTransferQueue = nullptr;
            }
            if (mComputeQueue != nullptr)
            {
                mComputeQueue->destroy();
                delete mComputeQueue;
                mComputeQueue = nullptr;
            }
            this->mMainQueue->destroy();
            delete this->mMainQueue;
            this->mMainQueue = nullptr;
        }

        mIndirectIndexedRenderCommandConvertShaderExecutor = nullptr;
        mFullscreenTextureExecutor = Private::FullscreenTextureExecutor{};

        eastl::vector<Sampler> liveSamplers;
        {
            std::lock_guard lock(mResourceSnapshotMutex);
            liveSamplers.swap(mLiveSamplers);
        }
        for (auto &sampler : liveSamplers)
        {
            freeSampler(sampler);
        }

        eastl::vector<Texture> liveTextures;
        eastl::vector<Buffer> liveBuffers;
        {
            std::lock_guard lock(mResourceSnapshotMutex);
            liveTextures.swap(mLiveTextures);
            liveBuffers.swap(mLiveBuffers);
        }
        for (auto &texture : liveTextures)
        {
            freeTexture(texture);
        }

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
        mObjectCache.clear();
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

    Private::FullscreenTextureExecutor &MDevice::getFullscreenTextureExecutor()
    {
        return mFullscreenTextureExecutor;
    }

    uint64_t MDevice::getFrameIndex() const
    {
        return mFrameIndex;
    }

    void MDevice::trackBuffer(Buffer buffer)
    {
        if (!buffer.isNull())
        {
            std::lock_guard lock(mResourceSnapshotMutex);
            mLiveBuffers.push_back(buffer);
        }
    }

    void MDevice::trackTexture(Texture texture)
    {
        if (!texture.isNull())
        {
            std::lock_guard lock(mResourceSnapshotMutex);
            mLiveTextures.push_back(texture);
        }
    }

    void MDevice::trackSampler(Sampler sampler)
    {
        if (!sampler.isNull())
        {
            std::lock_guard lock(mResourceSnapshotMutex);
            mLiveSamplers.push_back(sampler);
        }
    }

    void MDevice::untrackBuffer(Buffer buffer)
    {
        std::lock_guard lock(mResourceSnapshotMutex);
        eraseTrackedHandle(mLiveBuffers, buffer);
    }

    void MDevice::untrackTexture(Texture texture)
    {
        std::lock_guard lock(mResourceSnapshotMutex);
        eraseTrackedHandle(mLiveTextures, texture);
    }

    void MDevice::untrackSampler(Sampler sampler)
    {
        std::lock_guard lock(mResourceSnapshotMutex);
        eraseTrackedHandle(mLiveSamplers, sampler);
    }

    Arguments MDevice::createArguments(const ArgumentsDescriptor &descriptor)
    {
        auto snapshot = eastl::make_intrusive<MArguments>();
        snapshot->init(*this, descriptor);
        return snapshot;
    }

    void MDevice::setArgumentMemoryBudget(uint64_t bytes) { mArgumentPagePool->setLimit(bytes); }

} // namespace GVM::RHI::Metal
