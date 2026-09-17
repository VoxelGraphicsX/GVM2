#include "VKTaskDependencyResolver.hpp"

#include "VKBuffer.hpp"
#include "VKCommandEncoder.hpp"
#include "VKBufferRangeStateUtils.hpp"
#include "VKTaskDependencyResolver.Diagnostics.hpp"
#include "VKQueue.hpp"
#include "VKResourceStateDB.hpp"
#include "VKSyncUtils.hpp"
#include "VKTexture.hpp"
#include "VKTextureSubresourceUtils.hpp"

#include <EASTL/algorithm.h>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        TextureAspectFlags resolvePlaneAspect(vk::ImageAspectFlagBits aspect)
        {
            switch (aspect)
            {
            case vk::ImageAspectFlagBits::eDepth: return TextureAspect::DepthOnly;
            case vk::ImageAspectFlagBits::eStencil: return TextureAspect::StencilOnly;
            default: return TextureAspect::All;
            }
        }

        bool trackerBufferStatePayloadMatches(
            const VKResourceBufferState &lhs,
            const VKResourceBufferState &rhs)
        {
            return lhs.stageMask == rhs.stageMask &&
                lhs.accessMask == rhs.accessMask &&
                lhs.lastWriteStageMask == rhs.lastWriteStageMask &&
                lhs.lastWriteAccessMask == rhs.lastWriteAccessMask &&
                lhs.initialized == rhs.initialized &&
                lhs.lastWriteInitialized == rhs.lastWriteInitialized &&
                lhs.initialSource == rhs.initialSource;
        }

        void recordPipelineBarrierBatch(
            vk::CommandBuffer commandBuffer,
            vk::PipelineStageFlags srcStageMask,
            vk::PipelineStageFlags dstStageMask,
            const eastl::vector<vk::MemoryBarrier> *memoryBarriers,
            const eastl::vector<vk::BufferMemoryBarrier> *bufferBarriers,
            const eastl::vector<vk::ImageMemoryBarrier> *imageBarriers)
        {
            static const eastl::vector<vk::MemoryBarrier> EmptyMemoryBarriers;
            static const eastl::vector<vk::BufferMemoryBarrier> EmptyBufferBarriers;
            static const eastl::vector<vk::ImageMemoryBarrier> EmptyImageBarriers;

            const eastl::vector<vk::MemoryBarrier> &resolvedMemoryBarriers =
                memoryBarriers != nullptr ? *memoryBarriers : EmptyMemoryBarriers;
            const eastl::vector<vk::BufferMemoryBarrier> &resolvedBufferBarriers =
                bufferBarriers != nullptr ? *bufferBarriers : EmptyBufferBarriers;
            const eastl::vector<vk::ImageMemoryBarrier> &resolvedImageBarriers =
                imageBarriers != nullptr ? *imageBarriers : EmptyImageBarriers;

            if (resolvedMemoryBarriers.empty() &&
                resolvedBufferBarriers.empty() &&
                resolvedImageBarriers.empty())
            {
                return;
            }

            commandBuffer.pipelineBarrier(
                srcStageMask,
                dstStageMask,
                {},
                resolvedMemoryBarriers,
                resolvedBufferBarriers,
                resolvedImageBarriers);
        }

        void accumulateTextureBarrierMetrics(
            TaskDependencyResolverDiagnostics &diagnostics,
            const vk::ImageMemoryBarrier &barrier,
            vk::PipelineStageFlags batchSrcStageMask,
            vk::PipelineStageFlags batchDstStageMask)
        {
            const vk::ImageSubresourceRange &range = barrier.subresourceRange;
            const uint32_t subresourceCount = range.levelCount * range.layerCount;
            const bool layoutChange = barrier.oldLayout != barrier.newLayout;
            const bool stageChange = batchSrcStageMask != batchDstStageMask;
            const bool accessChange = barrier.srcAccessMask != barrier.dstAccessMask;
            const bool writeInvolved =
                !isReadOnlyAccess(barrier.srcAccessMask) || !isReadOnlyAccess(barrier.dstAccessMask);

            ++diagnostics.textureBarrierCount;
            diagnostics.textureBarrierSubresourceCount += subresourceCount;
            if (layoutChange)
            {
                ++diagnostics.textureLayoutBarrierCount;
            }
            if (stageChange)
            {
                ++diagnostics.textureStageBarrierCount;
            }
            if (accessChange)
            {
                ++diagnostics.textureAccessBarrierCount;
            }
            if (writeInvolved)
            {
                ++diagnostics.textureWriteBarrierCount;
            }
        }

        bool canMergeTextureBarrierRange(
            const vk::ImageMemoryBarrier &lhs,
            const vk::ImageMemoryBarrier &rhs)
        {
            const vk::ImageSubresourceRange &lhsRange = lhs.subresourceRange;
            const vk::ImageSubresourceRange &rhsRange = rhs.subresourceRange;
            return lhs.image == rhs.image &&
                lhs.oldLayout == rhs.oldLayout &&
                lhs.newLayout == rhs.newLayout &&
                lhs.srcAccessMask == rhs.srcAccessMask &&
                lhs.dstAccessMask == rhs.dstAccessMask &&
                lhsRange.aspectMask == rhsRange.aspectMask &&
                lhsRange.baseArrayLayer == rhsRange.baseArrayLayer &&
                lhsRange.layerCount == rhsRange.layerCount &&
                lhsRange.baseMipLevel + lhsRange.levelCount == rhsRange.baseMipLevel;
        }

        void flushTextureBarrierBatch(
            vk::CommandBuffer commandBuffer,
            TaskDependencyResolverDiagnostics &diagnostics,
            vk::PipelineStageFlags &batchSrcStageMask,
            vk::PipelineStageFlags &batchDstStageMask,
            eastl::vector<vk::ImageMemoryBarrier> &imageBarriers)
        {
            if (imageBarriers.empty())
            {
                return;
            }

            for (const vk::ImageMemoryBarrier &barrier : imageBarriers)
            {
                accumulateTextureBarrierMetrics(
                    diagnostics,
                    barrier,
                    batchSrcStageMask,
                    batchDstStageMask);
            }

            recordPipelineBarrierBatch(
                commandBuffer,
                batchSrcStageMask,
                batchDstStageMask,
                nullptr,
                nullptr,
                &imageBarriers);
            imageBarriers.clear();
        }

    } // namespace

    void VKTaskDependencyResolver::reset()
    {
        mBufferStates.clear();
        mTextureStates.clear();
        mDiagnostics.reset();
        mQueue = nullptr;
        mStateDB = nullptr;
    }

    void VKTaskDependencyResolver::init(const VKQueue &queue, VKResourceStateDB &stateDB)
    {
        reset();
        mQueue = &queue;
        mStateDB = &stateDB;
    }

    void VKTaskDependencyResolver::synchronizeBufferRange(
        vk::CommandBuffer commandBuffer,
        VKBuffer &buffer,
        uint64_t offset,
        uint64_t size,
        vk::PipelineStageFlags requiredStageMask,
        vk::AccessFlags requiredAccessMask)
    {
        eastl::vector<PendingBufferBarrier> pendingBufferBarriers;
        pendingBufferBarriers.reserve(1u);
        uint64_t outstandingWriteConsumedCount = 0u;
        if (collectBufferRangeSyncBarriers(
                BufferRangeSyncRequest{
                    .buffer = &buffer,
                    .offset = offset,
                    .size = size,
                    .requiredStageMask = requiredStageMask,
                    .requiredAccessMask = requiredAccessMask,
                },
                pendingBufferBarriers,
                outstandingWriteConsumedCount))
        {
            ++mDiagnostics.bufferPrepareCalls;
            mDiagnostics.bufferOutstandingWriteConsumedCount += outstandingWriteConsumedCount;
        }
        flushPendingBufferBarriers(commandBuffer, pendingBufferBarriers);
    }

    void VKTaskDependencyResolver::synchronizeBufferRanges(
        vk::CommandBuffer commandBuffer,
        const eastl::vector<BufferRangeSyncRequest> &requests)
    {
        if (requests.empty())
        {
            return;
        }

        eastl::vector<PendingBufferBarrier> pendingBufferBarriers;
        pendingBufferBarriers.reserve(requests.size());
        eastl::vector<PendingBufferBarrier> requestBarriers;
        uint64_t outstandingWriteConsumedCount = 0u;

        for (const BufferRangeSyncRequest &request : requests)
        {
            requestBarriers.clear();
            uint64_t requestOutstandingWriteConsumedCount = 0u;
            if (!collectBufferRangeSyncBarriers(request, requestBarriers, requestOutstandingWriteConsumedCount))
            {
                continue;
            }

            ++mDiagnostics.bufferPrepareCalls;
            outstandingWriteConsumedCount += requestOutstandingWriteConsumedCount;
            if (requestBarriers.empty())
            {
                continue;
            }

            if (containsOverlappingPendingBufferBarrier(pendingBufferBarriers, requestBarriers))
            {
                flushPendingBufferBarriers(commandBuffer, pendingBufferBarriers);
            }

            for (PendingBufferBarrier &requestBarrier : requestBarriers)
            {
                pendingBufferBarriers.push_back(eastl::move(requestBarrier));
            }
        }

        mDiagnostics.bufferOutstandingWriteConsumedCount += outstandingWriteConsumedCount;
        flushPendingBufferBarriers(commandBuffer, pendingBufferBarriers);
    }

    bool VKTaskDependencyResolver::containsOverlappingPendingBufferBarrier(
        const eastl::vector<PendingBufferBarrier> &existingBarriers,
        const eastl::vector<PendingBufferBarrier> &incomingBarriers)
    {
        for (const PendingBufferBarrier &incomingBarrier : incomingBarriers)
        {
            for (const PendingBufferBarrier &existingBarrier : existingBarriers)
            {
                if (existingBarrier.buffer != incomingBarrier.buffer)
                {
                    continue;
                }

                if (bufferRangesOverlap(
                        existingBarrier.barrier.offset,
                        existingBarrier.barrier.size,
                        incomingBarrier.barrier.offset,
                        incomingBarrier.barrier.size))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool VKTaskDependencyResolver::collectBufferRangeSyncBarriers(
        const BufferRangeSyncRequest &request,
        eastl::vector<PendingBufferBarrier> &outPendingBarriers,
        uint64_t &outOutstandingWriteConsumedCount)
    {
        VKBuffer *buffer = request.buffer;
        const vk::PipelineStageFlags requiredStageMask = request.requiredStageMask;
        const vk::AccessFlags requiredAccessMask = request.requiredAccessMask;
        if (buffer == nullptr)
        {
            throw makeLogicError("VKTaskDependencyResolver::synchronizeBufferRange received a null Vulkan buffer.");
        }
        if (buffer->getNativeBuffer() == vk::Buffer{})
        {
            const eastl::string label = buffer->getLabelName().empty() ? eastl::string("<unnamed>") : eastl::string(buffer->getLabelName().c_str());
            throw makeLogicError("VKTaskDependencyResolver::synchronizeBufferRange encountered destroyed or uninitialized Vulkan buffer '" + label + "'.");
        }

        const uint64_t offset = request.offset;
        const uint64_t size = resolveBufferRangeSize(
            "VKTaskDependencyResolver::collectBufferRangeSyncBarriers",
            buffer,
            request.offset,
            request.size);
        const bool dstReadOnly = isReadOnlyAccess(requiredAccessMask);
        const vk::AccessFlags dstWriteAccessMask = extractWriteAccessMask(requiredAccessMask);
        if (size == 0u)
        {
            return false;
        }

        eastl::vector<VKResourceBufferState> &bufferStates = getOrCreateBufferStates(*buffer);

        splitBufferRangeStates(bufferStates, offset);
        splitBufferRangeStates(bufferStates, offset + size);
        for (VKResourceBufferState &state : bufferStates)
        {
            if (!bufferRangesOverlap(state.offset, state.size, offset, size))
            {
                continue;
            }

            const bool hasTrackedState = state.initialized;
            const bool outstandingWrite = state.lastWriteInitialized && state.lastWriteAccessMask != vk::AccessFlags{};
            const bool srcReadOnly = isReadOnlyAccess(state.accessMask);
            const bool readOnlyToReadOnly = hasTrackedState &&
                srcReadOnly &&
                dstReadOnly &&
                !outstandingWrite;
            const bool writeInvolved = hasTrackedState &&
                (!srcReadOnly || !dstReadOnly || outstandingWrite);

            if (!hasTrackedState)
            {
                state.stageMask = requiredStageMask;
                state.accessMask = requiredAccessMask;
                state.initialized = true;
                state.lastWriteAccessMask = dstWriteAccessMask;
                if (dstWriteAccessMask == vk::AccessFlags{})
                {
                    state.lastWriteStageMask = vk::PipelineStageFlags{};
                }
                else
                {
                    state.lastWriteStageMask = requiredStageMask;
                }
                state.lastWriteInitialized = dstWriteAccessMask != vk::AccessFlags{};
            }
            else if (readOnlyToReadOnly)
            {
                state.stageMask |= requiredStageMask;
                state.accessMask |= requiredAccessMask;
            }
            else
            {
                if (state.stageMask != requiredStageMask || state.accessMask != requiredAccessMask || writeInvolved)
                {
                    vk::PipelineStageFlags srcStageMask = state.stageMask;
                    vk::AccessFlags srcAccessMask = state.accessMask;
                    const bool reasonStageChange = state.stageMask != requiredStageMask;
                    const bool reasonAccessChange = state.accessMask != requiredAccessMask;
                    const bool suspiciousRepeatedReadBarrier =
                        outstandingWrite &&
                        dstReadOnly &&
                        !reasonStageChange &&
                        !reasonAccessChange;
                    if (outstandingWrite)
                    {
                        srcStageMask |= state.lastWriteStageMask;
                        srcAccessMask |= state.lastWriteAccessMask;
                    }

                    vk::BufferMemoryBarrier barrier = {};
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.buffer = buffer->getNativeBuffer();
                    barrier.offset = state.offset;
                    barrier.size = state.size;
                    barrier.srcAccessMask = srcAccessMask;
                    barrier.dstAccessMask = requiredAccessMask;

                    PendingBufferBarrier pendingBarrier = {};
                    pendingBarrier.buffer = buffer;
                    pendingBarrier.barrier = barrier;
                    pendingBarrier.srcStageMask = normalizeStageMask(srcStageMask);
                    pendingBarrier.dstStageMask = normalizeStageMask(requiredStageMask);
                    pendingBarrier.writeInvolved = writeInvolved;
                    pendingBarrier.outstandingWrite = outstandingWrite;
                    pendingBarrier.suspiciousRepeatedReadBarrier = suspiciousRepeatedReadBarrier;
                    outPendingBarriers.push_back(pendingBarrier);
                }

                state.stageMask = requiredStageMask;
                state.accessMask = requiredAccessMask;
                state.initialized = true;
                if (dstWriteAccessMask != vk::AccessFlags{})
                {
                    state.lastWriteStageMask = requiredStageMask;
                    state.lastWriteAccessMask = dstWriteAccessMask;
                    state.lastWriteInitialized = true;
                }
                else if (outstandingWrite && dstReadOnly)
                {
                    state.lastWriteStageMask = vk::PipelineStageFlags{};
                    state.lastWriteAccessMask = vk::AccessFlags{};
                    state.lastWriteInitialized = false;
                    ++outOutstandingWriteConsumedCount;
                }
            }
            state.initialSource = VKResourceBufferStateInitialSource::CommandLocal;
        }

        mergeAdjacentBufferRangeStates(bufferStates, trackerBufferStatePayloadMatches);
        return true;
    }

    void VKTaskDependencyResolver::flushPendingBufferBarriers(
        vk::CommandBuffer commandBuffer,
        eastl::vector<PendingBufferBarrier> &pendingBufferBarriers)
    {
        if (pendingBufferBarriers.empty())
        {
            return;
        }

        eastl::vector<vk::MemoryBarrier> memoryBarriers;
        eastl::vector<vk::BufferMemoryBarrier> bufferBarriers;
        memoryBarriers.reserve(pendingBufferBarriers.size());
        bufferBarriers.reserve(pendingBufferBarriers.size());

        coalescePendingBufferBarriers(pendingBufferBarriers);
        for (const PendingBufferBarrier &pendingBarrier : pendingBufferBarriers)
        {
            ++mDiagnostics.bufferBarrierCount;
            if (pendingBarrier.writeInvolved)
            {
                ++mDiagnostics.bufferWriteBarrierCount;
            }
            if (pendingBarrier.outstandingWrite)
            {
                ++mDiagnostics.bufferOutstandingWriteBarrierCount;
            }
            if (pendingBarrier.suspiciousRepeatedReadBarrier)
            {
                ++mDiagnostics.bufferSuspiciousRepeatedReadBarrierCount;
            }
        }

        eastl::sort(
            pendingBufferBarriers.begin(),
            pendingBufferBarriers.end(),
            &VKTaskDependencyResolver::pendingBufferBarrierBatchLess);

        size_t batchStartIndex = 0u;
        while (batchStartIndex < pendingBufferBarriers.size())
        {
            const vk::PipelineStageFlags batchSrcStageMask = pendingBufferBarriers[batchStartIndex].srcStageMask;
            const vk::PipelineStageFlags batchDstStageMask = pendingBufferBarriers[batchStartIndex].dstStageMask;
            size_t batchEndIndex = batchStartIndex + 1u;
            while (batchEndIndex < pendingBufferBarriers.size() &&
                   pendingBufferBarriers[batchEndIndex].srcStageMask == batchSrcStageMask &&
                   pendingBufferBarriers[batchEndIndex].dstStageMask == batchDstStageMask)
            {
                ++batchEndIndex;
            }

            memoryBarriers.clear();
            bufferBarriers.clear();

            size_t accessStartIndex = batchStartIndex;
            while (accessStartIndex < batchEndIndex)
            {
                const vk::AccessFlags batchSrcAccessMask = pendingBufferBarriers[accessStartIndex].barrier.srcAccessMask;
                const vk::AccessFlags batchDstAccessMask = pendingBufferBarriers[accessStartIndex].barrier.dstAccessMask;
                size_t accessEndIndex = accessStartIndex + 1u;
                while (accessEndIndex < batchEndIndex &&
                       pendingBufferBarriers[accessEndIndex].barrier.srcAccessMask == batchSrcAccessMask &&
                       pendingBufferBarriers[accessEndIndex].barrier.dstAccessMask == batchDstAccessMask)
                {
                    ++accessEndIndex;
                }

                const size_t groupSize = accessEndIndex - accessStartIndex;
                if (TaskDependencyResolverDiagnosticUtils::shouldPromoteBufferBarrierGroupToMemoryBarrier(
                        batchSrcStageMask,
                        batchDstStageMask,
                        batchSrcAccessMask,
                        batchDstAccessMask,
                        groupSize))
                {
                    vk::MemoryBarrier memoryBarrier = {};
                    memoryBarrier.srcAccessMask = batchSrcAccessMask;
                    memoryBarrier.dstAccessMask = batchDstAccessMask;
                    memoryBarriers.push_back(memoryBarrier);
                    ++mDiagnostics.bufferGlobalMemoryBarrierCount;
                    mDiagnostics.bufferPromotedBarrierCount += groupSize;
                }
                else
                {
                    for (size_t bufferBarrierIndex = accessStartIndex; bufferBarrierIndex < accessEndIndex; ++bufferBarrierIndex)
                    {
                        bufferBarriers.push_back(pendingBufferBarriers[bufferBarrierIndex].barrier);
                    }
                }

                accessStartIndex = accessEndIndex;
            }

            recordPipelineBarrierBatch(
                commandBuffer,
                batchSrcStageMask,
                batchDstStageMask,
                &memoryBarriers,
                &bufferBarriers,
                nullptr);
            ++mDiagnostics.bufferPipelineBarrierCallCount;
            batchStartIndex = batchEndIndex;
        }

        pendingBufferBarriers.clear();
    }

    void VKTaskDependencyResolver::coalescePendingBufferBarriers(eastl::vector<PendingBufferBarrier> &pendingBufferBarriers)
    {
        if (pendingBufferBarriers.size() <= 1u)
        {
            return;
        }

        size_t writeIndex = 0u;
        for (size_t readIndex = 0u; readIndex < pendingBufferBarriers.size(); ++readIndex)
        {
            PendingBufferBarrier &pendingBarrier = pendingBufferBarriers[readIndex];
            if (writeIndex > 0u)
            {
                PendingBufferBarrier &last = pendingBufferBarriers[writeIndex - 1u];
                const bool canCoalesce =
                    last.buffer == pendingBarrier.buffer &&
                    last.srcStageMask == pendingBarrier.srcStageMask &&
                    last.dstStageMask == pendingBarrier.dstStageMask &&
                    last.barrier.srcAccessMask == pendingBarrier.barrier.srcAccessMask &&
                    last.barrier.dstAccessMask == pendingBarrier.barrier.dstAccessMask &&
                    last.writeInvolved == pendingBarrier.writeInvolved &&
                    last.outstandingWrite == pendingBarrier.outstandingWrite &&
                    last.suspiciousRepeatedReadBarrier == pendingBarrier.suspiciousRepeatedReadBarrier &&
                    last.barrier.offset + last.barrier.size == pendingBarrier.barrier.offset;
                if (canCoalesce)
                {
                    last.barrier.size += pendingBarrier.barrier.size;
                    continue;
                }
            }

            if (writeIndex != readIndex)
            {
                pendingBufferBarriers[writeIndex] = eastl::move(pendingBufferBarriers[readIndex]);
            }
            ++writeIndex;
        }

        pendingBufferBarriers.resize(writeIndex);
    }

    bool VKTaskDependencyResolver::pendingBufferBarrierBatchLess(
        const PendingBufferBarrier &lhs,
        const PendingBufferBarrier &rhs)
    {
        const uint64_t lhsSrcStageMaskBits = TaskDependencyResolverDiagnosticUtils::stageMaskBits(lhs.srcStageMask);
        const uint64_t rhsSrcStageMaskBits = TaskDependencyResolverDiagnosticUtils::stageMaskBits(rhs.srcStageMask);
        if (lhsSrcStageMaskBits != rhsSrcStageMaskBits)
        {
            return lhsSrcStageMaskBits < rhsSrcStageMaskBits;
        }

        const uint64_t lhsDstStageMaskBits = TaskDependencyResolverDiagnosticUtils::stageMaskBits(lhs.dstStageMask);
        const uint64_t rhsDstStageMaskBits = TaskDependencyResolverDiagnosticUtils::stageMaskBits(rhs.dstStageMask);
        if (lhsDstStageMaskBits != rhsDstStageMaskBits)
        {
            return lhsDstStageMaskBits < rhsDstStageMaskBits;
        }

        const uint64_t lhsSrcAccessMaskBits = TaskDependencyResolverDiagnosticUtils::accessMaskBits(lhs.barrier.srcAccessMask);
        const uint64_t rhsSrcAccessMaskBits = TaskDependencyResolverDiagnosticUtils::accessMaskBits(rhs.barrier.srcAccessMask);
        if (lhsSrcAccessMaskBits != rhsSrcAccessMaskBits)
        {
            return lhsSrcAccessMaskBits < rhsSrcAccessMaskBits;
        }

        const uint64_t lhsDstAccessMaskBits = TaskDependencyResolverDiagnosticUtils::accessMaskBits(lhs.barrier.dstAccessMask);
        const uint64_t rhsDstAccessMaskBits = TaskDependencyResolverDiagnosticUtils::accessMaskBits(rhs.barrier.dstAccessMask);
        if (lhsDstAccessMaskBits != rhsDstAccessMaskBits)
        {
            return lhsDstAccessMaskBits < rhsDstAccessMaskBits;
        }

        if (lhs.buffer != rhs.buffer)
        {
            return reinterpret_cast<uintptr_t>(lhs.buffer) < reinterpret_cast<uintptr_t>(rhs.buffer);
        }

        if (lhs.barrier.offset != rhs.barrier.offset)
        {
            return lhs.barrier.offset < rhs.barrier.offset;
        }

        return lhs.barrier.size < rhs.barrier.size;
    }

    void VKTaskDependencyResolver::appendPendingTextureBarrierRange(
        const VKTexture &texture,
        vk::ImageAspectFlagBits planeAspect,
        uint32_t mipLevel,
        bool &hasActiveBarrierRange,
        uint32_t activeBarrierRangeBaseLayer,
        uint32_t activeBarrierRangeLayerEnd,
        vk::ImageLayout activeBarrierRangeOldLayout,
        vk::PipelineStageFlags activeBarrierRangeSrcStageMask,
        vk::AccessFlags activeBarrierRangeSrcAccessMask,
        vk::ImageLayout targetLayout,
        vk::PipelineStageFlags requiredStageMask,
        vk::AccessFlags requiredAccessMask,
        eastl::vector<PendingTextureBarrier> &outPendingBarriers) const
    {
        if (!hasActiveBarrierRange || activeBarrierRangeLayerEnd <= activeBarrierRangeBaseLayer)
        {
            return;
        }

        PendingTextureBarrier pendingBarrier = {};
        pendingBarrier.texture = &texture;
        pendingBarrier.srcStageMask = normalizeStageMask(activeBarrierRangeSrcStageMask);
        pendingBarrier.dstStageMask = normalizeStageMask(requiredStageMask);
        pendingBarrier.barrier.oldLayout = activeBarrierRangeOldLayout;
        pendingBarrier.barrier.newLayout = targetLayout;
        pendingBarrier.barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pendingBarrier.barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pendingBarrier.barrier.image = texture.getNativeImage();
        pendingBarrier.barrier.subresourceRange = buildTextureSubresourceRange(
            &texture,
            resolvePlaneAspect(planeAspect),
            mipLevel,
            1u,
            activeBarrierRangeBaseLayer,
            activeBarrierRangeLayerEnd - activeBarrierRangeBaseLayer);
        pendingBarrier.barrier.srcAccessMask = activeBarrierRangeSrcAccessMask;
        pendingBarrier.barrier.dstAccessMask = requiredAccessMask;
        outPendingBarriers.push_back(eastl::move(pendingBarrier));
        hasActiveBarrierRange = false;
    }

    void VKTaskDependencyResolver::collectTextureSubresourceSyncBarriers(
        VKTexture &texture,
        VKResourceTextureState &state,
        const ResolvedTextureSubresourceRange &resolvedSubresources,
        vk::ImageLayout targetLayout,
        vk::PipelineStageFlags requiredStageMask,
        vk::AccessFlags requiredAccessMask,
        eastl::vector<PendingTextureBarrier> &outPendingBarriers)
    {
        for (size_t planeIndex = 0u; planeIndex < state.planeAspects.size(); ++planeIndex)
        {
            const vk::ImageAspectFlagBits planeAspect = state.planeAspects[planeIndex];
            if (!texturePlaneMatches(resolvedSubresources, planeAspect))
            {
                continue;
            }

            const size_t planeOffset = planeIndex * static_cast<size_t>(state.subresourceCountPerPlane);
            for (uint32_t mipLevel = resolvedSubresources.baseMipLevel; mipLevel < resolvedSubresources.mipLevelEnd(); ++mipLevel)
            {
                bool hasActiveBarrierRange = false;
                uint32_t activeBarrierRangeBaseLayer = resolvedSubresources.baseArrayLayer;
                vk::ImageLayout activeBarrierRangeOldLayout = vk::ImageLayout::eUndefined;
                vk::PipelineStageFlags activeBarrierRangeSrcStageMask = {};
                vk::AccessFlags activeBarrierRangeSrcAccessMask = {};

                for (uint32_t arrayLayer = resolvedSubresources.baseArrayLayer; arrayLayer < resolvedSubresources.arrayLayerEnd(); ++arrayLayer)
                {
                    const size_t stateIndex =
                        planeOffset +
                        static_cast<size_t>(resolveTextureSubresourceIndex(&texture, mipLevel, arrayLayer));
                    if (stateIndex >= state.layouts.size())
                    {
                        appendPendingTextureBarrierRange(
                            texture,
                            planeAspect,
                            mipLevel,
                            hasActiveBarrierRange,
                            activeBarrierRangeBaseLayer,
                            arrayLayer,
                            activeBarrierRangeOldLayout,
                            activeBarrierRangeSrcStageMask,
                            activeBarrierRangeSrcAccessMask,
                            targetLayout,
                            requiredStageMask,
                            requiredAccessMask,
                            outPendingBarriers);
                        continue;
                    }

                    const vk::ImageLayout currentLayout = state.layouts[stateIndex];
                    const vk::PipelineStageFlags currentStageMask = state.stageMasks[stateIndex];
                    const vk::AccessFlags currentAccessMask = state.accessMasks[stateIndex];

                    // Layout can stay unchanged while a write-after-read or
                    // read-after-write hazard still requires an image barrier.
                    const bool layoutChange = currentLayout != targetLayout;
                    const bool accessChange = currentAccessMask != requiredAccessMask;
                    const bool stageChange = currentStageMask != requiredStageMask;
                    const bool writeInvolved =
                        !isReadOnlyAccess(currentAccessMask) || !isReadOnlyAccess(requiredAccessMask);
                    const bool mergeReadOnlyState =
                        !layoutChange &&
                        isReadOnlyAccess(currentAccessMask) &&
                        isReadOnlyAccess(requiredAccessMask);
                    const bool requiresBarrier =
                        !mergeReadOnlyState &&
                        (layoutChange || accessChange || stageChange || writeInvolved);

                    if (!requiresBarrier)
                    {
                        appendPendingTextureBarrierRange(
                            texture,
                            planeAspect,
                            mipLevel,
                            hasActiveBarrierRange,
                            activeBarrierRangeBaseLayer,
                            arrayLayer,
                            activeBarrierRangeOldLayout,
                            activeBarrierRangeSrcStageMask,
                            activeBarrierRangeSrcAccessMask,
                            targetLayout,
                            requiredStageMask,
                            requiredAccessMask,
                            outPendingBarriers);
                    }
                    else if (!hasActiveBarrierRange)
                    {
                        hasActiveBarrierRange = true;
                        activeBarrierRangeBaseLayer = arrayLayer;
                        activeBarrierRangeOldLayout = currentLayout;
                        activeBarrierRangeSrcStageMask = currentStageMask;
                        activeBarrierRangeSrcAccessMask = currentAccessMask;
                    }
                    else if (
                        activeBarrierRangeOldLayout != currentLayout ||
                        activeBarrierRangeSrcStageMask != currentStageMask ||
                        activeBarrierRangeSrcAccessMask != currentAccessMask)
                    {
                        appendPendingTextureBarrierRange(
                            texture,
                            planeAspect,
                            mipLevel,
                            hasActiveBarrierRange,
                            activeBarrierRangeBaseLayer,
                            arrayLayer,
                            activeBarrierRangeOldLayout,
                            activeBarrierRangeSrcStageMask,
                            activeBarrierRangeSrcAccessMask,
                            targetLayout,
                            requiredStageMask,
                            requiredAccessMask,
                            outPendingBarriers);
                        hasActiveBarrierRange = true;
                        activeBarrierRangeBaseLayer = arrayLayer;
                        activeBarrierRangeOldLayout = currentLayout;
                        activeBarrierRangeSrcStageMask = currentStageMask;
                        activeBarrierRangeSrcAccessMask = currentAccessMask;
                    }

                    if (mergeReadOnlyState)
                    {
                        state.stageMasks[stateIndex] |= requiredStageMask;
                        state.accessMasks[stateIndex] |= requiredAccessMask;
                    }
                    else
                    {
                        state.layouts[stateIndex] = targetLayout;
                        state.stageMasks[stateIndex] = requiredStageMask;
                        state.accessMasks[stateIndex] = requiredAccessMask;
                    }
                }

                appendPendingTextureBarrierRange(
                    texture,
                    planeAspect,
                    mipLevel,
                    hasActiveBarrierRange,
                    activeBarrierRangeBaseLayer,
                    resolvedSubresources.arrayLayerEnd(),
                    activeBarrierRangeOldLayout,
                    activeBarrierRangeSrcStageMask,
                    activeBarrierRangeSrcAccessMask,
                    targetLayout,
                    requiredStageMask,
                    requiredAccessMask,
                    outPendingBarriers);
            }
        }

    }

    void VKTaskDependencyResolver::flushPendingTextureBarriers(
        vk::CommandBuffer commandBuffer,
        eastl::vector<PendingTextureBarrier> &pendingTextureBarriers)
    {
        if (pendingTextureBarriers.empty())
        {
            return;
        }

        coalescePendingTextureBarriers(pendingTextureBarriers);
        vk::PipelineStageFlags batchSrcStageMask = {};
        vk::PipelineStageFlags batchDstStageMask = {};
        eastl::vector<vk::ImageMemoryBarrier> imageBarriers;
        imageBarriers.reserve(pendingTextureBarriers.size());

        for (const PendingTextureBarrier &pendingBarrier : pendingTextureBarriers)
        {
            if (!imageBarriers.empty() &&
                (batchSrcStageMask != pendingBarrier.srcStageMask || batchDstStageMask != pendingBarrier.dstStageMask))
            {
                flushTextureBarrierBatch(
                    commandBuffer,
                    mDiagnostics,
                    batchSrcStageMask,
                    batchDstStageMask,
                    imageBarriers);
            }

            if (imageBarriers.empty())
            {
                batchSrcStageMask = pendingBarrier.srcStageMask;
                batchDstStageMask = pendingBarrier.dstStageMask;
            }

            imageBarriers.push_back(pendingBarrier.barrier);
        }

        flushTextureBarrierBatch(
            commandBuffer,
            mDiagnostics,
            batchSrcStageMask,
            batchDstStageMask,
            imageBarriers);
        pendingTextureBarriers.clear();
    }

    void VKTaskDependencyResolver::coalescePendingTextureBarriers(eastl::vector<PendingTextureBarrier> &pendingTextureBarriers)
    {
        if (pendingTextureBarriers.size() <= 1u)
        {
            return;
        }

        size_t writeIndex = 0u;
        for (size_t readIndex = 0u; readIndex < pendingTextureBarriers.size(); ++readIndex)
        {
            PendingTextureBarrier &pendingBarrier = pendingTextureBarriers[readIndex];
            if (writeIndex > 0u)
            {
                PendingTextureBarrier &last = pendingTextureBarriers[writeIndex - 1u];
                if (last.srcStageMask == pendingBarrier.srcStageMask &&
                    last.dstStageMask == pendingBarrier.dstStageMask &&
                    canMergeTextureBarrierRange(last.barrier, pendingBarrier.barrier))
                {
                    last.barrier.subresourceRange.levelCount += pendingBarrier.barrier.subresourceRange.levelCount;
                    continue;
                }
            }

            if (writeIndex != readIndex)
            {
                pendingTextureBarriers[writeIndex] = eastl::move(pendingTextureBarriers[readIndex]);
            }
            ++writeIndex;
        }

        pendingTextureBarriers.resize(writeIndex);
    }

    void VKTaskDependencyResolver::synchronizeTextureSubresources(
        vk::CommandBuffer commandBuffer,
        VKTexture &texture,
        vk::ImageLayout targetLayout,
        vk::PipelineStageFlags requiredStageMask,
        vk::AccessFlags requiredAccessMask,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount)
    {
        if (texture.getNativeImage() == vk::Image{})
        {
            const eastl::string label = texture.getLabelName().empty() ? eastl::string("<unnamed>") : eastl::string(texture.getLabelName().c_str());
            throw makeLogicError("VKTaskDependencyResolver::synchronizeTextureSubresources encountered destroyed or uninitialized Vulkan texture '" + label + "'.");
        }

        VKResourceTextureState &state = getOrCreateTextureState(texture);
        state.touched = true;

        const ResolvedTextureSubresourceRange resolvedSubresources = resolveTextureSubresourceRange(
            &texture,
            aspectMask,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount);
        ++mDiagnostics.texturePrepareCalls;
        mDiagnostics.texturePrepareSubresourceCount +=
            static_cast<uint64_t>(resolvedSubresources.mipLevelCount) * static_cast<uint64_t>(resolvedSubresources.arrayLayerCount);
        eastl::vector<PendingTextureBarrier> pendingTextureBarriers;
        pendingTextureBarriers.reserve(
            state.planeAspects.size() * static_cast<size_t>(resolvedSubresources.mipLevelCount));

        collectTextureSubresourceSyncBarriers(
            texture,
            state,
            resolvedSubresources,
            targetLayout,
            requiredStageMask,
            requiredAccessMask,
            pendingTextureBarriers);
        flushPendingTextureBarriers(
            commandBuffer,
            pendingTextureBarriers);
    }

    bool VKTaskDependencyResolver::transitionImageState(
        vk::CommandBuffer commandBuffer,
        vk::Image image,
        const vk::ImageSubresourceRange &subresourceRange,
        vk::ImageLayout &layout,
        vk::PipelineStageFlags &stageMask,
        vk::AccessFlags &accessMask,
        vk::ImageLayout newLayout,
        vk::PipelineStageFlags newStageMask,
        vk::AccessFlags newAccessMask)
    {
        if (layout == newLayout &&
            stageMask == newStageMask &&
            accessMask == newAccessMask)
        {
            return false;
        }

        vk::ImageMemoryBarrier barrier = {};
        barrier.oldLayout = layout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = subresourceRange;
        barrier.srcAccessMask = accessMask;
        barrier.dstAccessMask = newAccessMask;

        commandBuffer.pipelineBarrier(
            normalizeStageMask(stageMask),
            normalizeStageMask(newStageMask),
            {},
            {},
            {},
            barrier);

        layout = newLayout;
        stageMask = newStageMask;
        accessMask = newAccessMask;
        return true;
    }

    void VKTaskDependencyResolver::transitionTextureLayout(
        vk::CommandBuffer commandBuffer,
        VKTexture &texture,
        vk::ImageLayout targetLayout,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount)
    {
        synchronizeTextureSubresources(
            commandBuffer,
            texture,
            targetLayout,
            resolveStageMaskForLayout(targetLayout),
            resolveAccessMaskForLayout(targetLayout),
            aspectMask,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount);
    }

    void VKTaskDependencyResolver::restoreTextureSteadyStateLayout(
        vk::CommandBuffer commandBuffer,
        VKTexture &texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount)
    {
        if (!texture.hasSteadyStateLayout())
        {
            return;
        }

        ++mDiagnostics.textureSteadyStateRestoreCalls;
        transitionTextureLayout(
            commandBuffer,
            texture,
            texture.getSteadyStateLayout(),
            aspectMask,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount);
    }

    VKResourceFinalStateBatch VKTaskDependencyResolver::finalize(vk::CommandBuffer commandBuffer)
    {
        VKResourceFinalStateBatch finalStateBatch = {};
        finalStateBatch.textures.reserve(mTextureStates.size());
        size_t trackedBufferRangeCount = 0u;
        for (const auto &entry : mBufferStates)
        {
            trackedBufferRangeCount += entry.second.size();
        }
        finalStateBatch.buffers.reserve(trackedBufferRangeCount);

        for (auto &entry : mTextureStates)
        {
            VKResourceTextureState &textureState = entry.second;
            if (!textureState.touched || textureState.texture == nullptr)
            {
                continue;
            }

            if (textureNeedsSteadyStateLayoutRestore(textureState))
            {
                restoreTextureSteadyStateLayout(commandBuffer, *textureState.texture);
            }
            textureState.touched = false;

            finalStateBatch.textures.push_back(textureState);
        }

        for (const auto &entry : mBufferStates)
        {
            for (const VKResourceBufferState &bufferState : entry.second)
            {
                if (!bufferState.initialized || bufferState.size == 0u)
                {
                    continue;
                }

                finalStateBatch.buffers.push_back(bufferState);
            }
        }

        TaskDependencyResolverDiagnosticUtils::emitFinalize(
            *mQueue,
            mDiagnostics,
            mBufferStates.size(),
            mTextureStates.size(),
            finalStateBatch.buffers.size(),
            finalStateBatch.textures.size(),
            trackedBufferRangeCount);
        mDiagnostics.reset();
        return finalStateBatch;
    }

    void VKTaskDependencyResolver::recordTrackedBufferWriteCompletionBarriers(vk::CommandBuffer commandBuffer) const
    {
        for (const auto &entry : mBufferStates)
        {
            const VKBuffer *buffer = entry.first;
            if (buffer == nullptr || buffer->getNativeBuffer() == vk::Buffer{})
            {
                continue;
            }

            const eastl::vector<VKResourceBufferState> &states = entry.second;
            for (const VKResourceBufferState &state : states)
            {
                if (!state.initialized || state.size == 0u)
                {
                    continue;
                }

                // Only legalize writes that can be observed by later command buffers
                // in the same submit. Pure transfer-read source buffers do not need
                // this completion barrier.
                vk::PipelineStageFlags srcStageMask = {};
                vk::AccessFlags srcAccessMask = extractWriteAccessMask(state.accessMask);
                if (srcAccessMask != vk::AccessFlags{})
                {
                    srcStageMask |= state.stageMask;
                }
                if (state.lastWriteInitialized && state.lastWriteAccessMask != vk::AccessFlags{})
                {
                    srcStageMask |= state.lastWriteStageMask;
                    srcAccessMask |= extractWriteAccessMask(state.lastWriteAccessMask);
                }
                if (srcAccessMask == vk::AccessFlags{})
                {
                    continue;
                }
                if (srcStageMask == vk::PipelineStageFlags{})
                {
                    srcStageMask = vk::PipelineStageFlagBits::eAllCommands;
                }

                vk::BufferMemoryBarrier barrier = {};
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = buffer->getNativeBuffer();
                barrier.offset = state.offset;
                barrier.size = state.offset == 0u && state.size == buffer->getStorageSize() ? VK_WHOLE_SIZE : state.size;
                barrier.srcAccessMask = srcAccessMask;
                barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;

                commandBuffer.pipelineBarrier(
                    normalizeStageMask(srcStageMask),
                    normalizeStageMask(vk::PipelineStageFlagBits::eAllCommands),
                    {},
                    {},
                    barrier,
                    {});
            }
        }
    }

    eastl::vector<VKResourceBufferState> &VKTaskDependencyResolver::getOrCreateBufferStates(VKBuffer &buffer)
    {
        const auto existing = mBufferStates.find(&buffer);
        if (existing != mBufferStates.end())
        {
            return existing->second;
        }

        eastl::vector<VKResourceBufferState> trackerInitialStates;
        VKResourceBufferStateInitialSource initialSource = VKResourceBufferStateInitialSource::None;
        if (mStateDB == nullptr)
        {
            throw makeLogicError("VKTaskDependencyResolver::getOrCreateBufferStates requires a bound Vulkan resource state DB.");
        }

        if (!mStateDB->tryGetCurrentBufferStates(buffer, trackerInitialStates, &initialSource))
        {
            throw makeLogicError("VKTaskDependencyResolver::getOrCreateBufferStates could not resolve tracker-initial Vulkan buffer states.");
        }
        eastl::vector<VKResourceBufferState> states;
        states.reserve(trackerInitialStates.size());
        for (const VKResourceBufferState &trackerInitialState : trackerInitialStates)
        {
            if (trackerInitialState.size == 0u)
            {
                continue;
            }

            VKResourceBufferState state = trackerInitialState;
            state.buffer = &buffer;
            if (trackerInitialState.initialized)
            {
                state.initialSource = initialSource;
            }
            else
            {
                state.initialSource = VKResourceBufferStateInitialSource::None;
            }
            states.push_back(state);
        }
        if (states.empty())
        {
            states.push_back({
                .buffer = &buffer,
                .offset = 0u,
                .size = buffer.getStorageSize(),
            });
        }
        mergeAdjacentBufferRangeStates(states, trackerBufferStatePayloadMatches);
        return mBufferStates.emplace(&buffer, eastl::move(states)).first->second;
    }

    VKResourceTextureState &VKTaskDependencyResolver::getOrCreateTextureState(VKTexture &texture)
    {
        const auto existing = mTextureStates.find(&texture);
        if (existing != mTextureStates.end())
        {
            return existing->second;
        }

        VKResourceTextureState state = {};
        state.texture = &texture;

        if (mStateDB == nullptr)
        {
            throw makeLogicError("VKTaskDependencyResolver::getOrCreateTextureState requires a bound Vulkan resource state DB.");
        }

        VKResourceTextureState trackerInitialState = {};
        if (!mStateDB->tryGetCurrentTextureState(texture, trackerInitialState))
        {
            throw makeLogicError("VKTaskDependencyResolver::getOrCreateTextureState could not resolve a tracker-initial Vulkan texture state.");
        }

        state = eastl::move(trackerInitialState);
        state.texture = &texture;
        state.touched = false;

        return mTextureStates.emplace(&texture, eastl::move(state)).first->second;
    }

    bool VKTaskDependencyResolver::textureNeedsSteadyStateLayoutRestore(const VKResourceTextureState &state)
    {
        if (state.texture == nullptr || state.steadyStateLayout == vk::ImageLayout::eUndefined)
        {
            return false;
        }

        for (vk::ImageLayout layout : state.layouts)
        {
            if (layout != state.steadyStateLayout)
            {
                return true;
            }
        }

        return false;
    }
} // namespace GVM::RHI::Vulkan
