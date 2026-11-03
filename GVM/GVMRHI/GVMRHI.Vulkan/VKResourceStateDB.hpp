#pragma once
#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include "VKQueueFrontier.hpp"
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
        VKQueueFrontier frontier;
    };

    /// Tracks the actual layout and access history of one image subresource.
    struct VKImageSubresourceState
    {
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
        VKResourceAccessState access;
        VKQueueFrontier frontier;
    };

    /// Stores independent aspect/mip/layer cells; updates never replace unrelated layers.
    struct VKResourceTextureState
    {
        eastl::vector<vk::ImageAspectFlagBits> planeAspects;
        uint32_t subresourceCountPerPlane = 0;
        eastl::vector<VKImageSubresourceState> subresources;
    };

    /// Describes one source-family release and the exact allocations its native barriers must keep alive.
    struct VKOwnershipRelease
    {
        eastl::vector<vk::BufferMemoryBarrier2> buffers;
        eastl::vector<vk::ImageMemoryBarrier2> images;
        eastl::vector<Buffer> bufferOwners;
        eastl::vector<Texture> imageOwners;
    };

    /// Owns device resource history at the tail of scheduled work, independent of GPU retirement.
    /// Native queues share one device submission lock; workers never mutate the accepted history.
    class VKResourceStateDB final
    {
    public:
        /// Returns mutable byte-range state for the buffer.
        eastl::vector<VKResourceBufferState> &editBuffer(VKBuffer &buffer);
        /// Returns mutable image state for the texture.
        VKResourceTextureState &editTexture(const VKTexture &texture);
        /// Returns an aggregate snapshot for existing backend diagnostics.
        bool tryGetCurrentBufferState(const VKBuffer &buffer, VKResourceBufferState &outState) const;
        /// Finds the last accepted writer on one queue that overlaps a mapped host read.
        uint64_t lastBufferWriter(const VKBuffer &buffer, uint64_t offset, uint64_t size, uint32_t lane) const;
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
        /// Clears resource history when the owning device is initialized or destroyed.
        void reset();
        /// Starts an empty submission overlay; the caller holds the device submission lock until commit or destruction.
        void beginSubmission(VKResourceStateDB &history, VKQueue &queue, uint64_t value);
        /// Advances a tentative queue frontier and reports whether exclusive ownership changes family.
        bool advanceFrontier(VKQueueFrontier &frontier, bool exclusive);
        /// Pairs a consuming buffer acquire with the matching source-family release.
        void transferOwnership(vk::BufferMemoryBarrier2 &acquire, const VKBuffer &buffer);
        /// Pairs a consuming image acquire with the identical release range and layout transition.
        void transferOwnership(vk::ImageMemoryBarrier2 &acquire, const VKTexture &texture);
        /// Submits any release commands and merges required foreign timeline values before consumer acceptance.
        void prepareQueueWaits(const eastl::vector<CommandEncoder> &users, eastl::vector<vk::SemaphoreSubmitInfo> &waits);
        /// Reports an accepted release whose acquire was not accepted, for device-loss diagnostics.
        bool hasUnacquiredRelease() const;
        /// Publishes preallocated touched entries after native submission is accepted; never allocates or throws.
        void commitSubmission() noexcept;

    private:
        /// Creates the initial undefined cells for a newly encountered image.
        static VKResourceTextureState makeTextureState(const VKTexture &texture);
        mutable Mutex mMutex;
        eastl::unordered_map<const VKBuffer *, eastl::vector<VKResourceBufferState>> mBuffers;
        eastl::unordered_map<const VKTexture *, VKResourceTextureState> mTextures;
        VKQueue *mQueue = nullptr;
        VKQueue *mOther = nullptr;
        uint32_t mLane = 0;
        uint64_t mValue = 0;
        uint64_t mForeignWait = 0;
        uint64_t mAcceptedRelease = 0;
        eastl::vector<VKQuerySet *> mQueries;
        VKOwnershipRelease mRelease;
        VKResourceStateDB *mHistory = nullptr;
    };
} // namespace GVM::RHI::Vulkan
