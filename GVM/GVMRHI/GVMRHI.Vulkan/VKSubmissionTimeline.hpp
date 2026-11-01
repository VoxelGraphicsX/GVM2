#pragma once
#include "VKCommon.hpp"
#include <GVMRHI/Private/DeviceLostState.hpp>
#include <EASTL/shared_ptr.h>
#include <mutex>
#include <shared_mutex>

namespace GVM::RHI::Vulkan
{
    class VKDevice;

    /// Owns a queue timeline and preserves its terminal state after the native device is destroyed.
    class VKSubmissionTimeline final
    {
    public:
        /// Creates a timeline for one live device; its owner must close it before destroying that device.
        explicit VKSubmissionTimeline(VKDevice &device);
        /// Releases a previously closed timeline and retained failure state.
        ~VKSubmissionTimeline();
        /// Returns the native semaphore while queue submission lifetime is externally protected.
        vk::Semaphore nativeSemaphore() const;
        /// Identifies the retained device domain without relying on a reusable native handle value.
        const eastl::shared_ptr<DeviceLostState> &deviceState() const;
        /// Queries or waits for a submitted value; handles remain queryable after close.
        bool wait(uint64_t value, uint64_t timeoutNanoseconds) const;
        /// Snapshots completion and releases the semaphore after queue idle or device loss.
        void close();

    private:
        mutable std::shared_mutex mMutex;
        vk::Device mDevice;
        vk::UniqueSemaphore mSemaphore;
        eastl::shared_ptr<DeviceLostState> mLostState;
        uint64_t mCompletedAtClose = 0;
    };
}
