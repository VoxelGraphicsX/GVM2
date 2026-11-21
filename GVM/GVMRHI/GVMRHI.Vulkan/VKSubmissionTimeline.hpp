#pragma once
#include "VKCommon.hpp"
#include <GVMRHI/Private/DeviceLostState.hpp>
#include <GVMRHI/Private/SubmissionState.hpp>
#include <EASTL/shared_ptr.h>
#include <EASTL/span.h>
#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace GVM::RHI::Vulkan
{
    class VKDevice;

    /// Pure Vulkan 1.3 timeline-semaphore host timeline state usable with waitAny on any platform.
    class VKHostTimelineState final : public Private::HostTimelineState
    {
    public:
        VKHostTimelineState(vk::Device device, eastl::shared_ptr<DeviceLostState> lostState);
        ~VKHostTimelineState() override;

        uint64_t signal() noexcept override;
        uint64_t currentValue() const noexcept override;
        bool wait(uint64_t targetValue, uint64_t timeoutNanoseconds) const override;
        /// Signals any active waitAny and releases the native timeline semaphore before vkDestroyDevice.
        void close() noexcept;

    private:
        friend class VKSubmissionTimeline;
        void noteWaitTarget(uint64_t targetValue) const noexcept;
        mutable std::shared_mutex mLifetimeMutex;
        mutable std::mutex mSignalMutex;
        vk::Device mDevice;
        vk::UniqueSemaphore mSemaphore;
        eastl::shared_ptr<DeviceLostState> mLostState;
        std::atomic<uint64_t> mCurrentValue{0};
        mutable std::atomic<uint64_t> mMaxWaitTarget{0};
        std::atomic<bool> mClosed{false};
    };

    /// Owns a queue timeline and preserves its terminal state after the native device is destroyed.
    class VKSubmissionTimeline final
    {
    public:
        struct WaitTarget
        {
            const VKSubmissionTimeline *timeline = nullptr;
            uint64_t value = 0;
        };

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
        /// Waits until any deduplicated sorted timeline target or optional host timeline is satisfied.
        static bool waitAny(
            eastl::span<const WaitTarget> sortedTargets,
            const VKHostTimelineState *hostTimelineState,
            uint64_t hostTargetValue,
            uint64_t timeoutNanoseconds);
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
