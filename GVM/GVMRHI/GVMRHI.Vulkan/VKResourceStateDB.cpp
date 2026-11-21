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
    bool operator==(const VKResourceQueueState &left, const VKResourceQueueState &right)
    {
        return left.lastExclusiveQueueId == right.lastExclusiveQueueId &&
            left.lastExclusiveValue == right.lastExclusiveValue &&
            left.lastReadValues == right.lastReadValues &&
            left.ownerFamily == right.ownerFamily && left.releaseQueueId == right.releaseQueueId;
    }

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
                    if (range.queueState.lastExclusiveQueueId == lane)
                    {
                        writer = eastl::max(writer, range.queueState.lastExclusiveValue);
                    }
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
        mAcceptedReleases.fill(0);
        mForeignWaits.fill(0);
        for (auto &release : mReleases)
        {
            release = {};
        }
        mQueries.clear();
        mQueueIndex = UINT32_MAX;
        mQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        mHistory = nullptr;
    }

    void VKResourceStateDB::beginSubmission(VKResourceStateDB &history, uint32_t queueIndex, uint32_t queueFamily, uint64_t value)
    {
        if (&history == this || mHistory != nullptr || !mBuffers.empty() || !mTextures.empty())
        {
            throw std::logic_error("A submission resource overlay must start empty and borrow a distinct history.");
        }
        if (queueIndex >= MaxDeviceQueues || queueFamily == VK_QUEUE_FAMILY_IGNORED || value == 0)
        {
            throw std::invalid_argument("A submission resource overlay requires a valid queue index, family and nonzero timeline value.");
        }
        mHistory = &history;
        mQueueIndex = queueIndex;
        mQueueFamily = queueFamily;
        mValue = value;
        mForeignWaits.fill(0);
    }

    bool VKResourceStateDB::prepareQueueAccess(VKResourceQueueState &state, bool exclusive, bool concurrent)
    {
        if (mHistory == nullptr)
        {
            throw std::logic_error("Queue access requires an active submission resource overlay.");
        }
        const bool transfer = !concurrent && state.ownerFamily != VK_QUEUE_FAMILY_IGNORED && state.ownerFamily != mQueueFamily;
        if (state.lastExclusiveValue != 0 && state.lastExclusiveQueueId != mQueueIndex)
        {
            auto &wait = mForeignWaits[state.lastExclusiveQueueId];
            wait = eastl::max(wait, state.lastExclusiveValue);
        }
        if (exclusive || transfer)
        {
            for (uint32_t index = 0; index < MaxDeviceQueues; ++index)
            {
                if (index != mQueueIndex)
                {
                    mForeignWaits[index] = eastl::max(mForeignWaits[index], state.lastReadValues[index]);
                }
            }
            state.lastExclusiveQueueId = mQueueIndex;
            state.lastExclusiveValue = mValue;
            state.lastReadValues.fill(0);
        }
        else
        {
            state.lastReadValues[mQueueIndex] = mValue;
        }
        state.ownerFamily = mQueueFamily;
        state.releaseQueueId = mQueueIndex;
        return transfer;
    }

    const eastl::array<uint64_t, MaxDeviceQueues> &VKResourceStateDB::queueWaitValues() const
    {
        return mForeignWaits;
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

    VKOwnershipRelease &VKResourceStateDB::prepareOwnershipRelease(const VKResourceQueueState &previous)
    {
        auto &release = mReleases.at(previous.releaseQueueId);
        if (previous.lastExclusiveValue != 0)
        {
            auto &wait = release.waits[previous.lastExclusiveQueueId];
            wait = eastl::max(wait, previous.lastExclusiveValue);
        }
        for (uint32_t index = 0; index < MaxDeviceQueues; ++index)
        {
            release.waits[index] = eastl::max(release.waits[index], previous.lastReadValues[index]);
        }
        return release;
    }

    void VKResourceStateDB::transferOwnership(vk::BufferMemoryBarrier2 &acquire, const VKBuffer &buffer, const VKResourceQueueState &previous)
    {
        auto &release = prepareOwnershipRelease(previous);
        if (release.bufferOwners.empty() || release.bufferOwners.back() != buffer.getHandle())
        {
            release.bufferOwners.push_back(buffer.getHandle());
        }
        release.buffers.push_back(makeRelease(acquire, previous.ownerFamily, mQueueFamily));
    }

    void VKResourceStateDB::transferOwnership(vk::ImageMemoryBarrier2 &acquire, const VKTexture &texture, const VKResourceQueueState &previous)
    {
        auto &release = prepareOwnershipRelease(previous);
        if (release.imageOwners.empty() || release.imageOwners.back() != texture.getHandle())
        {
            release.imageOwners.push_back(texture.getHandle());
        }
        release.images.push_back(makeRelease(acquire, previous.ownerFamily, mQueueFamily));
    }

    void VKResourceStateDB::prepareQueueWaits(VKDevice &device, const eastl::vector<CommandEncoder> &users, eastl::vector<vk::SemaphoreSubmitInfo> &waits)
    {
        for (const auto &encoder : users)
        {
            for (const auto &query : static_cast<VKCommandEncoder *>(encoder.get())->getRetainedQuerySets())
            {
                auto *native = static_cast<VKQuerySet *>(query.get());
                for (uint32_t index = 0; index < device.queueCount(); ++index)
                {
                    if (index != mQueueIndex)
                    {
                        mForeignWaits[index] = eastl::max(mForeignWaits[index], native->lastUse(index));
                    }
                }
                mQueries.push_back(native);
            }
        }
        for (uint32_t index = 0; index < device.queueCount(); ++index)
        {
            auto &release = mReleases[index];
            if (!release.buffers.empty() || !release.images.empty())
            {
                const uint64_t value = device.queue(index).submitOwnershipRelease(release);
                mForeignWaits[index] = eastl::max(mForeignWaits[index], value);
                // Preserve every accepted source release if a later release or the consumer fails.
                ScopedLock lock(mHistory->mMutex);
                mHistory->mAcceptedReleases[index] = value;
                eastl::swap(mHistory->mReleases[index], release);
            }
        }
        for (uint32_t index = 0; index < device.queueCount(); ++index)
        {
            if (index != mQueueIndex && mForeignWaits[index] != 0)
            {
                mergeTimelineWait(waits, device.queue(index).timelineWait(mForeignWaits[index]));
            }
        }
    }

    bool VKResourceStateDB::hasUnacquiredRelease() const
    {
        ScopedLock lock(mMutex);
        for (uint64_t value : mAcceptedReleases)
        {
            if (value != 0)
            {
                return true;
            }
        }
        return false;
    }

    void VKResourceStateDB::commitSubmission() noexcept
    {
        ScopedLock lock(mMutex);
        ScopedLock historyLock(mHistory->mMutex);
        for (auto *query : mQueries)
        {
            query->acceptUse(mQueueIndex, mValue);
        }
        mHistory->mAcceptedReleases.fill(0);
        for (auto &release : mHistory->mReleases)
        {
            release = {};
        }
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
