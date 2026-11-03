#pragma once
#include "VKCommon.hpp"
#include "VKResourceStateDB.hpp"
#include "VKTaskDependencyResolverDiagnostics.hpp"
#include <EASTL/unordered_map.h>

namespace GVM::RHI::Vulkan
{
    /// Resolves encoder-local dependencies and records unresolved entry scopes for ordered submission reconciliation.
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
        /// Starts independent native recording without reading or modifying the device resource history.
        void init(const VKQueue &queue);
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
        /// Builds this encoder's entry barriers and advances only the supplied uncommitted submission overlay.
        void prepareSubmission(VKResourceStateDB &state, eastl::vector<vk::BufferMemoryBarrier2> &buffers,
            eastl::vector<vk::ImageMemoryBarrier2> &images) const;
        /// Appends the final acquired-image transition after every encoder in the submission has been reconciled.
        void preparePresent(VKResourceStateDB &state, const VKTexture &texture, eastl::vector<vk::ImageMemoryBarrier2> &images) const;

    private:
        /// Stores accesses requiring incoming synchronization and the exact state produced inside the encoder.
        struct AccessSummary
        {
            VKAccessScope entry;
            VKResourceAccessState exit;
            vk::ImageLayout entryLayout = vk::ImageLayout::eUndefined;
            vk::ImageLayout layout = vk::ImageLayout::eUndefined;
            bool exclusiveEntry = false;
            /// Compares the complete entry and exit contract before adjacent byte ranges are merged.
            bool operator==(const AccessSummary &) const;
        };
        /// Tracks one disjoint buffer interval and its independent entry/exit contract.
        struct BufferSummary
        {
            uint64_t offset = 0, size = 0;
            AccessSummary access;
        };
        /// Keeps one independent summary per image aspect, mip and array layer.
        struct TextureSummary
        {
            eastl::vector<vk::ImageAspectFlagBits> planeAspects;
            uint32_t subresourceCountPerPlane = 0;
            eastl::vector<AccessSummary> subresources;
        };
        /// Finds or creates the encoder's complete partition of a buffer's byte range.
        eastl::vector<BufferSummary> &bufferSummary(VKBuffer &buffer);
        /// Finds or creates image cells using immutable texture metadata only.
        TextureSummary &textureSummary(VKTexture &texture);
        /// Captures every access before the first local producer and resolves dependencies inside the encoder.
        static bool advance(AccessSummary &summary, VKAccessScope next, bool layoutChange, vk::MemoryBarrier2 &barrier);
        /// Establishes the entry layout without encoding an unknown incoming transition in the body.
        static bool advanceImage(AccessSummary &summary, vk::ImageLayout layout, VKAccessScope next, vk::MemoryBarrier2 &barrier);
        const VKQueue *mQueue = nullptr;
        TaskDependencyResolverDiagnostics mDiagnostics;
        eastl::unordered_map<VKBuffer *, eastl::vector<BufferSummary>> mBuffers;
        eastl::unordered_map<VKTexture *, TextureSummary> mTextures;
    };
} // namespace GVM::RHI::Vulkan
