#pragma once
#include "VKCommon.hpp"
#include "VKResourceStateDB.hpp"
#include "VKTaskDependencyResolverDiagnostics.hpp"
#include <EASTL/unordered_set.h>

namespace GVM::RHI::Vulkan
{
    /// Generates precise synchronization2 dependencies while lowering ordered commands.
    class VKTaskDependencyResolver final
    {
    public:
        /// Describes one bound byte range used by a dispatch or render pass.
        struct BufferRangeSyncRequest
        {
            VKBuffer *buffer = nullptr;
            uint64_t offset = 0, size = 0;
            vk::PipelineStageFlags2 requiredStageMask = {};
            vk::AccessFlags2 requiredAccessMask = {};
        };
        /** Describes the exact view subresources and shader access used by one immutable resource capture. */
        struct TextureSubresourceSyncRequest
        {
            VKTextureView *view = nullptr;
            vk::ImageLayout layout = vk::ImageLayout::eUndefined;
            vk::PipelineStageFlags2 stages{};
            vk::AccessFlags2 accesses{};
        };
        /// Clears per-encoder diagnostics, never the queue's resource state.
        void reset();
        /// Binds this encoder to the single queue state table.
        void init(const VKQueue &queue, VKResourceStateDB &stateDB);
        /// Makes the selected buffer range ready for its actual next access.
        void synchronizeBufferRange(vk::CommandBuffer commandBuffer, VKBuffer &buffer, uint64_t offset, uint64_t size, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses);
        /// Unions all uses at one command boundary before advancing each byte interval once; scratch lists may be moved in.
        void synchronizeBufferRanges(vk::CommandBuffer commandBuffer, eastl::vector<BufferRangeSyncRequest> requests);
        /** Unions prechecked view uses before advancing each image cell once; all uses of one image must share a layout. */
        void synchronizeTextureViews(vk::CommandBuffer commandBuffer, eastl::vector<TextureSubresourceSyncRequest> requests);
        /// Transitions and synchronizes only the specified image subresources.
        void synchronizeTextureSubresources(vk::CommandBuffer commandBuffer, VKTexture &texture, vk::ImageLayout layout, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses, TextureAspectFlags aspect = TextureAspect::All, uint32_t mip = 0, uint32_t mipCount = 0, uint32_t layer = 0, uint32_t layerCount = 0);
        /// Handles externally owned WSI images using their explicit access scope.
        static bool transitionImageState(vk::CommandBuffer commandBuffer, vk::Image image, const vk::ImageSubresourceRange &range, vk::ImageLayout &layout, vk::PipelineStageFlags2 &stages, vk::AccessFlags2 &accesses, vk::ImageLayout newLayout, vk::PipelineStageFlags2 newStages, vk::AccessFlags2 newAccesses);
        /// Makes touched CPU-readable buffers visible to the host and reports encoder synchronization metrics.
        void finalize(vk::CommandBuffer commandBuffer);

    private:
        const VKQueue *mQueue = nullptr;
        VKResourceStateDB *mStateDB = nullptr;
        TaskDependencyResolverDiagnostics mDiagnostics;
        eastl::unordered_set<VKBuffer *> mBuffers;
        eastl::unordered_set<const VKTexture *> mTextures;
    };
} // namespace GVM::RHI::Vulkan
