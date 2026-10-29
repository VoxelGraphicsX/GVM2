#include "VKCommandEncoder.hpp"
#include "VKInstance.hpp"

#include "VKBlitPassEncoder.hpp"
#include "VKBuffer.hpp"
#include "VKComputePassEncoder.hpp"
#include "VKDevice.hpp"
#include "VKLogging.hpp"
#include "VKQuerySet.hpp"
#include "VKQueue.hpp"
#include "VKRenderPassEncoder.hpp"
#include "VKTexture.hpp"

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

        releaseRetainedResourceReferences();

        if (mQueue != nullptr)
        {
            mQueue->recycleCommandBufferContext(eastl::move(mCommandContext));
        }
    }

    void VKCommandEncoder::init(VKQueue &queue, eastl::shared_ptr<VKCommandBufferContext> commandContext)
    {
        mQueue = &queue;
        mDevice = queue.getDevice();
        if (mDevice == nullptr)
        {
            throw std::invalid_argument("VKCommandEncoder::init could not resolve the owning Vulkan device.");
        }

        mCommandContext = eastl::move(commandContext);
        if (!mCommandContext)
        {
            throw std::runtime_error("VKCommandEncoder::init could not acquire a pooled Vulkan command context.");
        }

        mCommandBuffer = mCommandContext->commandBuffer;
        mDevice->setDebugObjectName(vk::ObjectType::eCommandBuffer, reinterpret_cast<uint64_t>(static_cast<VkCommandBuffer>(mCommandBuffer)), "GVM command buffer");
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

    bool VKCommandEncoder::isSubmitted() const
    {
        return mSubmitted;
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

        mBegan = true;
    }

    RenderPassEncoder VKCommandEncoder::beginRenderPass(const RenderPassDescriptor &pass)
    {
        beginPass("VKCommandEncoder::beginRenderPass");
        RenderPassEncoder encoder = new VKRenderPassEncoder();
        try
        {
            static_cast<VKRenderPassEncoder *>(encoder.get())->init(mDevice, CommandEncoder(this), pass);
        }
        catch (...)
        {
            mHasOpenPass = false;
            throw;
        }
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
            throw std::runtime_error("VKCommandEncoder::resolveQuerySet was called after the command encoder had already ended.");
        }
        if (mHasOpenPass)
        {
            throw std::logic_error("VKCommandEncoder::resolveQuerySet cannot be called while a pass encoder is open.");
        }
        if (queryCount == 0u)
        {
            return;
        }
        if (querySet == nullptr)
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet requires a valid query set.");
        }
        auto *vkQuerySet = dynamic_cast<VKQuerySet *>(querySet.get());
        if (vkQuerySet == nullptr)
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet received a non-Vulkan query set.");
        }
        if (vkQuerySet->getType() != QueryType::Timestamp)
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet currently only supports timestamp query sets.");
        }
        if (firstQuery > vkQuerySet->getCount() || queryCount > (vkQuerySet->getCount() - firstQuery))
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet query range exceeds the query set count.");
        }
        if (destination.buffer.isNull())
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet requires a valid destination buffer.");
        }
        if ((destination.offset % sizeof(uint64_t)) != 0u)
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet requires an 8-byte aligned destination offset.");
        }

        auto *destinationBuffer = dynamic_cast<VKBuffer *>(destination.buffer.get());
        if (destinationBuffer == nullptr)
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet received a non-Vulkan destination buffer.");
        }
        if ((destinationBuffer->getUsage() & BufferUsage::QueryResolve) == 0u)
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet destination buffer must be created with BufferUsage::QueryResolve.");
        }

        const uint64_t resolveBytes = static_cast<uint64_t>(queryCount) * sizeof(uint64_t);
        const uint64_t storageSize = destinationBuffer->getStorageSize();
        if (destination.offset > storageSize)
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet destination offset exceeds the buffer size.");
        }
        const uint64_t remainingSize = storageSize - destination.offset;
        const uint64_t destinationSize = destination.size == WholeSize ? remainingSize : destination.size;
        if (destinationSize < resolveBytes || remainingSize < resolveBytes)
        {
            throw std::invalid_argument("VKCommandEncoder::resolveQuerySet destination buffer range is too small.");
        }

        retainQuerySet(querySet);
        retainBuffer(destination.buffer);
        mCommands.push_back(ResolveQueryCommand{querySet, firstQuery, queryCount, destination});
    }

    void VKCommandEncoder::end()
    {
        ensureRecordable("VKCommandEncoder::end");
        if (mHasOpenPass)
        {
            throw std::logic_error("VKCommandEncoder::end cannot close a command buffer while a pass encoder is still open.");
        }
        if (mEnded)
        {
            return;
        }

        mEnded = true;
    }

    void VKCommandEncoder::appendPass(RenderPassEncoder pass) { mCommands.push_back(pass); }
    void VKCommandEncoder::appendPass(ComputePassEncoder pass) { mCommands.push_back(pass); }
    void VKCommandEncoder::appendPass(BlitPassEncoder pass) { mCommands.push_back(pass); }

    void VKCommandEncoder::encodeNative(Texture presentTexture)
    {
        if (!mEnded || mSubmitted)
        {
            throw std::logic_error("Only a closed, unsubmitted command encoder can be lowered.");
        }
        if (mNativeEncoded)
        {
            return;
        }
        mTaskDependencyResolver.init(*mQueue, mQueue->getResourceStateDB());
        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        mCommandBuffer.begin(beginInfo);
        for (const auto &command : mCommands)
        {
            if (const auto *pass = eastl::get_if<RenderPassEncoder>(&command))
            {
                static_cast<VKRenderPassEncoder *>(pass->get())->encode(*this);
            }
            else if (const auto *pass = eastl::get_if<ComputePassEncoder>(&command))
            {
                static_cast<VKComputePassEncoder *>(pass->get())->encode(*this);
            }
            else if (const auto *pass = eastl::get_if<BlitPassEncoder>(&command))
            {
                static_cast<VKBlitPassEncoder *>(pass->get())->encode(*this);
            }
            else if (const auto *reset = eastl::get_if<ResetTimestampQueryRange>(&command))
            {
                mCommandBuffer.resetQueryPool(static_cast<VKQuerySet *>(reset->querySet.get())->getNativeQueryPool(), reset->firstIndex, reset->count);
            }
            else if (const auto *timestamp = eastl::get_if<TimestampCommand>(&command))
            {
                mCommandBuffer.writeTimestamp2(timestamp->stage, static_cast<VKQuerySet *>(timestamp->querySet.get())->getNativeQueryPool(), timestamp->index);
            }
            else
            {
                const auto &resolve = eastl::get<ResolveQueryCommand>(command);
                auto &buffer = *static_cast<VKBuffer *>(resolve.destination.buffer.get());
                mTaskDependencyResolver.synchronizeBufferRange(mCommandBuffer, buffer, resolve.destination.offset,
                    uint64_t(resolve.count) * sizeof(uint64_t), vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite);
                mCommandBuffer.copyQueryPoolResults(static_cast<VKQuerySet *>(resolve.querySet.get())->getNativeQueryPool(),
                    resolve.first, resolve.count, buffer.getNativeBuffer(), resolve.destination.offset, sizeof(uint64_t),
                    vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
            }
        }
        if (!presentTexture.isNull())
        {
            mTaskDependencyResolver.synchronizeTextureSubresources(mCommandBuffer, *static_cast<VKTexture *>(presentTexture.get()), vk::ImageLayout::ePresentSrcKHR, {}, {});
        }
        mTaskDependencyResolver.finalize(mCommandBuffer);
        mCommandBuffer.end();
        mNativeEncoded = true;
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

    void VKCommandEncoder::releaseRetainedResourceReferences()
    {
        for (const Buffer &buffer : mRetainedBuffers)
        {
            auto *bufferImpl = static_cast<VKBuffer *>(buffer.get());
            if (bufferImpl != nullptr)
            {
                bufferImpl->releaseReference();
            }
        }
        for (const Texture &texture : mRetainedTextures)
        {
            auto *textureImpl = static_cast<VKTexture *>(texture.get());
            if (textureImpl != nullptr)
            {
                textureImpl->releaseReference();
            }
        }
    }

    void VKCommandEncoder::retainBuffer(Buffer buffer)
    {
        if (buffer.isNull() || mDevice == nullptr)
        {
            return;
        }

        static_cast<VKBuffer *>(buffer.get())->retainReference();
        mRetainedBuffers.push_back(buffer);
    }

    void VKCommandEncoder::retainTexture(Texture texture)
    {
        if (texture.isNull() || mDevice == nullptr)
        {
            return;
        }

        static_cast<VKTexture *>(texture.get())->retainReference();
        mRetainedTextures.push_back(texture);
    }

    void VKCommandEncoder::notifyPassEnded()
    {
        mHasOpenPass = false;
    }

    void VKCommandEncoder::writePassTimestamp(const PassTimestampWrites &timestampWrites, bool beginning, vk::PipelineStageFlagBits2 stage)
    {
        if (timestampWrites.querySet == nullptr)
        {
            return;
        }

        const uint32_t writeIndex = beginning
            ? timestampWrites.beginningOfPassWriteIndex : timestampWrites.endOfPassWriteIndex;
        if (timestampWrites.beginningOfPassWriteIndex == QuerySetIndexUndefined && timestampWrites.endOfPassWriteIndex == QuerySetIndexUndefined)
        {
            return;
        }
        if (writeIndex != QuerySetIndexUndefined && hasWrittenTimestampQuery(timestampWrites.querySet, writeIndex))
        {
            throw std::invalid_argument("VKCommandEncoder::writePassTimestamp cannot write the same query index twice in one command encoder.");
        }
        if (beginning)
        {
            if (timestampWrites.beginningOfPassWriteIndex == timestampWrites.endOfPassWriteIndex)
            {
                throw std::invalid_argument("Pass timestamp begin and end indices must be distinct.");
            }
            const TimestampQuerySupport support = mDevice->getTimestampQuerySupport();
            if (support.supported == False || support.passTimestampWritesSupported == False)
            {
                throw std::runtime_error("Pass timestamps are unsupported on the selected queue family.");
            }
            auto *querySet = dynamic_cast<VKQuerySet *>(timestampWrites.querySet.get());
            if (querySet == nullptr || querySet->getType() != QueryType::Timestamp)
            {
                throw std::invalid_argument("Pass timestamps require a Vulkan timestamp query set.");
            }
            const uint32_t indices[] = {timestampWrites.beginningOfPassWriteIndex, timestampWrites.endOfPassWriteIndex};
            for (uint32_t index : indices)
            {
                if (index != QuerySetIndexUndefined && index >= querySet->getCount())
                {
                    throw std::invalid_argument("Pass timestamp index exceeds the query set count.");
                }
            }
            resetPassTimestampQueriesIfNeeded(*querySet, timestampWrites.querySet, timestampWrites);
        }
        if (writeIndex == QuerySetIndexUndefined)
        {
            return;
        }

        retainQuerySet(timestampWrites.querySet);
        mCommands.push_back(TimestampCommand{timestampWrites.querySet, writeIndex, stage});
        mWrittenTimestampQueries.push_back(WrittenTimestampQuery{
            .querySet = timestampWrites.querySet.get(),
            .index = writeIndex,
        });
    }

    VKArgumentAllocation VKCommandEncoder::allocateArguments(Arguments arguments)
    {
        ensureRecordable("VKCommandEncoder::allocateArguments");
        if (mArgumentStorage == nullptr)
        {
            mArgumentStorage = eastl::make_unique<VKArgumentStorage>(*mDevice);
        }
        return mArgumentStorage->allocate(eastl::move(arguments));
    }

    void VKCommandEncoder::onSubmitted()
    {
        if (mDevice == nullptr || mSubmitted)
        {
            return;
        }
        mSubmitted = true;
        if (mArgumentStorage != nullptr)
        {
            mArgumentStorage->markSubmitted();
        }
    }

    void VKCommandEncoder::onExecutionCompleted()
    {
        if (mDevice == nullptr || mExecutionCompleted)
        {
            return;
        }

        releaseRetainedResourceReferences();
        mCommands.clear();
        mRetainedBuffers.clear();
        mRetainedTextures.clear();
        mRetainedBindGroups.clear();
        mRetainedRenderPipelines.clear();
        mRetainedComputePipelines.clear();
        mRetainedQuerySets.clear();
        mRetainedNativeResources.clear();
        if (mArgumentStorage != nullptr)
        {
            mArgumentStorage->complete();
        }
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
            throw std::runtime_error((eastl::string(apiName) + " was called on an uninitialized Vulkan command encoder.").c_str());
        }
    }

    void VKCommandEncoder::beginPass(const char *apiName)
    {
        ensureRecordable(apiName);
        if (mEnded)
        {
            throw std::runtime_error((eastl::string(apiName) + " was called after the command encoder had already ended.").c_str());
        }
        if (mHasOpenPass)
        {
            throw std::logic_error((eastl::string(apiName) + " cannot nest pass encoders.").c_str());
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
                throw std::invalid_argument("VKCommandEncoder::resetPassTimestampQueriesIfNeeded cannot reset a timestamp query after it has been written.");
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
            mCommands.push_back(ResetTimestampQueryRange{querySet, rangeFirst, rangeCount});
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

    bool VKCommandEncoder::hasResetTimestampQuery(const QuerySet &querySet, uint32_t index) const
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

    bool VKCommandEncoder::hasWrittenTimestampQuery(const QuerySet &querySet, uint32_t index) const
    {
        for (const WrittenTimestampQuery &writtenQuery : mWrittenTimestampQueries)
        {
            if (writtenQuery.querySet == querySet.get() && writtenQuery.index == index)
            {
                return true;
            }
        }
        return false;
    }
} // namespace GVM::RHI::Vulkan
