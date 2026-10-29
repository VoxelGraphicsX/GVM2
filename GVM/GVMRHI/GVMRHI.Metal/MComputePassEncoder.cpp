#include "MComputePassEncoder.hpp"
#include "MBindGroup.hpp"
#include "MBufferTableLayout.hpp"
#include "MBuffer.hpp"
#include "MCommandEncoder.hpp"
#include "MComputePipeline.hpp"
#include "MDevice.hpp"
#include "MPipelineLayout.hpp"
#include "MEnumUtils.hpp"
#include "MQuerySet.hpp"
#include "MTexture.hpp"
#include <GVMRHI/Private/ArgumentValidation.hpp>
namespace GVM::RHI::Metal
{
    namespace
    {
        void configureComputeTimestampWrites(MTL::ComputePassDescriptor *nativeDescriptor, const PassTimestampWrites &timestampWrites)
        {
            MQuerySet *querySet = validateMetalTimestampWrites("MComputePassEncoder::init", timestampWrites);
            if (querySet == nullptr)
            {
                return;
            }
            auto *attachment = nativeDescriptor->sampleBufferAttachments()->object(0);
            attachment->setSampleBuffer(querySet->getNativeCounterSampleBuffer());
            attachment->setStartOfEncoderSampleIndex(toMetalCounterSampleIndex(timestampWrites.beginningOfPassWriteIndex));
            attachment->setEndOfEncoderSampleIndex(toMetalCounterSampleIndex(timestampWrites.endOfPassWriteIndex));
        }

        void configureComputePassCounterWrites(MTL::ComputePassDescriptor *nativeDescriptor, const PassCounterWrites &counterWrites)
        {
            MQuerySet *stageQuerySet = validateMetalPassCounterWrites(
                "MComputePassEncoder::init",
                QueryType::PassCounterStageUtilization,
                counterWrites.stageUtilizationQuerySet,
                counterWrites.stageUtilizationBeginIndex,
                counterWrites.stageUtilizationEndIndex);
            if (stageQuerySet != nullptr)
            {
                auto *attachment = nativeDescriptor->sampleBufferAttachments()->object(1);
                attachment->setSampleBuffer(stageQuerySet->getNativeCounterSampleBuffer());
                attachment->setStartOfEncoderSampleIndex(toMetalCounterSampleIndex(counterWrites.stageUtilizationBeginIndex));
                attachment->setEndOfEncoderSampleIndex(toMetalCounterSampleIndex(counterWrites.stageUtilizationEndIndex));
            }

            MQuerySet *statisticQuerySet = validateMetalPassCounterWrites(
                "MComputePassEncoder::init",
                QueryType::PassCounterStatistic,
                counterWrites.statisticQuerySet,
                counterWrites.statisticBeginIndex,
                counterWrites.statisticEndIndex);
            if (statisticQuerySet != nullptr)
            {
                auto *attachment = nativeDescriptor->sampleBufferAttachments()->object(2);
                attachment->setSampleBuffer(statisticQuerySet->getNativeCounterSampleBuffer());
                attachment->setStartOfEncoderSampleIndex(toMetalCounterSampleIndex(counterWrites.statisticBeginIndex));
                attachment->setEndOfEncoderSampleIndex(toMetalCounterSampleIndex(counterWrites.statisticEndIndex));
            }
        }

        bool hasComputePassCounterWrites(const PassCounterWrites &counterWrites)
        {
            return counterWrites.stageUtilizationQuerySet != nullptr || counterWrites.statisticQuerySet != nullptr;
        }
    } // namespace

    MComputePassEncoder::MComputePassEncoder()
    {
    }

    MComputePassEncoder::~MComputePassEncoder()
    {
        if (mNativeComputeEncoder != nullptr) { mNativeComputeEncoder->release(); }
    }

    void MComputePassEncoder::init(MDevice *device, GVM::RHI::CommandEncoder commandEncoder, const ComputePassDescriptor &descriptor)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        this->mDevice = device;
        this->mWeakCommandEncoder = eastl::static_pointer_cast<GVM::RHI::Metal::MCommandEncoder>(commandEncoder).get();
        this->mCommandBuffer = mWeakCommandEncoder->getNativeCommandEncoder();
        this->mLabelName = descriptor.label;
        mValidator.init(descriptor.label, Private::ComputePassLimits{.maxBindGroups = MetalBindGroupCount});

        if (descriptor.timestampWrites.querySet == nullptr && !hasComputePassCounterWrites(descriptor.counterWrites))
        {
            this->mNativeComputeEncoder = this->mCommandBuffer->computeCommandEncoder();
        }
        else
        {
            auto *computePassDescriptor = MTL::ComputePassDescriptor::alloc()->init();
            configureComputeTimestampWrites(computePassDescriptor, descriptor.timestampWrites);
            configureComputePassCounterWrites(computePassDescriptor, descriptor.counterWrites);
            if (descriptor.timestampWrites.querySet != nullptr)
            {
                mWeakCommandEncoder->retainQuerySet(descriptor.timestampWrites.querySet);
            }
            if (descriptor.counterWrites.stageUtilizationQuerySet != nullptr)
            {
                mWeakCommandEncoder->retainQuerySet(descriptor.counterWrites.stageUtilizationQuerySet);
            }
            if (descriptor.counterWrites.statisticQuerySet != nullptr)
            {
                mWeakCommandEncoder->retainQuerySet(descriptor.counterWrites.statisticQuerySet);
            }
            this->mNativeComputeEncoder = this->mCommandBuffer->computeCommandEncoder(computePassDescriptor);
            computePassDescriptor->release();
        }
        this->mNativeComputeEncoder->retain();
        if (descriptor.label.empty() == false)
        {
            mNativeComputeEncoder->setLabel(NS::String::string(descriptor.label.c_str(), NS::UTF8StringEncoding));
        }
    }

    void MComputePassEncoder::setPipeline(ComputePipeline pipeline)
    {
        ensureOpen("MComputePassEncoder::setPipeline");
        const auto *selectedPipeline = static_cast<MComputePipeline *>(pipeline.get());
        const PipelineLayout selectedLayout = selectedPipeline != nullptr ? selectedPipeline->getLayout() : PipelineLayout{};
        const auto *selectedLayoutImpl = static_cast<MPipelineLayout *>(selectedLayout.get());
        mValidator.setPipeline(pipeline, selectedLayout, selectedLayoutImpl != nullptr ? &selectedLayoutImpl->getDescriptor() : nullptr);
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        if (mCacheData.pipeline == nullptr || mCacheData.pipeline != pipeline)
        {
            mCacheData.pipeline = pipeline;
            mCacheData.bindgroups.clear();
            mCurrentArguments = nullptr;

            auto pNativePipeline = eastl::static_pointer_cast<MComputePipeline>(pipeline)->getNativePipelineState();
            this->mNativeComputeEncoder->setComputePipelineState(pNativePipeline);
            mLastThreadGroupSize = eastl::static_pointer_cast<MComputePipeline>(pipeline)->getLocalThreadGroupSize();
        }
    }

    void MComputePassEncoder::setBindGroup(BindGroup group, uint32_t groupIndex)
    {
        ensureOpen("MComputePassEncoder::setBindGroup");
        mValidator.checkBindGroup(group, groupIndex);
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        const auto &it = mCacheData.bindgroups.find(groupIndex);
        bool doBindgroup = false;
        if (it != mCacheData.bindgroups.end() && it->second != group)
        {
            doBindgroup = true;
            mCacheData.bindgroups[groupIndex] = group;
        }
        if (it == mCacheData.bindgroups.end())
        {
            mCacheData.bindgroups.emplace(groupIndex, group);
            doBindgroup = true;
        }
        if (doBindgroup)
        {
            eastl::static_pointer_cast<MBindGroup>(group)->trackUsage(this->mNativeComputeEncoder);
            this->mNativeComputeEncoder->setBuffer(eastl::static_pointer_cast<MBindGroup>(group)->getNativeBuffer(), 0, groupIndex);
        }
        const auto *bindGroup = static_cast<MBindGroup *>(group.get());
        mValidator.setBindGroup(group, groupIndex, bindGroup->getDescriptor().layout, bindGroup->getComputeTextureUses());
    }

    void MComputePassEncoder::setArguments(Arguments arguments)
    {
        ensureOpen("MComputePassEncoder::setArguments");
        if (mEnded)
        {
            throw std::logic_error("Select a compute pipeline before recording Args on an open pass.");
        }
        auto *snapshot = dynamic_cast<MArguments *>(arguments.get());
        if (snapshot == nullptr)
        {
            throw std::invalid_argument("Compute Args snapshot does not belong to the selected pipeline layout.");
        }
        mValidator.checkArguments(snapshot->getLayout());
        const auto allocation = mWeakCommandEncoder->allocateArguments(arguments);
        snapshot->trackUsage(*mNativeComputeEncoder);
        if (allocation.buffer != nullptr)
        {
            mNativeComputeEncoder->setBuffer(allocation.buffer, allocation.offset, MetalArgumentRootBufferIndex);
        }
        mCurrentArguments = snapshot;
        mValidator.setArguments(snapshot->getComputeTextureUses());
    }

    bool MComputePassEncoder::pipelineDeclaresArguments() const
    {
        const auto *pipeline = static_cast<MComputePipeline *>(mCacheData.pipeline.get());
        return pipeline != nullptr && pipeline->getLayout() != nullptr &&
            static_cast<MPipelineLayout *>(pipeline->getLayout().get())->getDescriptor().arguments.byteSize != 0;
    }

    void MComputePassEncoder::dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z)
    {
        ensureOpen("MComputePassEncoder::dispatchWorkgroups");
        mValidator.checkDispatch(x, y, z);
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        this->mNativeComputeEncoder->dispatchThreadgroups(MTL::Size(x, y, z), mLastThreadGroupSize);
    }

    void MComputePassEncoder::dispatchWorkgroupsIndirect(BufferRange indirectBuffer)
    {
        ensureOpen("MComputePassEncoder::dispatchWorkgroupsIndirect");
        if (mEnded)
        {
            throw std::logic_error("Indirect dispatch requires a pipeline on an open compute pass.");
        }
        mValidator.checkDispatchIndirect(indirectBuffer);
        auto &source = resolveBufferRange(*mDevice, indirectBuffer, BufferUsage::Indirect, 4u);
        recordDispatchIndirect(*source.getNativeBuffer(), indirectBuffer.offset);
    }

    void MComputePassEncoder::recordDispatchIndirect(MTL::Buffer &buffer, uint64_t offset)
    {
        if (mEnded || mCacheData.pipeline == nullptr)
        {
            throw std::logic_error("Indirect dispatch requires a pipeline on an open compute pass.");
        }
        Private::requireArgumentSnapshot(pipelineDeclaresArguments(), mCurrentArguments != nullptr);
        if (buffer.device() != mDevice->getNativeDevice())
        {
            throw std::invalid_argument("Indirect dispatch requires a native buffer from the same device.");
        }
        mNativeComputeEncoder->dispatchThreadgroups(&buffer, offset, mLastThreadGroupSize);
    }

    void MComputePassEncoder::end()
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        if (mEnded)
        {
            return;
        }
        this->mNativeComputeEncoder->endEncoding();
        if (mWeakCommandEncoder != nullptr)
        {
            mWeakCommandEncoder->notifyPassEnded();
        }
        mCurrentArguments = nullptr;
        mEnded = true;
    }

    MTL::ComputeCommandEncoder *MComputePassEncoder::getNativeEncoder() const
    {
        return this->mNativeComputeEncoder;
    }

    void MComputePassEncoder::ensureOpen(const char *apiName) const
    {
        if (mEnded)
        {
            throw std::runtime_error((eastl::string(apiName) + " was called on an ended compute pass encoder.").c_str());
        }
    }

} // namespace GVM::RHI::Metal
