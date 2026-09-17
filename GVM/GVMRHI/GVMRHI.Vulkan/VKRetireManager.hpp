#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    // One ready-to-execute retire batch collected from VKRetireManager.
    struct VKRetireReadyBatch
    {
        eastl::vector<VKBuffer *> bindGroupBufferReferenceReleases;
        eastl::vector<VKTexture *> bindGroupTextureReferenceReleases;
        eastl::vector<VKSampler *> bindGroupSamplerReferenceReleases;
        eastl::vector<Sampler> samplers;
        eastl::vector<Texture> textures;
        eastl::vector<Buffer> buffers;
        uint32_t pendingAfterCollect = 0u;
    };

    class VKRetireManager final
    {
    public:
        static constexpr uint64_t CollectionRetryIntervalUs = 1000u;

        void reset();

        [[nodiscard]]
        bool shouldCollect(bool forceRelease, uint64_t currentTimeUs) const;

        [[nodiscard]]
        size_t getPendingDestroyBufferCount() const;

        [[nodiscard]]
        size_t getPendingDestroyTextureCount() const;

        [[nodiscard]]
        size_t getPendingDestroySamplerCount() const;

        [[nodiscard]]
        Texture findPendingTextureHandle(const VKTexture *texture) const;

        void enqueueBuffer(Buffer buffer);
        void enqueueTexture(Texture texture);
        void enqueueSampler(Sampler sampler);
        void enqueueBindGroupReferenceReleases(
            eastl::vector<VKBuffer *> buffers,
            eastl::vector<VKTexture *> textures,
            eastl::vector<VKSampler *> samplers);

        [[nodiscard]]
        VKRetireReadyBatch collectReady(bool forceRelease);
        void finishCollection(bool forceRelease, bool madeProgress, uint64_t currentTimeUs);

    private:
        uint32_t refreshPendingResourceCountLocked();

        mutable Mutex mMutex;
        eastl::vector<Buffer> mPendingDestroyBuffers;
        eastl::vector<Texture> mPendingDestroyTextures;
        eastl::vector<Sampler> mPendingDestroySamplers;
        eastl::vector<VKBuffer *> mPendingBindGroupBufferReferenceReleases;
        eastl::vector<VKTexture *> mPendingBindGroupTextureReferenceReleases;
        eastl::vector<VKSampler *> mPendingBindGroupSamplerReferenceReleases;
        std::atomic<uint32_t> mPendingResourceCount{0u};
        std::atomic<uint64_t> mNextCollectionAttemptUs{0u};
    };
} // namespace GVM::RHI::Vulkan
