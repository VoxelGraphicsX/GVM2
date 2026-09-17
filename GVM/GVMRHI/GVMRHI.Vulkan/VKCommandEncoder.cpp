#include "VKCommandEncoder.hpp"

#include "VKBlitPassEncoder.hpp"
#include "VKBuffer.hpp"
#include "VKComputePassEncoder.hpp"
#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "VKQuerySet.hpp"
#include "VKQueue.hpp"
#include "VKRenderPassEncoder.hpp"

#include <EASTL/string.h>

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    VKCommandEncoder::~VKCommandEncoder()
    {
        if (mDevice == nullptr || mSubmitted)
        {
            mCommandContext.reset();
            return;
        }

        for (const Buffer &buffer : mRetainedBuffers)
        {
            mDevice->releasePendingCommandBufferReference(buffer);
        }
        for (const Texture &texture : mRetainedTextures)
        {
            mDevice->releasePendingCommandTextureReference(texture);
        }

        if (mQueue != nullptr)
        {
            mQueue->getResourceStateDB().discardEndedCommandEncoderState(*this);
            mQueue->recycleCommandBufferContext(eastl::move(mCommandContext));
        }
    }

    void VKCommandEncoder::init(VKQueue &queue, eastl::shared_ptr<VKCommandBufferContext> commandContext)
    {
        mQueue = &queue;
        mDevice = queue.getDevice();
        if (mDevice == nullptr)
        {
            throw makeInvalidArgument("VKCommandEncoder::init could not resolve the owning Vulkan device.");
        }

        mCommandContext = eastl::move(commandContext);
        if (!mCommandContext)
        {
            throw makeRuntimeError("VKCommandEncoder::init could not acquire a pooled Vulkan command context.");
        }

        mCommandBuffer = mCommandContext->commandBuffer;
        mTaskDependencyResolver.init(queue, queue.getResourceStateDB());
        mTransientDescriptorAllocator.init(mDevice);
        mPassSummaryCount = 0u;

        begin();
    }

    VKTaskDependencyResolver &VKCommandEncoder::getTaskDependencyResolver()
    {
        return mTaskDependencyResolver;
    }

    VKTransientDescriptorAllocator &VKCommandEncoder::getTransientDescriptorAllocator()
    {
        return mTransientDescriptorAllocator;
    }

    vk::CommandBuffer VKCommandEncoder::getNativeCommandBuffer() const
    {
        return mCommandBuffer;
    }

    bool VKCommandEncoder::isEnded() const
    {
        return mEnded;
    }

    VKQueue *VKCommandEncoder::getQueue() const
    {
        return mQueue;
    }

    void VKCommandEncoder::begin()
    {
        if (mBegan)
        {
            return;
        }

        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        mCommandBuffer.begin(beginInfo);
        mBegan = true;
    }

    RenderPassEncoder VKCommandEncoder::beginRenderPass(const RenderPassDescriptor &pass)
    {
        beginPass("VKCommandEncoder::beginRenderPass");
        RenderPassEncoder encoder = new VKRenderPassEncoder();
        static_cast<VKRenderPassEncoder *>(encoder.get())->init(mDevice, CommandEncoder(this), pass);
        return encoder;
    }

    BlitPassEncoder VKCommandEncoder::beginBlitPass(const BlitPassDescriptor &pass)
    {
        beginPass("VKCommandEncoder::beginBlitPass");
        BlitPassEncoder encoder = new VKBlitPassEncoder();
        static_cast<VKBlitPassEncoder *>(encoder.get())->init(mDevice, CommandEncoder(this), pass);
        return encoder;
    }

    ComputePassEncoder VKCommandEncoder::beginComputePass(const ComputePassDescriptor &pass)
    {
        beginPass("VKCommandEncoder::beginComputePass");
        ComputePassEncoder encoder = new VKComputePassEncoder();
        static_cast<VKComputePassEncoder *>(encoder.get())->init(mDevice, CommandEncoder(this), pass);
        return encoder;
    }

    void VKCommandEncoder::resolveQuerySet(QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, BufferRange destination)
    {
        ensureRecordable("VKCommandEncoder::resolveQuerySet");
        if (mEnded)
        {
            throw makeRuntimeError("VKCommandEncoder::resolveQuerySet was called after the command encoder had already ended.");
        }
        if (mHasOpenPass)
        {
            throw makeLogicError("VKCommandEncoder::resolveQuerySet cannot be called while a pass encoder is open.");
        }
        if (queryCount == 0u)
        {
            return;
        }
        if (querySet == nullptr)
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet requires a valid query set.");
        }
        auto *vkQuerySet = dynamic_cast<VKQuerySet *>(querySet.get());
        if (vkQuerySet == nullptr)
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet received a non-Vulkan query set.");
        }
        if (vkQuerySet->getType() != QueryType::Timestamp)
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet currently only supports timestamp query sets.");
        }
        if (firstQuery > vkQuerySet->getCount() || queryCount > (vkQuerySet->getCount() - firstQuery))
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet query range exceeds the query set count.");
        }
        if (destination.buffer.isNull())
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet requires a valid destination buffer.");
        }
        if ((destination.offset % sizeof(uint64_t)) != 0u)
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet requires an 8-byte aligned destination offset.");
        }

        auto *destinationBuffer = dynamic_cast<VKBuffer *>(destination.buffer.get());
        if (destinationBuffer == nullptr)
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet received a non-Vulkan destination buffer.");
        }
        if ((destinationBuffer->getUsage() & BufferUsage::QueryResolve) == 0u)
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet destination buffer must be created with BufferUsage::QueryResolve.");
        }

        const uint64_t resolveBytes = static_cast<uint64_t>(queryCount) * sizeof(uint64_t);
        const uint64_t storageSize = destinationBuffer->getStorageSize();
        if (destination.offset > storageSize)
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet destination offset exceeds the buffer size.");
        }
        const uint64_t remainingSize = storageSize - destination.offset;
        const uint64_t destinationSize = destination.size == WholeSize ? remainingSize : destination.size;
        if (destinationSize < resolveBytes || remainingSize < resolveBytes)
        {
            throw makeInvalidArgument("VKCommandEncoder::resolveQuerySet destination buffer range is too small.");
        }

        retainQuerySet(querySet);
        retainBuffer(destination.buffer);
        mTaskDependencyResolver.synchronizeBufferRange(
            mCommandBuffer,
            *destinationBuffer,
            destination.offset,
            resolveBytes,
            vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eTransferWrite);
        mCommandBuffer.copyQueryPoolResults(
            vkQuerySet->getNativeQueryPool(),
            firstQuery,
            queryCount,
            destinationBuffer->getNativeBuffer(),
            destination.offset,
            sizeof(uint64_t),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
    }

    void VKCommandEncoder::end()
    {
        ensureRecordable("VKCommandEncoder::end");
        if (mHasOpenPass)
        {
            throw makeLogicError("VKCommandEncoder::end cannot close a command buffer while a pass encoder is still open.");
        }
        if (mEnded)
        {
            return;
        }

        VKResourceFinalStateBatch finalStateBatch = mTaskDependencyResolver.finalize(mCommandBuffer);
        if (mQueue != nullptr)
        {
            mQueue->getResourceStateDB().registerEndedCommandEncoderState(*this, eastl::move(finalStateBatch));
        }
        mCommandBuffer.end();
        mEnded = true;
    }

    void VKCommandEncoder::markStatePendingSubmit() const
    {
        if (mQueue != nullptr)
        {
            mQueue->getResourceStateDB().markCommandEncoderStatePendingSubmit(*this);
        }
    }

    void VKCommandEncoder::retainBindGroup(BindGroup bindGroup)
    {
        if (bindGroup != nullptr)
        {
            mRetainedBindGroups.push_back(bindGroup);
        }
    }

    void VKCommandEncoder::retainRenderPipeline(RenderPipeline pipeline)
    {
        if (pipeline != nullptr)
        {
            mRetainedRenderPipelines.push_back(pipeline);
        }
    }

    void VKCommandEncoder::retainComputePipeline(ComputePipeline pipeline)
    {
        if (pipeline != nullptr)
        {
            mRetainedComputePipelines.push_back(pipeline);
        }
    }

    void VKCommandEncoder::retainQuerySet(QuerySet querySet)
    {
        if (querySet != nullptr)
        {
            mRetainedQuerySets.push_back(querySet);
        }
    }

    void VKCommandEncoder::retainNativeResource(eastl::shared_ptr<void> resource)
    {
        if (resource)
        {
            mRetainedNativeResources.push_back(eastl::move(resource));
        }
    }

    void VKCommandEncoder::notePassSummary()
    {
        ++mPassSummaryCount;
    }

    size_t VKCommandEncoder::getPassSummaryCount() const
    {
        return mPassSummaryCount;
    }

    size_t VKCommandEncoder::getRetainedBufferCount() const
    {
        return mRetainedBuffers.size();
    }

    size_t VKCommandEncoder::getRetainedTextureCount() const
    {
        return mRetainedTextures.size();
    }

    const eastl::vector<Texture> &VKCommandEncoder::getRetainedTextures() const
    {
        return mRetainedTextures;
    }

    void VKCommandEncoder::retainBuffer(Buffer buffer)
    {
        if (buffer.isNull() || mDevice == nullptr)
        {
            return;
        }

        mDevice->retainPendingCommandBufferReference(buffer);
        mRetainedBuffers.push_back(buffer);
    }

    void VKCommandEncoder::retainTexture(Texture texture)
    {
        if (texture.isNull() || mDevice == nullptr)
        {
            return;
        }

        mDevice->retainPendingCommandTextureReference(texture);
        mRetainedTextures.push_back(texture);
    }

    void VKCommandEncoder::notifyPassEnded()
    {
        mHasOpenPass = false;
    }

    void VKCommandEncoder::writePassTimestamp(const PassTimestampWrites &timestampWrites, bool beginning, vk::PipelineStageFlagBits stage)
    {
        if (timestampWrites.querySet == nullptr)
        {
            return;
        }

        const uint32_t writeIndex = beginning
            ? timestampWrites.beginningOfPassWriteIndex
            : timestampWrites.endOfPassWriteIndex;
        if (writeIndex == QuerySetIndexUndefined)
        {
            return;
        }
        if (timestampWrites.beginningOfPassWriteIndex != QuerySetIndexUndefined &&
            timestampWrites.endOfPassWriteIndex != QuerySetIndexUndefined &&
            timestampWrites.beginningOfPassWriteIndex == timestampWrites.endOfPassWriteIndex)
        {
            throw makeInvalidArgument("VKCommandEncoder::writePassTimestamp requires distinct begin and end query indices.");
        }

        const TimestampQuerySupport support = mDevice != nullptr ? mDevice->getTimestampQuerySupport() : TimestampQuerySupport{};
        if (support.supported == False || support.passTimestampWritesSupported == False)
        {
            throw makeRuntimeError("VKCommandEncoder::writePassTimestamp was called on a queue family without timestamp support.");
        }

        auto *vkQuerySet = dynamic_cast<VKQuerySet *>(timestampWrites.querySet.get());
        if (vkQuerySet == nullptr)
        {
            throw makeInvalidArgument("VKCommandEncoder::writePassTimestamp received a non-Vulkan query set.");
        }
        if (vkQuerySet->getType() != QueryType::Timestamp)
        {
            throw makeInvalidArgument("VKCommandEncoder::writePassTimestamp requires a timestamp query set.");
        }
        const auto validateWriteIndex = [&](uint32_t index, const char *name) {
            if (index != QuerySetIndexUndefined && index >= vkQuerySet->getCount())
            {
                throw makeInvalidArgument(eastl::string("VKCommandEncoder::writePassTimestamp ") + name + " query index exceeds the query set count.");
            }
        };
        validateWriteIndex(timestampWrites.beginningOfPassWriteIndex, "begin");
        validateWriteIndex(timestampWrites.endOfPassWriteIndex, "end");
        if (hasWrittenTimestampQuery(timestampWrites.querySet, writeIndex))
        {
            throw makeInvalidArgument("VKCommandEncoder::writePassTimestamp cannot write the same query index twice in one command encoder.");
        }

        resetPassTimestampQueriesIfNeeded(*vkQuerySet, timestampWrites.querySet, timestampWrites);
        retainQuerySet(timestampWrites.querySet);
        mCommandBuffer.writeTimestamp(stage, vkQuerySet->getNativeQueryPool(), writeIndex);
        mWrittenTimestampQueries.push_back(WrittenTimestampQuery{
            .querySet = timestampWrites.querySet,
            .index = writeIndex,
        });
    }

    void VKCommandEncoder::onSubmitted()
    {
        if (mDevice == nullptr || mSubmitted)
        {
            return;
        }
        mSubmitted = true;
    }

    void VKCommandEncoder::onExecutionCompleted()
    {
        if (mDevice == nullptr || mExecutionCompleted)
        {
            return;
        }

        for (const Buffer &buffer : mRetainedBuffers)
        {
            mDevice->releasePendingCommandBufferReference(buffer);
        }
        for (const Texture &texture : mRetainedTextures)
        {
            mDevice->releasePendingCommandTextureReference(texture);
        }
        mRetainedBuffers.clear();
        mRetainedTextures.clear();
        mRetainedBindGroups.clear();
        mRetainedRenderPipelines.clear();
        mRetainedComputePipelines.clear();
        mRetainedQuerySets.clear();
        mRetainedNativeResources.clear();
        mResetTimestampQueryRanges.clear();
        mWrittenTimestampQueries.clear();
        mPassSummaryCount = 0u;
        mExecutionCompleted = true;
    }

    eastl::shared_ptr<VKCommandBufferContext> VKCommandEncoder::takeCommandBufferContext()
    {
        mCommandBuffer = nullptr;
        return eastl::move(mCommandContext);
    }

    void VKCommandEncoder::ensureRecordable(const char *apiName) const
    {
        if (!mBegan || mDevice == nullptr || mQueue == nullptr)
        {
            throw makeRuntimeError(eastl::string(apiName) + " was called on an uninitialized Vulkan command encoder.");
        }
    }

    void VKCommandEncoder::beginPass(const char *apiName)
    {
        ensureRecordable(apiName);
        if (mEnded)
        {
            throw makeRuntimeError(eastl::string(apiName) + " was called after the command encoder had already ended.");
        }
        if (mHasOpenPass)
        {
            throw makeLogicError(eastl::string(apiName) + " cannot nest pass encoders.");
        }
        mHasOpenPass = true;
    }

    void VKCommandEncoder::resetPassTimestampQueriesIfNeeded(VKQuerySet &vkQuerySet, QuerySet querySet, const PassTimestampWrites &timestampWrites)
    {
        // Vulkan timestamp queries must be reset before writes. RHI keeps that reset implicit.
        uint32_t pendingIndices[2] = {};
        uint32_t pendingIndexCount = 0u;
        const auto addPendingIndex = [&](uint32_t index) {
            if (index == QuerySetIndexUndefined || hasResetTimestampQuery(querySet, index))
            {
                return;
            }
            if (hasWrittenTimestampQuery(querySet, index))
            {
                throw makeInvalidArgument("VKCommandEncoder::resetPassTimestampQueriesIfNeeded cannot reset a timestamp query after it has been written.");
            }
            pendingIndices[pendingIndexCount++] = index;
        };

        addPendingIndex(timestampWrites.beginningOfPassWriteIndex);
        addPendingIndex(timestampWrites.endOfPassWriteIndex);
        if (pendingIndexCount == 0u)
        {
            return;
        }
        if (pendingIndexCount == 2u && pendingIndices[1] < pendingIndices[0])
        {
            const uint32_t tmp = pendingIndices[0];
            pendingIndices[0] = pendingIndices[1];
            pendingIndices[1] = tmp;
        }

        uint32_t rangeFirst = pendingIndices[0];
        uint32_t rangeCount = 1u;
        const auto flushRange = [&]() {
            mCommandBuffer.resetQueryPool(vkQuerySet.getNativeQueryPool(), rangeFirst, rangeCount);
            mResetTimestampQueryRanges.push_back(ResetTimestampQueryRange{
                .querySet = querySet,
                .firstIndex = rangeFirst,
                .count = rangeCount,
            });
        };

        for (uint32_t i = 1u; i < pendingIndexCount; ++i)
        {
            if (pendingIndices[i] == rangeFirst + rangeCount)
            {
                ++rangeCount;
                continue;
            }
            flushRange();
            rangeFirst = pendingIndices[i];
            rangeCount = 1u;
        }
        flushRange();
    }

    bool VKCommandEncoder::hasResetTimestampQuery(QuerySet querySet, uint32_t index) const
    {
        for (const ResetTimestampQueryRange &range : mResetTimestampQueryRanges)
        {
            const uint64_t firstIndex = range.firstIndex;
            const uint64_t endIndex = firstIndex + range.count;
            if (range.querySet == querySet && index >= firstIndex && index < endIndex)
            {
                return true;
            }
        }
        return false;
    }

    bool VKCommandEncoder::hasWrittenTimestampQuery(QuerySet querySet, uint32_t index) const
    {
        for (const WrittenTimestampQuery &writtenQuery : mWrittenTimestampQueries)
        {
            if (writtenQuery.querySet == querySet && writtenQuery.index == index)
            {
                return true;
            }
        }
        return false;
    }
} // namespace GVM::RHI::Vulkan
