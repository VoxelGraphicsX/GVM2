#include "VKTaskDependencyResolver.hpp"
#include "VKBuffer.hpp"
#include "VKBufferRangeStateUtils.hpp"
#include "VKDevice.hpp"
#include "VKPresentManager.hpp"
#include "VKQueue.hpp"
#include "VKSyncUtils.hpp"
#include "VKTexture.hpp"
#include "VKTextureView.hpp"
#include "VKTextureSubresourceUtils.hpp"
#include <EASTL/algorithm.h>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        /** Holds one view's access to a single image cell while a batch is merged, without changing queue state. */
        struct ImageSubresourceAccess
        {
            uint32_t index = 0;
            vk::ImageSubresourceRange range;
            VKAccessScope access;
        };

        /// Resolves RAW/WAW/WAR and read visibility without discarding a producer after its first reader.
        bool resolveAccess(VKResourceAccessState &state, VKAccessScope next, bool layoutChange, vk::MemoryBarrier2 &barrier)
        {
            const bool write = !isReadOnlyAccess(next.accesses);
            const bool visible = (state.visibleReads.stages & next.stages) == next.stages && (state.visibleReads.accesses & next.accesses) == next.accesses;
            const bool dependency = layoutChange || (write && state.initialized) || (!write && state.hasProducer && !visible);
            if (dependency)
            {
                barrier.srcStageMask = state.producer.stages;
                barrier.srcAccessMask = state.producer.accesses;
                if (write || layoutChange)
                {
                    barrier.srcStageMask |= state.current.stages;
                }
                barrier.dstStageMask = next.stages;
                barrier.dstAccessMask = next.accesses;
                if (!write && !layoutChange)
                {
                    // Emit the whole union before caching it: separately ORing independently
                    // established stage/access pairs would invent visibility for cross-pairs.
                    barrier.dstStageMask |= state.visibleReads.stages;
                    barrier.dstAccessMask |= state.visibleReads.accesses;
                }
            }
            if (write)
            {
                state.current = next;
                state.producer = {next.stages, extractWriteAccessMask(next.accesses)};
                state.hasProducer = true;
                state.visibleReads = {};
            }
            else if (layoutChange)
            {
                // Layout writes are automatically available. Retain the destination scope
                // as the dependency-chain anchor for readers in additional stages.
                state.current = next;
                state.producer = {next.stages, {}};
                state.hasProducer = true;
                state.visibleReads = next;
            }
            else
            {
                if (!isReadOnlyAccess(state.current.accesses))
                {
                    state.current = {};
                }
                state.current.stages |= next.stages;
                state.current.accesses |= next.accesses;
                if (dependency)
                {
                    state.visibleReads = {barrier.dstStageMask, barrier.dstAccessMask};
                }
            }
            state.initialized = true;
            return dependency;
        }

        /// Compares range payloads so adjacent equal intervals can share one state entry.
        bool equalBufferAccess(const VKResourceBufferState &a, const VKResourceBufferState &b)
        {
            return a.access == b.access;
        }

        /// Batches barriers at one dependency point without widening their individual scopes.
        void emitBarriers(vk::CommandBuffer cb, const eastl::vector<vk::BufferMemoryBarrier2> &buffers, const eastl::vector<vk::ImageMemoryBarrier2> &images)
        {
            if (buffers.empty() && images.empty())
            {
                return;
            }
            vk::DependencyInfo info = {};
            info.bufferMemoryBarrierCount = static_cast<uint32_t>(buffers.size());
            info.pBufferMemoryBarriers = buffers.data();
            info.imageMemoryBarrierCount = static_cast<uint32_t>(images.size());
            info.pImageMemoryBarriers = images.data();
            cb.pipelineBarrier2(info);
        }

        /// Tests whether image ranges share the same layout transition and dependency scopes.
        bool sameImageDependency(const vk::ImageMemoryBarrier2 &a, const vk::ImageMemoryBarrier2 &b)
        {
            return a.image == b.image && a.oldLayout == b.oldLayout && a.newLayout == b.newLayout && a.srcStageMask == b.srcStageMask && a.dstStageMask == b.dstStageMask && a.srcAccessMask == b.srcAccessMask && a.dstAccessMask == b.dstAccessMask && a.srcQueueFamilyIndex == b.srcQueueFamilyIndex && a.dstQueueFamilyIndex == b.dstQueueFamilyIndex && a.subresourceRange.aspectMask == b.subresourceRange.aspectMask;
        }

        /// Compacts adjacent mip ranges after their layer ranges have been coalesced.
        void coalesceImageMips(eastl::vector<vk::ImageMemoryBarrier2> &barriers)
        {
            size_t count = 0;
            for (size_t index = 0; index < barriers.size(); ++index)
            {
                const auto &barrier = barriers[index];
                if (count != 0)
                {
                    auto &last = barriers[count - 1];
                    auto &a = last.subresourceRange;
                    const auto &b = barrier.subresourceRange;
                    if (sameImageDependency(last, barrier) && a.baseMipLevel + a.levelCount == b.baseMipLevel && a.baseArrayLayer == b.baseArrayLayer && a.layerCount == b.layerCount)
                    {
                        a.levelCount += b.levelCount;
                        continue;
                    }
                }
                if (count != index)
                {
                    barriers[count] = barrier;
                }
                ++count;
            }
            barriers.resize(count);
        }

        /// Coalesces consecutive layers only when every synchronization field matches.
        void appendImageBarrier(eastl::vector<vk::ImageMemoryBarrier2> &barriers, const vk::ImageMemoryBarrier2 &barrier)
        {
            if (!barriers.empty())
            {
                auto &last = barriers.back();
                const auto &a = last.subresourceRange;
                const auto &b = barrier.subresourceRange;
                if (sameImageDependency(last, barrier) && a.baseMipLevel == b.baseMipLevel && a.levelCount == b.levelCount && a.baseArrayLayer + a.layerCount == b.baseArrayLayer)
                {
                    last.subresourceRange.layerCount += b.layerCount;
                    return;
                }
            }
            barriers.push_back(barrier);
        }
    } // namespace

    void VKTaskDependencyResolver::reset()
    {
        mQueue = nullptr;
        mStateDB = nullptr;
        mBuffers.clear();
        mTextures.clear();
        mDiagnostics.reset();
    }
    void VKTaskDependencyResolver::init(const VKQueue &queue, VKResourceStateDB &stateDB)
    {
        reset();
        mQueue = &queue;
        mStateDB = &stateDB;
    }

    void VKTaskDependencyResolver::synchronizeBufferRange(vk::CommandBuffer cb, VKBuffer &buffer, uint64_t offset, uint64_t size, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses)
    {
        synchronizeBufferRanges(cb, {{&buffer, offset, size, stages, accesses}});
    }

    void VKTaskDependencyResolver::synchronizeBufferRanges(vk::CommandBuffer cb, eastl::vector<BufferRangeSyncRequest> requests)
    {
        if (requests.empty())
        {
            return;
        }
        eastl::sort(requests.begin(), requests.end(), [](const auto &left, const auto &right)
        {
            if (left.buffer != right.buffer)
            {
                return reinterpret_cast<uintptr_t>(left.buffer) < reinterpret_cast<uintptr_t>(right.buffer);
            }
            if (left.offset != right.offset)
            {
                return left.offset < right.offset;
            }
            return left.size < right.size;
        });
        size_t requestCount = 0;
        for (auto request : requests)
        {
            if (request.buffer == nullptr || !request.requiredStageMask)
            {
                throw std::invalid_argument("Buffer access requires a resource and explicit pipeline stages.");
            }
            request.size = resolveBufferRangeSize("synchronizeBufferRanges", request.buffer, request.offset, request.size);
            if (request.size == 0)
            {
                continue;
            }
            if (requestCount != 0)
            {
                auto &previous = requests[requestCount - 1];
                if (previous.buffer == request.buffer && previous.offset == request.offset && previous.size == request.size)
                {
                    previous.requiredStageMask |= request.requiredStageMask;
                    previous.requiredAccessMask |= request.requiredAccessMask;
                    continue;
                }
            }
            requests[requestCount++] = request;
        }
        requests.resize(requestCount);

        eastl::vector<vk::BufferMemoryBarrier2> barriers;
        size_t firstRequest = 0;
        while (firstRequest < requests.size())
        {
            auto &buffer = *requests[firstRequest].buffer;
            auto &ranges = mStateDB->editBuffer(buffer);
            mBuffers.insert(&buffer);
            size_t requestEnd = firstRequest;
            while (requestEnd < requests.size() && requests[requestEnd].buffer == &buffer)
            {
                const auto &request = requests[requestEnd++];
                splitBufferRangeStates(ranges, request.offset);
                splitBufferRangeStates(ranges, request.offset + request.size);
                mDiagnostics.recordBufferPrepare();
            }
            // All boundaries are fixed before any access state is advanced. These
            // requests describe one execution batch, not ordered producer/consumer commands.
            for (auto &range : ranges)
            {
                VKAccessScope next;
                for (size_t index = firstRequest; index < requestEnd; ++index)
                {
                    const auto &request = requests[index];
                    if (request.offset >= range.offset + range.size)
                    {
                        break;
                    }
                    if (request.offset + request.size > range.offset)
                    {
                        next.stages |= request.requiredStageMask;
                        next.accesses |= request.requiredAccessMask;
                    }
                }
                if (!next.stages)
                {
                    continue;
                }
                vk::MemoryBarrier2 dependency = {};
                if (!resolveAccess(range.access, next, false, dependency))
                {
                    continue;
                }
                vk::BufferMemoryBarrier2 barrier = {};
                barrier.srcStageMask = dependency.srcStageMask;
                barrier.srcAccessMask = dependency.srcAccessMask;
                barrier.dstStageMask = dependency.dstStageMask;
                barrier.dstAccessMask = dependency.dstAccessMask;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = buffer.getNativeBuffer();
                barrier.offset = range.offset;
                barrier.size = range.size;
                barriers.push_back(barrier);
                mDiagnostics.recordBufferBarrier(!isReadOnlyAccess(barrier.srcAccessMask) || !isReadOnlyAccess(barrier.dstAccessMask));
            }
            mergeAdjacentBufferRangeStates(ranges, equalBufferAccess);
            firstRequest = requestEnd;
        }
        if (!barriers.empty())
        {
            mDiagnostics.recordBufferPipelineBarrierCall();
        }
        emitBarriers(cb, barriers, {});
    }

    void VKTaskDependencyResolver::synchronizeTextureViews(vk::CommandBuffer cb, eastl::vector<TextureSubresourceSyncRequest> requests)
    {
        if (requests.empty())
        {
            return;
        }
        eastl::sort(requests.begin(), requests.end(), [](const auto &left, const auto &right)
        {
            return reinterpret_cast<uintptr_t>(left.view->getTexture()) < reinterpret_cast<uintptr_t>(right.view->getTexture());
        });
        eastl::vector<ImageSubresourceAccess> cells;
        eastl::vector<vk::ImageMemoryBarrier2> barriers;
        size_t first = 0;
        while (first < requests.size())
        {
            auto &texture = *requests[first].view->getTexture();
            const auto layout = requests[first].layout;
            auto &state = mStateDB->editTexture(texture);
            mTextures.insert(&texture);
            cells.clear();
            vk::PipelineStageFlags2 firstUseStages{};
            size_t end = first;
            while (end < requests.size() && requests[end].view->getTexture() == &texture)
            {
                const auto &request = requests[end++];
                const auto &view = request.view->getDescriptor();
                const auto range = resolveTextureSubresourceRange(&texture, view.aspect, view.baseMipLevel,
                    view.mipLevelCount, view.baseArrayLayer, view.arrayLayerCount);
                firstUseStages |= request.stages;
                mDiagnostics.recordTexturePrepare();
                for (size_t plane = 0; plane < state.planeAspects.size(); ++plane)
                {
                    if (!texturePlaneMatches(range, state.planeAspects[plane]))
                    {
                        continue;
                    }
                    for (uint32_t mip = range.baseMipLevel; mip < range.mipLevelEnd(); ++mip)
                    {
                        for (uint32_t layer = range.baseArrayLayer; layer < range.arrayLayerEnd(); ++layer)
                        {
                            cells.push_back({static_cast<uint32_t>(plane * state.subresourceCountPerPlane) +
                                resolveTextureSubresourceIndex(&texture, mip, layer),
                                {state.planeAspects[plane], mip, 1, layer, 1}, {request.stages, request.accesses}});
                        }
                    }
                }
            }
            if (mQueue->getDevice()->getPresentManager().setDirectPresentFirstUse(texture, firstUseStages))
            {
                for (auto &cell : state.subresources)
                {
                    cell.access = {};
                    cell.access.current.stages = firstUseStages;
                }
            }
            eastl::sort(cells.begin(), cells.end(), [](const auto &left, const auto &right)
            {
                return left.index < right.index;
            });
            size_t index = 0;
            while (index < cells.size())
            {
                auto use = cells[index++];
                while (index < cells.size() && cells[index].index == use.index)
                {
                    const auto &other = cells[index++];
                    use.access.stages |= other.access.stages;
                    use.access.accesses |= other.access.accesses;
                }
                auto &cell = state.subresources[use.index];
                mDiagnostics.recordTexturePrepareSubresource();
                vk::MemoryBarrier2 dependency{};
                if (!resolveAccess(cell.access, use.access, cell.layout != layout, dependency))
                {
                    continue;
                }
                vk::ImageMemoryBarrier2 barrier{};
                barrier.srcStageMask = dependency.srcStageMask;
                barrier.srcAccessMask = dependency.srcAccessMask;
                barrier.dstStageMask = dependency.dstStageMask;
                barrier.dstAccessMask = dependency.dstAccessMask;
                barrier.oldLayout = cell.layout;
                barrier.newLayout = layout;
                barrier.image = texture.getNativeImage();
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange = use.range;
                mDiagnostics.recordTextureBarrierSubresource(cell.layout != layout);
                cell.layout = layout;
                appendImageBarrier(barriers, barrier);
            }
            first = end;
        }
        coalesceImageMips(barriers);
        mDiagnostics.recordTextureBarriers(barriers.size());
        emitBarriers(cb, {}, barriers);
    }

    void VKTaskDependencyResolver::synchronizeTextureSubresources(vk::CommandBuffer cb, VKTexture &texture, vk::ImageLayout layout, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses, TextureAspectFlags aspect, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount)
    {
        if (!texture.getNativeImage() || (!stages && layout != vk::ImageLayout::ePresentSrcKHR))
        {
            throw std::invalid_argument("Image access requires a live image and explicit pipeline stages.");
        }
        auto &state = mStateDB->editTexture(texture);
        if (mQueue->getDevice()->getPresentManager().setDirectPresentFirstUse(texture, stages ? stages : vk::PipelineStageFlagBits2::eColorAttachmentOutput))
        {
            // The acquisition semaphore's wait stage must chain into the first layout transition.
            for (auto &cell : state.subresources)
            {
                cell.access = {};
                cell.access.current.stages = stages ? stages : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            }
        }
        mTextures.insert(&texture);
        mDiagnostics.recordTexturePrepare();
        const auto range = resolveTextureSubresourceRange(&texture, aspect, mip, mipCount, layer, layerCount);
        eastl::vector<vk::ImageMemoryBarrier2> barriers;
        for (size_t plane = 0; plane < state.planeAspects.size(); ++plane)
        {
            if (!texturePlaneMatches(range, state.planeAspects[plane]))
            {
                continue;
            }
            for (uint32_t m = range.baseMipLevel; m < range.mipLevelEnd(); ++m)
            {
                for (uint32_t l = range.baseArrayLayer; l < range.arrayLayerEnd(); ++l)
                {
                    auto &cell = state.subresources[plane * state.subresourceCountPerPlane + resolveTextureSubresourceIndex(&texture, m, l)];
                    mDiagnostics.recordTexturePrepareSubresource();
                    vk::MemoryBarrier2 dependency = {};
                    if (!resolveAccess(cell.access, {stages, accesses}, cell.layout != layout, dependency))
                    {
                        continue;
                    }
                    vk::ImageMemoryBarrier2 barrier = {};
                    barrier.srcStageMask = dependency.srcStageMask;
                    barrier.srcAccessMask = dependency.srcAccessMask;
                    barrier.dstStageMask = dependency.dstStageMask;
                    barrier.dstAccessMask = dependency.dstAccessMask;
                    barrier.oldLayout = cell.layout;
                    barrier.newLayout = layout;
                    barrier.image = texture.getNativeImage();
                    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.subresourceRange = vk::ImageSubresourceRange{state.planeAspects[plane], m, 1, l, 1};
                    mDiagnostics.recordTextureBarrierSubresource(cell.layout != layout);
                    cell.layout = layout;
                    appendImageBarrier(barriers, barrier);
                }
            }
        }
        coalesceImageMips(barriers);
        mDiagnostics.recordTextureBarriers(barriers.size());
        emitBarriers(cb, {}, barriers);
    }

    bool VKTaskDependencyResolver::transitionImageState(vk::CommandBuffer cb, vk::Image image, const vk::ImageSubresourceRange &range, vk::ImageLayout &layout, vk::PipelineStageFlags2 &stages, vk::AccessFlags2 &accesses, vk::ImageLayout newLayout, vk::PipelineStageFlags2 newStages, vk::AccessFlags2 newAccesses)
    {
        if (layout == newLayout && isReadOnlyAccess(accesses) && isReadOnlyAccess(newAccesses))
        {
            return false;
        }
        vk::ImageMemoryBarrier2 barrier = {};
        barrier.srcStageMask = stages;
        barrier.srcAccessMask = extractWriteAccessMask(accesses);
        barrier.dstStageMask = newStages;
        barrier.dstAccessMask = newAccesses;
        barrier.oldLayout = layout;
        barrier.newLayout = newLayout;
        barrier.image = image;
        barrier.subresourceRange = range;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        emitBarriers(cb, {}, {barrier});
        layout = newLayout;
        stages = newStages;
        accesses = newAccesses;
        return true;
    }

    void VKTaskDependencyResolver::finalize(vk::CommandBuffer cb)
    {
        eastl::vector<vk::BufferMemoryBarrier2> barriers;
        const VKAccessScope hostRead{vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
        for (auto *buffer : mBuffers)
        {
            if (!buffer->supportsCpuRead())
            {
                continue;
            }
            auto &ranges = mStateDB->editBuffer(*buffer);
            for (auto &range : ranges)
            {
                vk::MemoryBarrier2 dependency{};
                if (!resolveAccess(range.access, hostRead, false, dependency))
                {
                    continue;
                }
                vk::BufferMemoryBarrier2 barrier{};
                barrier.srcStageMask = dependency.srcStageMask;
                barrier.srcAccessMask = dependency.srcAccessMask;
                barrier.dstStageMask = dependency.dstStageMask;
                barrier.dstAccessMask = dependency.dstAccessMask;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = buffer->getNativeBuffer();
                barrier.offset = range.offset;
                barrier.size = range.size;
                barriers.push_back(barrier);
                mDiagnostics.recordBufferBarrier(true);
            }
            mergeAdjacentBufferRangeStates(ranges, equalBufferAccess);
        }
        if (!barriers.empty())
        {
            mDiagnostics.recordBufferPipelineBarrierCall();
        }
        emitBarriers(cb, barriers, {});
        mDiagnostics.emitFinalize(*mQueue, mBuffers.size(), mTextures.size());
    }
} // namespace GVM::RHI::Vulkan
