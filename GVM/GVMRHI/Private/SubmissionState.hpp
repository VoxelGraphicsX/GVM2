#pragma once
#include <GVMRHI/Submission.hpp>
#include <EASTL/functional.h>
#include <EASTL/span.h>
#include <cstdint>

namespace GVM::RHI::Private
{
    /// Cross-platform host-driven 64-bit monotonic timeline state paired with multi-submission waits.
    class HostTimelineState
    {
    public:
        virtual ~HostTimelineState();
        /// Advances the monotonic timeline counter and wakes any active waitAny call.
        virtual uint64_t signal() noexcept = 0;
        /// Returns the latest signaled monotonic value.
        virtual uint64_t currentValue() const noexcept = 0;
        /// Waits until currentValue() >= targetValue or timeoutNanoseconds elapses.
        virtual bool wait(uint64_t targetValue, uint64_t timeoutNanoseconds) const = 0;
        /// Attaches an optional host callback fired once currentValue() >= targetValue for callback-based backends.
        virtual uint64_t onTargetValue(uint64_t targetValue, eastl::function<void()> callback) const
        {
            (void)targetValue;
            (void)callback;
            return 0u;
        }
        /// Removes a previously registered target listener if it has not yet fired.
        virtual void removeListener(uint64_t token) const noexcept
        {
            (void)token;
        }
    };

    /// Provides backend completion for an accepted submission, including retained-handle lifetime.
    class SubmissionState
    {
    public:
        /// Releases completion storage after the last public handle and dependent submission retire.
        virtual ~SubmissionState();
        /// Returns the monotonic per-queue serial associated with this submission.
        virtual uint64_t timelineValue() const noexcept = 0;
        /// Queries or waits for successful completion; zero never blocks and failures throw.
        virtual bool wait(uint64_t timeoutNanoseconds) const = 0;
        /// Reports whether the backend pushes completion callbacks natively without host wait threads.
        virtual bool hasNativeCompletionCallback() const noexcept { return false; }
        /// Registers a native completion callback when supported; returns false when unsupported.
        virtual bool tryAddNativeCompletionCallback(eastl::function<void()> callback)
        {
            (void)callback;
            return false;
        }
        /// Waits until any submission in the batch completes, hostTimelineState reaches hostTargetValue, or timeout expires.
        virtual bool waitAny(
            eastl::span<const Submission> submissions,
            const HostTimelineState *hostTimelineState,
            uint64_t hostTargetValue,
            uint64_t timeoutNanoseconds) const = 0;
    };

    inline const eastl::shared_ptr<SubmissionState> &submissionStateOf(const Submission &submission)
    {
        return submission.mState;
    }

    inline const eastl::shared_ptr<HostTimelineState> &hostTimelineStateOf(const HostTimeline &timeline)
    {
        return timeline.mState;
    }
}
