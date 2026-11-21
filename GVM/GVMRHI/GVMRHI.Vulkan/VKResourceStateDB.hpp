#pragma once
#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include <EASTL/array.h>
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

    /// Tracks readers and the latest write/transition, with read visibility reusable only inside one encoder.
    struct VKResourceAccessState
    {
        VKAccessScope current;
        VKAccessScope producer;
        /// Caches encoder-local read barriers; shared submission history must leave this scope empty.
        VKAccessScope visibleReads;
        bool initialized = false;
        bool hasProducer = false;
        bool operator==(const VKResourceAccessState &) const = default;
    };

    /// Stores one resource range's accepted write/transition, independent reader timelines, and queue-family ownership.
    /// ResourceDB updates this data only while preparing a submission; recording workers do not mutate it.
    struct VKResourceQueueState
    {
        uint32_t lastExclusiveQueueId = UINT32_MAX;
        uint64_t lastExclusiveValue = 0;
        eastl::array<uint64_t, MaxDeviceQueues> lastReadValues{};
        uint32_t ownerFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t releaseQueueId = UINT32_MAX;
    };

    /// Compares all queue progress and ownership before adjacent resource ranges may share one state entry.
    bool operator==(const VKResourceQueueState &left, const VKResourceQueueState &right);

    /// Tracks one non-overlapping byte interval in the queue's buffer state.
    struct VKResourceBufferState
    {
        VKBuffer *buffer = nullptr;
        uint64_t offset = 0, size = 0;
        VKResourceAccessState access;
        VKResourceQueueState queueState;
    };

    /// Tracks the actual layout and access history of one image subresource.
    struct VKImageSubresourceState
    {
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
        VKResourceAccessState access;
        VKResourceQueueState queueState;
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
        eastl::array<uint64_t, MaxDeviceQueues> waits{};
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
        /// Starts an empty submission overlay with a valid device queue index, family and nonzero timeline value.
        /// The caller holds the device submission lock until commit or destruction.
        void beginSubmission(VKResourceStateDB &history, uint32_t queueIndex, uint32_t queueFamily, uint64_t value);
        /// Collects required foreign waits and updates one range's pending use; returns whether ownership changes family.
        /// Requires an active submission; writes, image layout changes, and EXCLUSIVE family changes are exclusive uses.
        bool prepareQueueAccess(VKResourceQueueState &state, bool exclusive, bool concurrent = false);
        /// Returns required timeline values indexed by device queue; values from different queues are independent.
        const eastl::array<uint64_t, MaxDeviceQueues> &queueWaitValues() const;
        /// Pairs a consuming buffer acquire with the matching source-family release.
        void transferOwnership(vk::BufferMemoryBarrier2 &acquire, const VKBuffer &buffer, const VKResourceQueueState &previous);
        /// Pairs a consuming image acquire with the identical release range and layout transition.
        void transferOwnership(vk::ImageMemoryBarrier2 &acquire, const VKTexture &texture, const VKResourceQueueState &previous);
        /// Submits any release commands and merges required foreign timeline values before consumer acceptance.
        void prepareQueueWaits(VKDevice &device, const eastl::vector<CommandEncoder> &users, eastl::vector<vk::SemaphoreSubmitInfo> &waits);
        /// Reports an accepted release whose acquire was not accepted, for device-loss diagnostics.
        bool hasUnacquiredRelease() const;
        /// Publishes preallocated touched entries after native submission is accepted; never allocates or throws.
        void commitSubmission() noexcept;

    private:
        /// Creates the initial undefined cells for a newly encountered image.
        static VKResourceTextureState makeTextureState(const VKTexture &texture);
        /// Accumulates all old-family accesses before the selected source queue releases ownership.
        VKOwnershipRelease &prepareOwnershipRelease(const VKResourceQueueState &previous);
        mutable Mutex mMutex;
        eastl::unordered_map<const VKBuffer *, eastl::vector<VKResourceBufferState>> mBuffers;
        eastl::unordered_map<const VKTexture *, VKResourceTextureState> mTextures;
        uint32_t mQueueIndex = UINT32_MAX;
        uint32_t mQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint64_t mValue = 0;
        eastl::array<uint64_t, MaxDeviceQueues> mForeignWaits{};
        eastl::array<uint64_t, MaxDeviceQueues> mAcceptedReleases{};
        eastl::vector<VKQuerySet *> mQueries;
        eastl::array<VKOwnershipRelease, MaxDeviceQueues> mReleases;
        VKResourceStateDB *mHistory = nullptr;
    };
} // namespace GVM::RHI::Vulkan
