#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/atomic.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    /** One ready-to-destroy batch drained from VKRetireManager by VKDevice::collectReleasedResources. */
    struct VKRetireReadyBatch
    {
        eastl::vector<Sampler> samplers;
        eastl::vector<Texture> textures;
        eastl::vector<Buffer> buffers;
    };

    /** Lock-protected ready queue of user-released resources whose last holder reference was dropped; VKDevice drains it during resource collection. */
    class VKRetireManager final
    {
    public:
        /** Drops every queued handle without destroying the resources; used during device shutdown and test resets. */
        void reset();
        /** Reports whether no handles are waiting, letting collection return early without taking the lock. */
        [[nodiscard]] bool isEmpty() const;
        /** Queues a buffer handle whose last holder reference was released for destruction at the next collection. */
        void enqueue(Buffer buffer);
        /** Queues a texture handle whose last holder reference was released for destruction at the next collection. */
        void enqueue(Texture texture);
        /** Queues a sampler handle whose last holder reference was released for destruction at the next collection. */
        void enqueue(Sampler sampler);
        /** Drains every enqueued handle into one batch for destruction outside the lock. */
        [[nodiscard]] VKRetireReadyBatch takeReady();

    private:
        Mutex mMutex;
        eastl::vector<Buffer> mBuffers;
        eastl::vector<Texture> mTextures;
        eastl::vector<Sampler> mSamplers;
        eastl::atomic<uint32_t> mReadyCount{0u};
    };
} // namespace GVM::RHI::Vulkan
