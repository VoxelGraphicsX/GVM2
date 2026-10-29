#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/atomic.h>
#include <cstdint>

namespace GVM::RHI::Vulkan
{
    /** Outcome of releasing the reference a user handle holds on a Vulkan resource. */
    enum class VKUserReleaseResult : uint8_t
    {
        AlreadyReleased,   // free*() was already called; nothing changed
        StillReferenced,   // other holders remain; destruction waits for the last release()
        LastReference,     // no holders remain; the resource may be destroyed at the next collection
    };

    /** Reference count and user-release flag of one Vulkan buffer, texture or sampler.
     *  The user handle holds one reference from creation until free*(); every bind group, Args snapshot,
     *  pending readback, queue upload and command encoder that may still reach the resource on the GPU
     *  holds one more until that work has completed. Thread-safe. */
    class VKResourceLifetime final
    {
    public:
        /** Adds one holder reference. */
        void retain();
        /** Drops one holder reference; returns true when the user handle was already released and this dropped the last reference. Throws std::logic_error naming apiName on underflow. */
        [[nodiscard]] bool release(const char *apiName);
        /** Releases the user handle's reference exactly once. */
        [[nodiscard]] VKUserReleaseResult releaseUser();
        /** Reports whether free*() has been called. */
        [[nodiscard]] bool isUserReleased() const;
        /** Reports whether the resource may be destroyed now: user released and no references remain. */
        [[nodiscard]] bool isDestroyable() const;
        /** Returns the current number of references, including the user handle's while it is held. */
        [[nodiscard]] uint32_t getReferenceCount() const;

    private:
        eastl::atomic<uint32_t> mReferences{1u};
        eastl::atomic<bool> mUserReleased{false};
    };
} // namespace GVM::RHI::Vulkan
