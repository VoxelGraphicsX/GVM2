#pragma once
#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    /// Describes an execution scope and the memory accesses within it.
    struct VKAccessScope
    {
        vk::PipelineStageFlags2 stages = {};
        vk::AccessFlags2 accesses = {};
        bool operator==(const VKAccessScope &) const = default;
    };

    /// Tracks readers, the latest write/transition, and established read visibility.
    struct VKResourceAccessState
    {
        VKAccessScope current;
        VKAccessScope producer;
        VKAccessScope visibleReads;
        bool initialized = false;
        bool hasProducer = false;
        bool operator==(const VKResourceAccessState &) const = default;
    };

    /// Tracks one non-overlapping byte interval in the queue's buffer state.
    struct VKResourceBufferState
    {
        VKBuffer *buffer = nullptr;
        uint64_t offset = 0, size = 0;
        VKResourceAccessState access;
    };

    /// Tracks the actual layout and access history of one image subresource.
    struct VKImageSubresourceState
    {
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
        VKResourceAccessState access;
    };

    /// Stores independent aspect/mip/layer cells; updates never replace unrelated layers.
    struct VKResourceTextureState
    {
        eastl::vector<vk::ImageAspectFlagBits> planeAspects;
        uint32_t subresourceCountPerPlane = 0;
        eastl::vector<VKImageSubresourceState> subresources;
    };

    /// Owns the state at the tail of the scheduled queue, independent of GPU retirement.
    /// Mutable references are used only while the queue's submission lock is held.
    class VKResourceStateDB final
    {
    public:
        /// Returns mutable byte-range state for the buffer.
        eastl::vector<VKResourceBufferState> &editBuffer(VKBuffer &buffer);
        /// Returns mutable image state for the texture.
        VKResourceTextureState &editTexture(VKTexture &texture);
        /// Returns an aggregate snapshot for existing backend diagnostics.
        bool tryGetCurrentBufferState(const VKBuffer &buffer, VKResourceBufferState &outState) const;
        /// Queries a uniform image range for native presentation operations.
        bool tryGetSharedTextureStateForTransition(const VKTexture &texture, TextureAspectFlags aspect, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount, vk::ImageLayout &layout, vk::PipelineStageFlags2 &stages, vk::AccessFlags2 &accesses) const;
        /// Records host-side access to a byte range outside a command encoder.
        void recordExternalBufferUsage(VKBuffer &buffer, uint64_t offset, uint64_t size, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses);
        /// Records host-side access to the complete buffer.
        void recordExternalBufferUsage(VKBuffer &buffer, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses);
        /// Publishes a native WSI transition in queue order.
        void recordExternalTextureState(VKTexture &texture, TextureAspectFlags aspect, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount, vk::ImageLayout layout, vk::PipelineStageFlags2 stages, vk::AccessFlags2 accesses);
        /// Keeps the legacy diagnostic query without imposing any idle-layout transitions.
        bool isTextureInSteadyStateLayout(const VKTexture &texture) const;
        /// Removes a physically retired buffer from state tracking.
        void discardBufferState(const VKBuffer &buffer);
        /// Removes a physically retired texture from state tracking.
        void discardTextureState(const VKTexture &texture);
        /// Clears queue state when the device or queue is destroyed.
        void reset();

    private:
        /// Creates the initial undefined cells for a newly encountered image.
        static VKResourceTextureState makeTextureState(const VKTexture &texture);
        mutable Mutex mMutex;
        eastl::unordered_map<const VKBuffer *, eastl::vector<VKResourceBufferState>> mBuffers;
        eastl::unordered_map<const VKTexture *, VKResourceTextureState> mTextures;
    };
} // namespace GVM::RHI::Vulkan
