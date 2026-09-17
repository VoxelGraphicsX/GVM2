#include "MComputePassEncoder.hpp"
#include "MBindGroup.hpp"
#include "MBuffer.hpp"
#include "MCommandEncoder.hpp"
#include "MComputePipeline.hpp"
#include "MDevice.hpp"
#include "MEnumUtils.hpp"
#include "MQuerySet.hpp"
#include "MTexture.hpp"
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
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        if (mCacheData.pipeline == nullptr || mCacheData.pipeline != pipeline)
        {
            mCacheData.pipeline = pipeline;

            auto pNativePipeline = eastl::static_pointer_cast<MComputePipeline>(pipeline)->getNativePipelineState();
            this->mNativeComputeEncoder->setComputePipelineState(pNativePipeline);
            mLastThreadGroupSize = eastl::static_pointer_cast<MComputePipeline>(pipeline)->getLocalThreadGroupSize();
        }
    }

    void MComputePassEncoder::setBindGroup(BindGroup group, uint32_t groupIndex)
    {
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
    }

    void MComputePassEncoder::dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        this->mNativeComputeEncoder->dispatchThreadgroups(MTL::Size(x, y, z), mLastThreadGroupSize);
    }

    void MComputePassEncoder::dispatchWorkgroupsIndirect(BufferRange indirectBuffer)
    {
        const auto autoreleasePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
        this->mNativeComputeEncoder->dispatchThreadgroups(static_cast<MBuffer *>(indirectBuffer.buffer.get())->getNativeBuffer(), indirectBuffer.offset, mLastThreadGroupSize);
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
        mEnded = true;
    }

    MTL::ComputeCommandEncoder *MComputePassEncoder::getNativeEncoder() const
    {
        return this->mNativeComputeEncoder;
    }

} // namespace GVM::RHI::Metal
