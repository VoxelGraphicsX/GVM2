#pragma once

#include "VKCommon.hpp"
#include "VKResourceStateDB.hpp"
#include "VKTaskDependencyResolver.Diagnostics.hpp"
#include "VKTextureSubresourceUtils.hpp"

#include <EASTL/unordered_map.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKTaskDependencyResolver final
    {
    public:
        struct BufferRangeSyncRequest
        {
            VKBuffer *buffer = nullptr;
            uint64_t offset = 0u;
            uint64_t size = 0u;
            vk::PipelineStageFlags requiredStageMask = {};
            vk::AccessFlags requiredAccessMask = {};
        };

        void reset();
        void init(const VKQueue &queue, VKResourceStateDB &stateDB);

        void synchronizeBufferRange(
            vk::CommandBuffer commandBuffer,
            VKBuffer &buffer,
            uint64_t offset,
            uint64_t size,
            vk::PipelineStageFlags requiredStageMask,
            vk::AccessFlags requiredAccessMask);

        void synchronizeBufferRanges(
            vk::CommandBuffer commandBuffer,
            const eastl::vector<BufferRangeSyncRequest> &requests);

        void synchronizeTextureSubresources(
            vk::CommandBuffer commandBuffer,
            VKTexture &texture,
            vk::ImageLayout targetLayout,
            vk::PipelineStageFlags requiredStageMask,
            vk::AccessFlags requiredAccessMask,
            TextureAspectFlags aspectMask = TextureAspect::All,
            uint32_t baseMipLevel = 0u,
            uint32_t mipLevelCount = 0u,
            uint32_t baseArrayLayer = 0u,
            uint32_t arrayLayerCount = 0u);

        static bool transitionImageState(
            vk::CommandBuffer commandBuffer,
            vk::Image image,
            const vk::ImageSubresourceRange &subresourceRange,
            vk::ImageLayout &layout,
            vk::PipelineStageFlags &stageMask,
            vk::AccessFlags &accessMask,
            vk::ImageLayout newLayout,
            vk::PipelineStageFlags newStageMask,
            vk::AccessFlags newAccessMask);

        void transitionTextureLayout(
            vk::CommandBuffer commandBuffer,
            VKTexture &texture,
            vk::ImageLayout targetLayout,
            TextureAspectFlags aspectMask = TextureAspect::All,
            uint32_t baseMipLevel = 0u,
            uint32_t mipLevelCount = 0u,
            uint32_t baseArrayLayer = 0u,
            uint32_t arrayLayerCount = 0u);

        void restoreTextureSteadyStateLayout(
            vk::CommandBuffer commandBuffer,
            VKTexture &texture,
            TextureAspectFlags aspectMask = TextureAspect::All,
            uint32_t baseMipLevel = 0u,
            uint32_t mipLevelCount = 0u,
            uint32_t baseArrayLayer = 0u,
            uint32_t arrayLayerCount = 0u);

        [[nodiscard]]
        VKResourceFinalStateBatch finalize(vk::CommandBuffer commandBuffer);

        void recordTrackedBufferWriteCompletionBarriers(vk::CommandBuffer commandBuffer) const;

    private:
        struct PendingBufferBarrier
        {
            VKBuffer *buffer = nullptr;
            vk::BufferMemoryBarrier barrier = {};
            vk::PipelineStageFlags srcStageMask = {};
            vk::PipelineStageFlags dstStageMask = {};
            bool writeInvolved = false;
            bool outstandingWrite = false;
            bool suspiciousRepeatedReadBarrier = false;
        };

        struct PendingTextureBarrier
        {
            const VKTexture *texture = nullptr;
            vk::ImageMemoryBarrier barrier = {};
            vk::PipelineStageFlags srcStageMask = {};
            vk::PipelineStageFlags dstStageMask = {};
        };

        [[nodiscard]]
        eastl::vector<VKResourceBufferState> &getOrCreateBufferStates(VKBuffer &buffer);

        [[nodiscard]]
        bool collectBufferRangeSyncBarriers(
            const BufferRangeSyncRequest &request,
            eastl::vector<PendingBufferBarrier> &outPendingBarriers,
            uint64_t &outOutstandingWriteConsumedCount);

        void flushPendingBufferBarriers(
            vk::CommandBuffer commandBuffer,
            eastl::vector<PendingBufferBarrier> &pendingBufferBarriers);

        static void coalescePendingBufferBarriers(eastl::vector<PendingBufferBarrier> &pendingBufferBarriers);
        [[nodiscard]]
        static bool pendingBufferBarrierBatchLess(
            const PendingBufferBarrier &lhs,
            const PendingBufferBarrier &rhs);
        [[nodiscard]]
        static bool containsOverlappingPendingBufferBarrier(
            const eastl::vector<PendingBufferBarrier> &existingBarriers,
            const eastl::vector<PendingBufferBarrier> &incomingBarriers);

        [[nodiscard]]
        VKResourceTextureState &getOrCreateTextureState(VKTexture &texture);
        void collectTextureSubresourceSyncBarriers(
            VKTexture &texture,
            VKResourceTextureState &state,
            const ResolvedTextureSubresourceRange &resolvedSubresources,
            vk::ImageLayout targetLayout,
            vk::PipelineStageFlags requiredStageMask,
            vk::AccessFlags requiredAccessMask,
            eastl::vector<PendingTextureBarrier> &outPendingBarriers);
        void appendPendingTextureBarrierRange(
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
            eastl::vector<PendingTextureBarrier> &outPendingBarriers) const;
        void flushPendingTextureBarriers(
            vk::CommandBuffer commandBuffer,
            eastl::vector<PendingTextureBarrier> &pendingTextureBarriers);
        static void coalescePendingTextureBarriers(eastl::vector<PendingTextureBarrier> &pendingTextureBarriers);

        [[nodiscard]]
        static bool textureNeedsSteadyStateLayoutRestore(const VKResourceTextureState &state);

        eastl::unordered_map<const VKBuffer *, eastl::vector<VKResourceBufferState>> mBufferStates;
        eastl::unordered_map<const VKTexture *, VKResourceTextureState> mTextureStates;
        TaskDependencyResolverDiagnostics mDiagnostics = {};
        const VKQueue *mQueue = nullptr;
        VKResourceStateDB *mStateDB = nullptr;
    };
} // namespace GVM::RHI::Vulkan
