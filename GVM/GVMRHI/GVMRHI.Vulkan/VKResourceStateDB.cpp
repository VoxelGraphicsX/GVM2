#include "VKResourceStateDB.hpp"
#include "VKBuffer.hpp"
#include "VKQueue.hpp"
#include "VKCommandEncoder.hpp"
#include "VKQuerySet.hpp"
#include "VKDevice.hpp"
#include <EASTL/algorithm.h>
#include "VKBufferRangeStateUtils.hpp"
#include "VKSyncUtils.hpp"
#include "VKTexture.hpp"
#include "VKTextureSubresourceUtils.hpp"

namespace GVM::RHI::Vulkan
{
    VKResourceTextureState VKResourceStateDB::makeTextureState(const VKTexture &texture)
    {
        VKResourceTextureState state;
        state.planeAspects = resolveTexturePlaneAspects(&texture);
        state.subresourceCountPerPlane = resolveTexturePlaneSubresourceCount(&texture);
        state.subresources.resize(state.planeAspects.size() * state.subresourceCountPerPlane);
        return state;
    }

    eastl::vector<VKResourceBufferState> &VKResourceStateDB::editBuffer(VKBuffer &buffer)
    {
        ScopedLock lock(mMutex);
        auto [it, inserted] = mBuffers.try_emplace(&buffer);
        if (inserted)
        {
            if (mHistory != nullptr)
            {
                ScopedLock historyLock(mHistory->mMutex);
                auto [history, created] = mHistory->mBuffers.try_emplace(&buffer);
                if (created)
                {
                    history->second.push_back({&buffer, 0, buffer.getStorageSize(), {}});
                }
                it->second = history->second;
            }
            else
            {
                it->second.push_back({&buffer, 0, buffer.getStorageSize(), {}});
            }
        }
        return it->second;
    }

    VKResourceTextureState &VKResourceStateDB::editTexture(const VKTexture &texture)
    {
        ScopedLock lock(mMutex);
        auto [it, inserted] = mTextures.try_emplace(&texture);
        if (inserted)
        {
            if (mHistory != nullptr)
            {
                ScopedLock historyLock(mHistory->mMutex);
                auto [history, created] = mHistory->mTextures.try_emplace(&texture);
                if (created)
                {
                    history->second = makeTextureState(texture);
                }
                it->second = history->second;
            }
            else
            {
                it->second = makeTextureState(texture);
            }
        }
        return it->second;
    }

    bool VKResourceStateDB::tryGetCurrentBufferState(const VKBuffer &buffer, VKResourceBufferState &out) const
    {
        ScopedLock lock(mMutex);
        out = {const_cast<VKBuffer *>(&buffer), 0, buffer.getStorageSize(), {}};
        const auto it = mBuffers.find(&buffer);
        if (it == mBuffers.end())
        {
            return true;
        }
        for (const auto &range : it->second)
        {
            out.access.current.stages |= range.access.current.stages;
            out.access.current.accesses |= range.access.current.accesses;
            out.access.producer.stages |= range.access.producer.stages;
            out.access.producer.accesses |= range.access.producer.accesses;
            out.access.initialized |= range.access.initialized;
            out.access.hasProducer |= range.access.hasProducer;
        }
        return true;
    }

    uint64_t VKResourceStateDB::lastBufferWriter(const VKBuffer &buffer, uint64_t offset, uint64_t size, uint32_t lane) const
    {
        ScopedLock lock(mMutex);
        uint64_t writer = 0;
        if (const auto it = mBuffers.find(&buffer); it != mBuffers.end())
        {
            for (const auto &range : it->second)
            {
                if (bufferRangesOverlap(range.offset, range.size, offset, size))
                {
                    writer = eastl::max(writer, range.frontier.writer[lane]);
                }
            }
        }
        return writer;
    }

    bool VKResourceStateDB::tryGetSharedTextureStateForTransition(const VKTexture &texture, TextureAspectFlags aspect, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount, vk::ImageLayout &layout, vk::PipelineStageFlags2 &stages, vk::AccessFlags2 &accesses) const
    {
        ScopedLock lock(mMutex);
        const auto it = mTextures.find(&texture);
        if (it == mTextures.end())
        {
            layout = vk::ImageLayout::eUndefined;
            stages = {};
            accesses = {};
            return true;
        }
        const auto &state = it->second;
        const auto range = resolveTextureSubresourceRange(&texture, aspect, mip, mipCount, layer, layerCount);
        bool first = true;
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
                    const auto &cell = state.subresources[plane * state.subresourceCountPerPlane + resolveTextureSubresourceIndex(&texture, m, l)];
                    if (!first && layout != cell.layout)
                    {
                        return false;
                    }
                    if (first)
                    {
                        layout = cell.layout;
                        stages = {};
                        accesses = {};
                        first = false;
                    }
                    stages |= cell.access.current.stages | cell.access.producer.stages;
                    accesses |= cell.access.current.accesses | cell.access.producer.accesses;
                }
            }
        }
        return !first;
    }

    void VKResourceStateDB::recordExternalBufferUsage(VKBuffer &buffer, uint64_t offset, uint64_t size, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses)
    {
        size = resolveBufferRangeSize("recordExternalBufferUsage", &buffer, offset, size);
        auto &ranges = editBuffer(buffer);
        splitBufferRangeStates(ranges, offset);
        splitBufferRangeStates(ranges, offset + size);
        for (auto &range : ranges)
        {
            if (bufferRangesOverlap(range.offset, range.size, offset, size))
            {
                range.access = {};
                range.access.current = {stages, accesses};
                range.access.initialized = true;
                range.access.hasProducer = !isReadOnlyAccess(accesses);
                if (range.access.hasProducer)
                {
                    range.access.producer = {stages, extractWriteAccessMask(accesses)};
                }
            }
        }
    }

    void VKResourceStateDB::recordExternalBufferUsage(VKBuffer &buffer, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses)
    {
        recordExternalBufferUsage(buffer, 0, WholeSize, stages, accesses);
    }

    void VKResourceStateDB::recordExternalTextureState(VKTexture &texture, TextureAspectFlags aspect, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount, vk::ImageLayout layout, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses)
    {
        ScopedLock submitLock(texture.getDevice().submissionMutex());
        auto &state = editTexture(texture);
        const auto range = resolveTextureSubresourceRange(&texture, aspect, mip, mipCount, layer, layerCount);
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
                    cell = {};
                    cell.layout = layout;
                    cell.access.current = {stages, accesses};
                    cell.access.initialized = true;
                    cell.access.producer = {stages, extractWriteAccessMask(accesses)};
                    cell.access.hasProducer = true;
                }
            }
        }
    }

    bool VKResourceStateDB::isTextureInSteadyStateLayout(const VKTexture &texture) const
    {
        ScopedLock lock(mMutex);
        if (!texture.hasSteadyStateLayout())
        {
            return true;
        }
        const auto it = mTextures.find(&texture);
        if (it == mTextures.end())
        {
            return false;
        }
        for (const auto &cell : it->second.subresources)
        {
            if (cell.layout != texture.getSteadyStateLayout())
            {
                return false;
            }
        }
        return true;
    }

    void VKResourceStateDB::discardBufferState(const VKBuffer &buffer)
    {
        ScopedLock lock(mMutex);
        mBuffers.erase(&buffer);
    }
    void VKResourceStateDB::discardTextureState(const VKTexture &texture)
    {
        ScopedLock lock(mMutex);
        mTextures.erase(&texture);
    }
    void VKResourceStateDB::reset()
    {
        ScopedLock lock(mMutex);
        mBuffers.clear();
        mTextures.clear();
        mAcceptedRelease = 0;
        mRelease.images.clear();
        mRelease.buffers.clear();
        mHistory = nullptr;
    }

    void VKResourceStateDB::beginSubmission(VKResourceStateDB &history, VKQueue &queue, uint64_t value)
    {
        if (&history == this || mHistory != nullptr || !mBuffers.empty() || !mTextures.empty())
        {
            throw std::logic_error("A submission resource overlay must start empty and borrow a distinct history.");
        }
        mHistory = &history;
        mQueue = &queue;
        mOther = queue.getDevice()->otherQueue(queue);
        mLane = &queue == queue.getDevice()->getMainQueueImpl() ? 0 : 1;
        mValue = value;
    }

    bool VKResourceStateDB::advanceFrontier(VKQueueFrontier &frontier, bool exclusive)
    {
        if (mOther == nullptr)
        {
            return false;
        }
        return frontier.advance(mLane, mQueue->getQueueFamilyIndex(), mValue, exclusive, mForeignWait);
    }

    namespace
    {
        /// Makes matching ownership barriers; the release's timeline signal and acquire's wait use ALL_COMMANDS.
        template<class Barrier>
        Barrier makeRelease(Barrier &acquire, uint32_t source, uint32_t destination)
        {
            acquire.srcQueueFamilyIndex = source;
            acquire.dstQueueFamilyIndex = destination;
            acquire.srcStageMask = {};
            acquire.srcAccessMask = {};
            acquire.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            acquire.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            Barrier release = acquire;
            release.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            release.srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            release.dstStageMask = {};
            release.dstAccessMask = {};
            return release;
        }
    }

    void VKResourceStateDB::transferOwnership(vk::BufferMemoryBarrier2 &acquire, const VKBuffer &buffer)
    {
        if (mRelease.bufferOwners.empty() || mRelease.bufferOwners.back() != buffer.getHandle())
        {
            mRelease.bufferOwners.push_back(buffer.getHandle());
        }
        mRelease.buffers.push_back(makeRelease(acquire, mOther->getQueueFamilyIndex(), mQueue->getQueueFamilyIndex()));
    }

    void VKResourceStateDB::transferOwnership(vk::ImageMemoryBarrier2 &acquire, const VKTexture &texture)
    {
        if (mRelease.imageOwners.empty() || mRelease.imageOwners.back() != texture.getHandle())
        {
            mRelease.imageOwners.push_back(texture.getHandle());
        }
        mRelease.images.push_back(makeRelease(acquire, mOther->getQueueFamilyIndex(), mQueue->getQueueFamilyIndex()));
    }

    void VKResourceStateDB::prepareQueueWaits(const eastl::vector<CommandEncoder> &users, eastl::vector<vk::SemaphoreSubmitInfo> &waits)
    {
        if (mOther == nullptr)
        {
            return;
        }
        for (const auto &encoder : users)
        {
            for (const auto &query : static_cast<VKCommandEncoder *>(encoder.get())->getRetainedQuerySets())
            {
                auto *native = static_cast<VKQuerySet *>(query.get());
                mForeignWait = eastl::max(mForeignWait, native->lastUse(1 - mLane));
                mQueries.push_back(native);
            }
        }
        if (!mRelease.buffers.empty() || !mRelease.images.empty())
        {
            // A subsequent acquire failure is terminal. Preserve the accepted release receipt and exact ranges for diagnostics.
            mForeignWait = mOther->submitOwnershipRelease(mRelease);
            ScopedLock lock(mHistory->mMutex);
            mHistory->mAcceptedRelease = mForeignWait;
            mHistory->mRelease.buffers.swap(mRelease.buffers);
            mHistory->mRelease.images.swap(mRelease.images);
        }
        if (mForeignWait == 0)
        {
            return;
        }
        mergeTimelineWait(waits, mOther->timelineWait(mForeignWait));
    }

    bool VKResourceStateDB::hasUnacquiredRelease() const
    {
        ScopedLock lock(mMutex);
        return mAcceptedRelease != 0;
    }

    void VKResourceStateDB::commitSubmission() noexcept
    {
        ScopedLock lock(mMutex);
        ScopedLock historyLock(mHistory->mMutex);
        for (auto *query : mQueries)
        {
            query->acceptUse(mLane, mValue);
        }
        mHistory->mAcceptedRelease = 0;
        mHistory->mRelease.buffers.clear();
        mHistory->mRelease.images.clear();
        for (auto &entry : mBuffers)
        {
            mHistory->mBuffers.at(entry.first).swap(entry.second);
        }
        for (auto &entry : mTextures)
        {
            auto &target = mHistory->mTextures.at(entry.first);
            target.planeAspects.swap(entry.second.planeAspects);
            eastl::swap(target.subresourceCountPerPlane, entry.second.subresourceCountPerPlane);
            target.subresources.swap(entry.second.subresources);
        }
        mHistory = nullptr;
    }
} // namespace GVM::RHI::Vulkan
