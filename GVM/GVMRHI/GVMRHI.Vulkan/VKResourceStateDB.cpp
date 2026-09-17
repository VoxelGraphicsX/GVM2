#include "VKResourceStateDB.hpp"

#include "VKBuffer.hpp"
#include "VKBufferRangeStateUtils.hpp"
#include "VKCommandEncoder.hpp"
#include "VKEnumUtils.hpp"
#include "VKSyncUtils.hpp"
#include "VKTexture.hpp"
#include "VKTextureSubresourceUtils.hpp"

#include <EASTL/algorithm.h>
namespace GVM::RHI::Vulkan
{
    namespace
    {
        bool finalBufferStatePayloadMatches(
            const VKResourceBufferState &lhs,
            const VKResourceBufferState &rhs)
        {
            return lhs.buffer == rhs.buffer &&
                lhs.stageMask == rhs.stageMask &&
                lhs.accessMask == rhs.accessMask &&
                lhs.lastWriteStageMask == rhs.lastWriteStageMask &&
                lhs.lastWriteAccessMask == rhs.lastWriteAccessMask &&
                lhs.initialized == rhs.initialized &&
                lhs.lastWriteInitialized == rhs.lastWriteInitialized;
        }

        void overlayFinalBufferState(
            eastl::vector<VKResourceBufferState> &states,
            VKResourceBufferState state)
        {
            if (state.buffer == nullptr || state.size == 0u)
            {
                return;
            }

            overlayBufferRangeState(states, state, finalBufferStatePayloadMatches);
        }

        void writeTextureStateRange(
            VKResourceTextureState &state,
            const VKTexture *texture,
            const ResolvedTextureSubresourceRange &resolvedSubresources,
            vk::ImageLayout layout,
            vk::PipelineStageFlags stageMask,
            vk::AccessFlags accessMask)
        {
            if (texture == nullptr || state.subresourceCountPerPlane == 0u)
            {
                return;
            }

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
                    for (uint32_t arrayLayer = resolvedSubresources.baseArrayLayer; arrayLayer < resolvedSubresources.arrayLayerEnd(); ++arrayLayer)
                    {
                        const size_t stateIndex =
                            planeOffset +
                            static_cast<size_t>(resolveTextureSubresourceIndex(texture, mipLevel, arrayLayer));
                        if (stateIndex >= state.layouts.size())
                        {
                            continue;
                        }

                        state.layouts[stateIndex] = layout;
                        state.stageMasks[stateIndex] = stageMask;
                        state.accessMasks[stateIndex] = accessMask;
                    }
                }
            }
        }

        bool tryResolveSharedTextureStateRange(
            const VKResourceTextureState &state,
            const VKTexture *texture,
            const ResolvedTextureSubresourceRange &resolvedSubresources,
            vk::ImageLayout &outLayout,
            vk::PipelineStageFlags &outStageMask,
            vk::AccessFlags &outAccessMask)
        {
            bool hasSharedState = false;
            vk::ImageLayout sharedLayout = vk::ImageLayout::eUndefined;
            vk::PipelineStageFlags sharedStageMask = {};
            vk::AccessFlags sharedAccessMask = {};
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
                    for (uint32_t arrayLayer = resolvedSubresources.baseArrayLayer; arrayLayer < resolvedSubresources.arrayLayerEnd(); ++arrayLayer)
                    {
                        const size_t stateIndex =
                            planeOffset +
                            static_cast<size_t>(resolveTextureSubresourceIndex(texture, mipLevel, arrayLayer));
                        if (stateIndex >= state.layouts.size())
                        {
                            continue;
                        }

                        const vk::ImageLayout candidateLayout = state.layouts[stateIndex];
                        const vk::PipelineStageFlags candidateStageMask = state.stageMasks[stateIndex];
                        const vk::AccessFlags candidateAccessMask = state.accessMasks[stateIndex];
                        if (!hasSharedState)
                        {
                            sharedLayout = candidateLayout;
                            sharedStageMask = candidateStageMask;
                            sharedAccessMask = candidateAccessMask;
                            hasSharedState = true;
                            continue;
                        }

                        if (sharedLayout != candidateLayout ||
                            sharedStageMask != candidateStageMask ||
                            sharedAccessMask != candidateAccessMask)
                        {
                            return false;
                        }
                    }
                }
            }

            if (!hasSharedState)
            {
                return false;
            }

            outLayout = sharedLayout;
            outStageMask = sharedStageMask;
            outAccessMask = sharedAccessMask;
            return true;
        }

        // Overlays the matching buffer states from one finalized batch onto a resolved query view.
        bool applyMatchingBufferStatesFromBatch(
            const VKResourceFinalStateBatch &stateBatch,
            const VKBuffer &buffer,
            eastl::vector<VKResourceBufferState> &outStates)
        {
            bool applied = false;
            for (const VKResourceBufferState &bufferState : stateBatch.buffers)
            {
                if (bufferState.buffer != &buffer || !bufferState.initialized || bufferState.size == 0u)
                {
                    continue;
                }

                overlayFinalBufferState(outStates, bufferState);
                applied = true;
            }
            return applied;
        }

        // Replaces the resolved texture view with the latest matching finalized batch state.
        void applyMatchingTextureStateFromBatch(
            const VKResourceFinalStateBatch &stateBatch,
            const VKTexture &texture,
            VKResourceTextureState &outState)
        {
            for (const VKResourceTextureState &textureState : stateBatch.textures)
            {
                if (textureState.texture != &texture)
                {
                    continue;
                }

                outState = textureState;
            }
        }

        // Removes one encoder state entry from a lifecycle bucket.
        bool takePendingStateEntry(
            eastl::vector<VKResourcePendingStateEntry> &entries,
            const VKCommandEncoder &encoder,
            VKResourcePendingStateEntry &outEntry)
        {
            const auto entryIt = eastl::find_if(
                entries.begin(),
                entries.end(),
                [&encoder](const VKResourcePendingStateEntry &entry)
                {
                    return entry.encoder == &encoder;
                });
            if (entryIt == entries.end())
            {
                return false;
            }

            outEntry = eastl::move(*entryIt);
            entries.erase(entryIt);
            return true;
        }

        // Removes one encoder entry from all lifecycle buckets, preferring the earliest-stage match.
        bool takePendingStateEntryFromBuckets(
            eastl::vector<VKResourcePendingStateEntry> &recordedEntries,
            eastl::vector<VKResourcePendingStateEntry> &pendingSubmitEntries,
            eastl::vector<VKResourcePendingStateEntry> &submittedEntries,
            const VKCommandEncoder &encoder,
            VKResourcePendingStateEntry &outEntry)
        {
            bool foundEntry = takePendingStateEntry(recordedEntries, encoder, outEntry);

            VKResourcePendingStateEntry discardedEntry = {};
            if (takePendingStateEntry(pendingSubmitEntries, encoder, discardedEntry) && !foundEntry)
            {
                outEntry = eastl::move(discardedEntry);
                foundEntry = true;
            }

            if (takePendingStateEntry(submittedEntries, encoder, discardedEntry) && !foundEntry)
            {
                outEntry = eastl::move(discardedEntry);
                foundEntry = true;
            }

            return foundEntry;
        }

        // Inserts or replaces the lifecycle bucket entry for one encoder.
        void upsertPendingStateEntry(
            eastl::vector<VKResourcePendingStateEntry> &entries,
            VKResourcePendingStateEntry entry)
        {
            const auto entryIt = eastl::find_if(
                entries.begin(),
                entries.end(),
                [&entry](const VKResourcePendingStateEntry &existingEntry)
                {
                    return existingEntry.encoder == entry.encoder;
                });
            if (entryIt != entries.end())
            {
                *entryIt = eastl::move(entry);
                return;
            }

            entries.push_back(eastl::move(entry));
        }

        // Applies all matching buffer states from one lifecycle bucket.
        bool applyMatchingBufferStatesFromEntries(
            const eastl::vector<VKResourcePendingStateEntry> &entries,
            const VKBuffer &buffer,
            eastl::vector<VKResourceBufferState> &outStates)
        {
            bool applied = false;
            for (const VKResourcePendingStateEntry &entry : entries)
            {
                applied = applyMatchingBufferStatesFromBatch(entry.stateBatch, buffer, outStates) || applied;
            }
            return applied;
        }

        // Applies all matching texture states from one lifecycle bucket.
        void applyMatchingTextureStateFromEntries(
            const eastl::vector<VKResourcePendingStateEntry> &entries,
            const VKTexture &texture,
            VKResourceTextureState &outState)
        {
            for (const VKResourcePendingStateEntry &entry : entries)
            {
                applyMatchingTextureStateFromBatch(entry.stateBatch, texture, outState);
            }
        }

        // Removes all pending bucket buffer states that reference one buffer.
        void discardBufferStatesFromEntries(
            eastl::vector<VKResourcePendingStateEntry> &entries,
            const VKBuffer &buffer)
        {
            for (VKResourcePendingStateEntry &entry : entries)
            {
                const auto newEnd = eastl::remove_if(
                    entry.stateBatch.buffers.begin(),
                    entry.stateBatch.buffers.end(),
                    [&buffer](const VKResourceBufferState &state)
                    {
                        return state.buffer == &buffer;
                    });
                if (newEnd != entry.stateBatch.buffers.end())
                {
                    entry.stateBatch.buffers.erase(newEnd, entry.stateBatch.buffers.end());
                }
            }
        }

        // Removes all pending bucket texture states that reference one texture.
        void discardTextureStatesFromEntries(
            eastl::vector<VKResourcePendingStateEntry> &entries,
            const VKTexture &texture)
        {
            for (VKResourcePendingStateEntry &entry : entries)
            {
                const auto newEnd = eastl::remove_if(
                    entry.stateBatch.textures.begin(),
                    entry.stateBatch.textures.end(),
                    [&texture](const VKResourceTextureState &state)
                    {
                        return state.texture == &texture;
                    });
                if (newEnd != entry.stateBatch.textures.end())
                {
                    entry.stateBatch.textures.erase(newEnd, entry.stateBatch.textures.end());
                }
            }
        }

    } // namespace

    bool VKResourceFinalStateBatch::empty() const
    {
        return buffers.empty() && textures.empty();
    }

    bool VKResourceStateDB::tryGetCurrentBufferStates(
        const VKBuffer &buffer,
        eastl::vector<VKResourceBufferState> &outStates,
        VKResourceBufferStateInitialSource *outSource) const
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        getCurrentBufferStatesUnlocked(buffer, outStates, outSource);
        return true;
    }

    bool VKResourceStateDB::tryGetCurrentBufferState(const VKBuffer &buffer, VKResourceBufferState &outState) const
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        getCurrentBufferStateUnlocked(buffer, outState);
        return true;
    }

    bool VKResourceStateDB::tryGetCurrentTextureState(const VKTexture &texture, VKResourceTextureState &outState) const
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        getCurrentTextureStateUnlocked(texture, outState);
        return true;
    }

    bool VKResourceStateDB::isTextureInSteadyStateLayout(const VKTexture &texture) const
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        return isTextureInSteadyStateLayoutUnlocked(texture);
    }

    void VKResourceStateDB::discardBufferState(const VKBuffer &buffer)
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        discardBufferStateUnlocked(buffer);
    }

    void VKResourceStateDB::discardTextureState(const VKTexture &texture)
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        discardTextureStateUnlocked(texture);
    }

    void VKResourceStateDB::recordExternalBufferUsage(
        VKBuffer &buffer,
        uint64_t offset,
        uint64_t size,
        vk::PipelineStageFlags stageMask,
        vk::AccessFlags accessMask)
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        recordExternalBufferUsageUnlocked(buffer, offset, size, stageMask, accessMask);
    }

    void VKResourceStateDB::recordExternalBufferUsage(
        VKBuffer &buffer,
        vk::PipelineStageFlags stageMask,
        vk::AccessFlags accessMask)
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        recordExternalBufferUsageUnlocked(buffer, 0u, WholeSize, stageMask, accessMask);
    }

    void VKResourceStateDB::recordExternalTextureState(
        VKTexture &texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount,
        vk::ImageLayout layout,
        vk::PipelineStageFlags stageMask,
        vk::AccessFlags accessMask)
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        recordExternalTextureStateUnlocked(
            texture,
            aspectMask,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount,
            layout,
            stageMask,
            accessMask);
    }

    bool VKResourceStateDB::tryGetSharedTextureStateForTransition(
        const VKTexture &texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount,
        vk::ImageLayout &outLayout,
        vk::PipelineStageFlags &outStageMask,
        vk::AccessFlags &outAccessMask) const
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        return tryGetSharedSubresourceStateForTransitionUnlocked(
            texture,
            aspectMask,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount,
            outLayout,
            outStageMask,
            outAccessMask);
    }

    void VKResourceStateDB::registerEndedCommandEncoderState(const VKCommandEncoder &encoder, VKResourceFinalStateBatch stateBatch)
    {
        if (stateBatch.empty())
        {
            return;
        }

        ScopedLock<Mutex> stateDBLock(mMutex);
        VKResourcePendingStateEntry entry = {};
        if (!takePendingStateEntryFromBuckets(
                mRecordedStateEntries,
                mPendingSubmitStateEntries,
                mSubmittedStateEntries,
                encoder,
                entry))
        {
            entry.encoder = &encoder;
        }

        entry.submissionId = 0u;
        entry.stateBatch = eastl::move(stateBatch);
        upsertPendingStateEntry(mRecordedStateEntries, eastl::move(entry));
    }

    void VKResourceStateDB::discardEndedCommandEncoderState(const VKCommandEncoder &encoder)
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        eraseStateEntries(mRecordedStateEntries, encoder);
        eraseStateEntries(mPendingSubmitStateEntries, encoder);
        eraseStateEntries(mSubmittedStateEntries, encoder);
    }

    void VKResourceStateDB::markCommandEncoderStatePendingSubmit(const VKCommandEncoder &encoder)
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        VKResourcePendingStateEntry entry = {};
        if (!takePendingStateEntry(mRecordedStateEntries, encoder, entry))
        {
            return;
        }

        upsertPendingStateEntry(mPendingSubmitStateEntries, eastl::move(entry));
    }

    void VKResourceStateDB::markCommandEncoderStatesSubmitted(uint64_t submissionId, const eastl::vector<const VKCommandEncoder *> &encoders)
    {
        if (submissionId == 0u || encoders.empty())
        {
            return;
        }

        ScopedLock<Mutex> stateDBLock(mMutex);
        for (const VKCommandEncoder *encoder : encoders)
        {
            if (encoder == nullptr)
            {
                continue;
            }

            VKResourcePendingStateEntry entry = {};
            if (!takePendingStateEntry(mPendingSubmitStateEntries, *encoder, entry) &&
                !takePendingStateEntry(mRecordedStateEntries, *encoder, entry))
            {
                continue;
            }

            entry.submissionId = submissionId;
            upsertPendingStateEntry(mSubmittedStateEntries, eastl::move(entry));
        }
    }

    void VKResourceStateDB::retireSubmission(uint64_t submissionId)
    {
        if (submissionId == 0u)
        {
            return;
        }

        ScopedLock<Mutex> stateDBLock(mMutex);
        size_t writeIndex = 0u;
        for (size_t readIndex = 0u; readIndex < mSubmittedStateEntries.size(); ++readIndex)
        {
            if (mSubmittedStateEntries[readIndex].submissionId == submissionId)
            {
                commitStateBatch(mSubmittedStateEntries[readIndex].stateBatch);
                continue;
            }

            if (writeIndex != readIndex)
            {
                mSubmittedStateEntries[writeIndex] = eastl::move(mSubmittedStateEntries[readIndex]);
            }
            ++writeIndex;
        }
        mSubmittedStateEntries.resize(writeIndex);
    }

    void VKResourceStateDB::reset()
    {
        ScopedLock<Mutex> stateDBLock(mMutex);
        mCommittedBufferStates.clear();
        mCommittedTextureStates.clear();
        mRecordedStateEntries.clear();
        mPendingSubmitStateEntries.clear();
        mSubmittedStateEntries.clear();
    }

    void VKResourceStateDB::getCurrentBufferStatesUnlocked(
        const VKBuffer &buffer,
        eastl::vector<VKResourceBufferState> &outStates,
        VKResourceBufferStateInitialSource *outSource) const
    {
        outStates.clear();

        const eastl::vector<VKResourceBufferState> *storedStates = nullptr;
        bool hasQueueLocalState = false;
        bool hasCommandLocalState = false;
        const auto existing = mCommittedBufferStates.find(&buffer);
        if (existing != mCommittedBufferStates.end())
        {
            storedStates = &existing->second;
            hasQueueLocalState = true;
        }

        buildResolvedBufferStates(buffer, storedStates, outStates);

        hasQueueLocalState = applyMatchingBufferStatesFromEntries(mSubmittedStateEntries, buffer, outStates) || hasQueueLocalState;
        hasQueueLocalState = applyMatchingBufferStatesFromEntries(mPendingSubmitStateEntries, buffer, outStates) || hasQueueLocalState;
        hasCommandLocalState = applyMatchingBufferStatesFromEntries(mRecordedStateEntries, buffer, outStates) || hasCommandLocalState;

        mergeAdjacentBufferRangeStates(outStates, finalBufferStatePayloadMatches);
        if (outSource != nullptr)
        {
            *outSource = hasQueueLocalState
                ? VKResourceBufferStateInitialSource::QueueLocal
                : (hasCommandLocalState ? VKResourceBufferStateInitialSource::CommandLocal : VKResourceBufferStateInitialSource::None);
        }
    }

    void VKResourceStateDB::getCurrentBufferStateUnlocked(const VKBuffer &buffer, VKResourceBufferState &outState) const
    {
        eastl::vector<VKResourceBufferState> states;
        getCurrentBufferStatesUnlocked(buffer, states);
        outState = mergeBufferStatesForQuery(buffer, states);
    }

    void VKResourceStateDB::getCurrentTextureStateUnlocked(const VKTexture &texture, VKResourceTextureState &outState) const
    {
        const auto existing = mCommittedTextureStates.find(&texture);
        if (existing != mCommittedTextureStates.end())
        {
            outState = existing->second;
        }
        else
        {
            outState = buildDefaultTextureState(texture);
        }

        applyMatchingTextureStateFromEntries(mSubmittedStateEntries, texture, outState);
        applyMatchingTextureStateFromEntries(mPendingSubmitStateEntries, texture, outState);
        applyMatchingTextureStateFromEntries(mRecordedStateEntries, texture, outState);
    }

    bool VKResourceStateDB::isTextureInSteadyStateLayoutUnlocked(const VKTexture &texture) const
    {
        VKResourceTextureState state = {};
        getCurrentTextureStateUnlocked(texture, state);
        if (state.steadyStateLayout == vk::ImageLayout::eUndefined)
        {
            return true;
        }

        for (vk::ImageLayout layout : state.layouts)
        {
            if (layout != state.steadyStateLayout)
            {
                return false;
            }
        }

        return true;
    }

    void VKResourceStateDB::discardBufferStateUnlocked(const VKBuffer &buffer)
    {
        mCommittedBufferStates.erase(&buffer);
        discardBufferStatesFromEntries(mRecordedStateEntries, buffer);
        discardBufferStatesFromEntries(mPendingSubmitStateEntries, buffer);
        discardBufferStatesFromEntries(mSubmittedStateEntries, buffer);
    }

    void VKResourceStateDB::discardTextureStateUnlocked(const VKTexture &texture)
    {
        mCommittedTextureStates.erase(&texture);
        discardTextureStatesFromEntries(mRecordedStateEntries, texture);
        discardTextureStatesFromEntries(mPendingSubmitStateEntries, texture);
        discardTextureStatesFromEntries(mSubmittedStateEntries, texture);
    }

    void VKResourceStateDB::recordExternalBufferUsageUnlocked(
        VKBuffer &buffer,
        uint64_t offset,
        uint64_t size,
        vk::PipelineStageFlags stageMask,
        vk::AccessFlags accessMask)
    {
        VKResourceBufferState state = {};
        state.buffer = &buffer;
        state.offset = offset;
        state.size = resolveBufferRangeSize(
            "VKResourceStateDB::recordExternalBufferUsage",
            &buffer,
            offset,
            size);
        if (state.size == 0u)
        {
            return;
        }
        state.stageMask = stageMask;
        state.accessMask = accessMask;
        state.initialized = true;
        const vk::AccessFlags writeAccessMask = extractWriteAccessMask(accessMask);
        if (writeAccessMask != vk::AccessFlags{})
        {
            state.lastWriteStageMask = stageMask;
            state.lastWriteAccessMask = writeAccessMask;
            state.lastWriteInitialized = true;
        }

        overlayFinalBufferState(mCommittedBufferStates[&buffer], state);
    }

    void VKResourceStateDB::recordExternalTextureStateUnlocked(
        VKTexture &texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount,
        vk::ImageLayout layout,
        vk::PipelineStageFlags stageMask,
        vk::AccessFlags accessMask)
    {
        VKResourceTextureState state = {};
        getCurrentTextureStateUnlocked(texture, state);

        const ResolvedTextureSubresourceRange resolvedSubresources = resolveTextureSubresourceRange(
            &texture,
            aspectMask,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount);

        vk::ImageLayout sharedLayout = vk::ImageLayout::eUndefined;
        vk::PipelineStageFlags sharedStageMask = {};
        vk::AccessFlags sharedAccessMask = {};
        if (tryResolveSharedTextureStateRange(state, &texture, resolvedSubresources, sharedLayout, sharedStageMask, sharedAccessMask) &&
            sharedLayout == layout &&
            sharedStageMask == stageMask &&
            sharedAccessMask == accessMask)
        {
            return;
        }

        state.texture = &texture;
        writeTextureStateRange(
            state,
            &texture,
            resolvedSubresources,
            layout,
            stageMask,
            accessMask);

        mCommittedTextureStates[&texture] = state;
    }

    bool VKResourceStateDB::tryGetSharedSubresourceStateForTransitionUnlocked(
        const VKTexture &texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount,
        vk::ImageLayout &outLayout,
        vk::PipelineStageFlags &outStageMask,
        vk::AccessFlags &outAccessMask) const
    {
        VKResourceTextureState trackerInitialState = {};
        getCurrentTextureStateUnlocked(texture, trackerInitialState);
        return tryResolveSharedSubresourceState(
            trackerInitialState,
            texture,
            aspectMask,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount,
            outLayout,
            outStageMask,
            outAccessMask);
    }

    bool VKResourceStateDB::tryResolveSharedSubresourceState(
        const VKResourceTextureState &state,
        const VKTexture &texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount,
        vk::ImageLayout &outLayout,
        vk::PipelineStageFlags &outStageMask,
        vk::AccessFlags &outAccessMask)
    {
        const ResolvedTextureSubresourceRange resolvedSubresources = resolveTextureSubresourceRange(
            &texture,
            aspectMask,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount);

        return tryResolveSharedTextureStateRange(
            state,
            &texture,
            resolvedSubresources,
            outLayout,
            outStageMask,
            outAccessMask);
    }
    void VKResourceStateDB::eraseStateEntries(
        eastl::vector<VKResourcePendingStateEntry> &entries,
        const VKCommandEncoder &encoder)
    {
        size_t writeIndex = 0u;
        for (size_t readIndex = 0u; readIndex < entries.size(); ++readIndex)
        {
            if (entries[readIndex].encoder == &encoder)
            {
                continue;
            }

            if (writeIndex != readIndex)
            {
                entries[writeIndex] = eastl::move(entries[readIndex]);
            }
            ++writeIndex;
        }
        entries.resize(writeIndex);
    }

    void VKResourceStateDB::applyStateBatchToStateMaps(
        const VKResourceFinalStateBatch &stateBatch,
        eastl::unordered_map<const VKBuffer *, eastl::vector<VKResourceBufferState>> &bufferStates,
        eastl::unordered_map<const VKTexture *, VKResourceTextureState> &textureStates) const
    {
        for (const VKResourceBufferState &bufferState : stateBatch.buffers)
        {
            if (bufferState.buffer == nullptr || !bufferState.initialized || bufferState.size == 0u)
            {
                continue;
            }

            overlayFinalBufferState(bufferStates[bufferState.buffer], bufferState);
        }

        for (const VKResourceTextureState &textureState : stateBatch.textures)
        {
            if (textureState.texture == nullptr)
            {
                continue;
            }

            textureStates[textureState.texture] = textureState;
        }
    }

    void VKResourceStateDB::commitStateBatch(const VKResourceFinalStateBatch &stateBatch)
    {
        applyStateBatchToStateMaps(stateBatch, mCommittedBufferStates, mCommittedTextureStates);
    }

    VKResourceBufferState VKResourceStateDB::buildDefaultBufferState(const VKBuffer &buffer)
    {
        VKResourceBufferState state = {};
        state.buffer = const_cast<VKBuffer *>(&buffer);
        state.offset = 0u;
        state.size = buffer.getStorageSize();
        return state;
    }

    void VKResourceStateDB::buildResolvedBufferStates(
        const VKBuffer &buffer,
        const eastl::vector<VKResourceBufferState> *storedStates,
        eastl::vector<VKResourceBufferState> &outStates)
    {
        outStates.clear();
        outStates.push_back(buildDefaultBufferState(buffer));
        if (storedStates == nullptr)
        {
            return;
        }

        for (const VKResourceBufferState &state : *storedStates)
        {
            if (state.buffer == nullptr || !state.initialized || state.size == 0u)
            {
                continue;
            }

            overlayFinalBufferState(outStates, state);
        }
        mergeAdjacentBufferRangeStates(outStates, finalBufferStatePayloadMatches);
    }

    VKResourceBufferState VKResourceStateDB::mergeBufferStatesForQuery(
        const VKBuffer &buffer,
        const eastl::vector<VKResourceBufferState> &states)
    {
        if (states.empty())
        {
            return buildDefaultBufferState(buffer);
        }

        if (states.size() == 1u)
        {
            return states.front();
        }

        VKResourceBufferState mergedState = buildDefaultBufferState(buffer);
        for (const VKResourceBufferState &state : states)
        {
            mergedState.stageMask |= state.stageMask;
            mergedState.accessMask |= state.accessMask;
            mergedState.initialized = mergedState.initialized || state.initialized;
            mergedState.lastWriteStageMask |= state.lastWriteStageMask;
            mergedState.lastWriteAccessMask |= state.lastWriteAccessMask;
            mergedState.lastWriteInitialized = mergedState.lastWriteInitialized || state.lastWriteInitialized;
        }
        return mergedState;
    }

    VKResourceTextureState VKResourceStateDB::buildDefaultTextureState(const VKTexture &texture)
    {
        VKResourceTextureState state = {};
        state.texture = const_cast<VKTexture *>(&texture);
        state.steadyStateLayout = texture.getSteadyStateLayout();

        state.planeAspects = resolveTexturePlaneAspects(&texture);
        state.subresourceCountPerPlane = resolveTexturePlaneSubresourceCount(&texture);
        const size_t trackedSubresourceCount =
            state.planeAspects.size() * static_cast<size_t>(state.subresourceCountPerPlane);
        state.layouts.assign(trackedSubresourceCount, vk::ImageLayout::eUndefined);
        state.stageMasks.assign(trackedSubresourceCount, vk::PipelineStageFlags{});
        state.accessMasks.assign(trackedSubresourceCount, vk::AccessFlags{});
        return state;
    }
} // namespace GVM::RHI::Vulkan
