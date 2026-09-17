#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/unordered_map.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    enum class VKResourceBufferStateInitialSource : uint8_t
    {
        None,
        QueueLocal,
        CommandLocal,
    };

    // Tracks the resolved hazard state for one buffer range.
    struct VKResourceBufferState
    {
        VKBuffer *buffer = nullptr;
        uint64_t offset = 0u;
        uint64_t size = 0u;
        vk::PipelineStageFlags stageMask = {};
        vk::AccessFlags accessMask = {};
        vk::PipelineStageFlags lastWriteStageMask = {};
        vk::AccessFlags lastWriteAccessMask = {};
        bool initialized = false;
        bool lastWriteInitialized = false;
        VKResourceBufferStateInitialSource initialSource = VKResourceBufferStateInitialSource::None;
    };

    // Tracks per-subresource layout and access state for a texture.
    struct VKResourceTextureState
    {
        VKTexture *texture = nullptr;
        vk::ImageLayout steadyStateLayout = vk::ImageLayout::eUndefined;
        eastl::vector<vk::ImageAspectFlagBits> planeAspects;
        eastl::vector<vk::ImageLayout> layouts;
        eastl::vector<vk::PipelineStageFlags> stageMasks;
        eastl::vector<vk::AccessFlags> accessMasks;
        uint32_t subresourceCountPerPlane = 0u;
        bool touched = false;
    };

    // Holds the finalized resource states produced by one ended command encoder.
    struct VKResourceFinalStateBatch
    {
        eastl::vector<VKResourceBufferState> buffers;
        eastl::vector<VKResourceTextureState> textures;

        [[nodiscard]] bool empty() const;
    };

    // Carries one encoder state batch through recorded, pending-submit, and submitted buckets.
    struct VKResourcePendingStateEntry
    {
        const VKCommandEncoder *encoder = nullptr;
        uint64_t submissionId = 0u;
        VKResourceFinalStateBatch stateBatch;
    };

    class VKResourceStateDB final
    {
    public:
        [[nodiscard]]
        bool tryGetCurrentBufferStates(
            const VKBuffer &buffer,
            eastl::vector<VKResourceBufferState> &outStates,
            VKResourceBufferStateInitialSource *outSource = nullptr) const;
        [[nodiscard]]
        bool tryGetCurrentBufferState(const VKBuffer &buffer, VKResourceBufferState &outState) const;
        [[nodiscard]]
        bool tryGetCurrentTextureState(const VKTexture &texture, VKResourceTextureState &outState) const;
        [[nodiscard]]
        bool isTextureInSteadyStateLayout(const VKTexture &texture) const;
        void discardBufferState(const VKBuffer &buffer);
        void discardTextureState(const VKTexture &texture);
        void recordExternalBufferUsage(VKBuffer &buffer, uint64_t offset, uint64_t size, vk::PipelineStageFlags stageMask, vk::AccessFlags accessMask);
        void recordExternalBufferUsage(VKBuffer &buffer, vk::PipelineStageFlags stageMask, vk::AccessFlags accessMask);
        void recordExternalTextureState(VKTexture &texture, TextureAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t mipLevelCount, uint32_t baseArrayLayer, uint32_t arrayLayerCount, vk::ImageLayout layout, vk::PipelineStageFlags stageMask, vk::AccessFlags accessMask);
        [[nodiscard]]
        bool tryGetSharedTextureStateForTransition(const VKTexture &texture, TextureAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t mipLevelCount, uint32_t baseArrayLayer, uint32_t arrayLayerCount, vk::ImageLayout &outLayout, vk::PipelineStageFlags &outStageMask, vk::AccessFlags &outAccessMask) const;
        void registerEndedCommandEncoderState(const VKCommandEncoder &encoder, VKResourceFinalStateBatch stateBatch);
        void discardEndedCommandEncoderState(const VKCommandEncoder &encoder);
        void markCommandEncoderStatePendingSubmit(const VKCommandEncoder &encoder);
        void markCommandEncoderStatesSubmitted(uint64_t submissionId, const eastl::vector<const VKCommandEncoder *> &encoders);
        void retireSubmission(uint64_t submissionId);
        void reset();

    private:
        void eraseStateEntries(
            eastl::vector<VKResourcePendingStateEntry> &entries,
            const VKCommandEncoder &encoder);
        void getCurrentBufferStatesUnlocked(
            const VKBuffer &buffer,
            eastl::vector<VKResourceBufferState> &outStates,
            VKResourceBufferStateInitialSource *outSource = nullptr) const;
        void getCurrentBufferStateUnlocked(const VKBuffer &buffer, VKResourceBufferState &outState) const;
        void getCurrentTextureStateUnlocked(const VKTexture &texture, VKResourceTextureState &outState) const;
        [[nodiscard]]
        bool isTextureInSteadyStateLayoutUnlocked(const VKTexture &texture) const;
        [[nodiscard]]
        bool tryGetSharedSubresourceStateForTransitionUnlocked(const VKTexture &texture, TextureAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t mipLevelCount, uint32_t baseArrayLayer, uint32_t arrayLayerCount, vk::ImageLayout &outLayout, vk::PipelineStageFlags &outStageMask, vk::AccessFlags &outAccessMask) const;
        void discardBufferStateUnlocked(const VKBuffer &buffer);
        void discardTextureStateUnlocked(const VKTexture &texture);
        void recordExternalBufferUsageUnlocked(VKBuffer &buffer, uint64_t offset, uint64_t size, vk::PipelineStageFlags stageMask, vk::AccessFlags accessMask);
        void recordExternalTextureStateUnlocked(VKTexture &texture, TextureAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t mipLevelCount, uint32_t baseArrayLayer, uint32_t arrayLayerCount, vk::ImageLayout layout, vk::PipelineStageFlags stageMask, vk::AccessFlags accessMask);
        void applyStateBatchToStateMaps(
            const VKResourceFinalStateBatch &stateBatch,
            eastl::unordered_map<const VKBuffer *, eastl::vector<VKResourceBufferState>> &bufferStates,
            eastl::unordered_map<const VKTexture *, VKResourceTextureState> &textureStates) const;
        void commitStateBatch(const VKResourceFinalStateBatch &stateBatch);
        [[nodiscard]]
        static bool tryResolveSharedSubresourceState(
            const VKResourceTextureState &state,
            const VKTexture &texture,
            TextureAspectFlags aspectMask,
            uint32_t baseMipLevel,
            uint32_t mipLevelCount,
            uint32_t baseArrayLayer,
            uint32_t arrayLayerCount,
            vk::ImageLayout &outLayout,
            vk::PipelineStageFlags &outStageMask,
            vk::AccessFlags &outAccessMask);
        static void buildResolvedBufferStates(
            const VKBuffer &buffer,
            const eastl::vector<VKResourceBufferState> *storedStates,
            eastl::vector<VKResourceBufferState> &outStates);
        [[nodiscard]]
        static VKResourceBufferState mergeBufferStatesForQuery(
            const VKBuffer &buffer,
            const eastl::vector<VKResourceBufferState> &states);

        [[nodiscard]]
        static VKResourceBufferState buildDefaultBufferState(const VKBuffer &buffer);
        [[nodiscard]]
        static VKResourceTextureState buildDefaultTextureState(const VKTexture &texture);

        mutable Mutex mMutex;
        eastl::unordered_map<const VKBuffer *, eastl::vector<VKResourceBufferState>> mCommittedBufferStates;
        eastl::unordered_map<const VKTexture *, VKResourceTextureState> mCommittedTextureStates;
        eastl::vector<VKResourcePendingStateEntry> mRecordedStateEntries;
        eastl::vector<VKResourcePendingStateEntry> mPendingSubmitStateEntries;
        eastl::vector<VKResourcePendingStateEntry> mSubmittedStateEntries;
    };
} // namespace GVM::RHI::Vulkan
