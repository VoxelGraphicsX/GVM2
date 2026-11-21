#pragma once
#include <GVMRHI/Private/SubmissionState.hpp>
#include <GVMRHI/Private/DeviceLostState.hpp>
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>
#include <Metal/Metal.hpp>
#include <condition_variable>
#include <mutex>

namespace GVM::RHI::Metal
{
    struct MSubmissionDependency
    {
        MTL::Event *event = nullptr;
        uint64_t value = 0;
    };

    /// Retains native completion and GPU dependency events independently of queue and device wrappers.
    class MSubmissionState final : public Private::SubmissionState, public eastl::enable_shared_from_this<MSubmissionState>
    {
    public:
        /// Captures every command buffer in one batch before any native commit occurs.
        MSubmissionState(eastl::vector<NS::SharedPtr<MTL::CommandBuffer>> buffers,
            NS::SharedPtr<MTL::Event> event, uint64_t eventValue, eastl::shared_ptr<DeviceLostState> device);
        /// Registers weak completion notifications before committing the captured command buffers.
        void arm();
        /// Returns the monotonic event serial signaled by this submission.
        uint64_t timelineValue() const noexcept override;
        /// Queries or waits for all captured native command buffers, including pending transfers.
        bool wait(uint64_t timeoutNanoseconds) const override;
        /// Metal command buffers natively push completion notifications via addCompletedHandler.
        bool hasNativeCompletionCallback() const noexcept override { return true; }
        /// Registers a callback invoked once all captured command buffers finish or fail.
        bool tryAddNativeCompletionCallback(eastl::function<void()> callback) override;
        /// Waits until any submission in the Metal batch completes, hostTimelineState triggers, or timeout expires.
        bool waitAny(
            eastl::span<const Submission> submissions,
            const Private::HostTimelineState *hostTimelineState,
            uint64_t hostTargetValue,
            uint64_t timeoutNanoseconds) const override;
        /// Validates the retained device domain and returns the event and value signaled by this submission.
        MSubmissionDependency dependency(const eastl::shared_ptr<DeviceLostState> &device) const;

    private:
        struct CallbackEntry
        {
            uint64_t token = 0;
            eastl::function<void()> callback;
        };
        uint64_t addWaiterCallback(eastl::function<void()> callback);
        void removeWaiterCallback(uint64_t token) noexcept;
        /// Reports native completion and promotes command-buffer failures to device loss.
        bool checkComplete() const;
        /// Wakes host waiters when Metal reports completion of any captured command buffer.
        void notifyCompletion();
        eastl::vector<NS::SharedPtr<MTL::CommandBuffer>> mBuffers;
        NS::SharedPtr<MTL::Event> mEvent;
        uint64_t mEventValue = 0;
        eastl::shared_ptr<DeviceLostState> mLostState;
        mutable std::mutex mMutex;
        mutable std::condition_variable mCompleted;
        uint64_t mNotifications = 0;
        uint64_t mNextCallbackToken = 1u;
        size_t mRemainingBuffers = 0;
        bool mCallbacksFired = false;
        eastl::vector<CallbackEntry> mCallbacks;
        eastl::shared_ptr<MSubmissionState> mCallbackSelfKeepAlive;
    };
}
