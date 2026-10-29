#include "VKResourceLifetime.hpp"

namespace GVM::RHI::Vulkan
{
    void VKResourceLifetime::retain()
    {
        mReferences.fetch_add(1u, eastl::memory_order_acq_rel);
    }

    bool VKResourceLifetime::release(const char *apiName)
    {
        const uint32_t previous = mReferences.fetch_sub(1u, eastl::memory_order_acq_rel);
        if (previous == 0u)
        {
            mReferences.fetch_add(1u, eastl::memory_order_relaxed);
            throw std::logic_error((eastl::string(apiName) + " released a reference that does not exist.").c_str());
        }
        return previous == 1u && mUserReleased.load(eastl::memory_order_acquire);
    }

    VKUserReleaseResult VKResourceLifetime::releaseUser()
    {
        bool expected = false;
        if (!mUserReleased.compare_exchange_strong(expected, true, eastl::memory_order_acq_rel, eastl::memory_order_acquire))
        {
            return VKUserReleaseResult::AlreadyReleased;
        }
        const uint32_t previous = mReferences.fetch_sub(1u, eastl::memory_order_acq_rel);
        return previous == 1u ? VKUserReleaseResult::LastReference : VKUserReleaseResult::StillReferenced;
    }

    bool VKResourceLifetime::isUserReleased() const
    {
        return mUserReleased.load(eastl::memory_order_acquire);
    }

    bool VKResourceLifetime::isDestroyable() const
    {
        return mUserReleased.load(eastl::memory_order_acquire) &&
            mReferences.load(eastl::memory_order_acquire) == 0u;
    }

    uint32_t VKResourceLifetime::getReferenceCount() const
    {
        return mReferences.load(eastl::memory_order_acquire);
    }
} // namespace GVM::RHI::Vulkan
