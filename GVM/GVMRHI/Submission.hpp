#pragma once

#include <EASTL/functional.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/span.h>
#include <cstdint>
#include <limits>

namespace GVM::RHI
{
    class DeviceImpl;
    class QueueImpl;
    class Submission;
    class HostTimeline;

    namespace Private
    {
        class SubmissionState;
        class HostTimelineState;
        const eastl::shared_ptr<SubmissionState> &submissionStateOf(const Submission &submission);
        const eastl::shared_ptr<HostTimelineState> &hostTimelineStateOf(const HostTimeline &timeline);
    }

    /// Host-driven 64-bit monotonic timeline usable with RHI::waitAny across all platforms without OS-specific APIs.
    class HostTimeline final
    {
    public:
        HostTimeline();
        ~HostTimeline();
        [[nodiscard]]
        bool isValid() const noexcept;
        /// Increments the monotonic timeline counter and wakes any active waitAny waiting on <= the returned value.
        uint64_t signal() const noexcept;
        /// Returns the latest signaled monotonic value.
        [[nodiscard]]
        uint64_t currentValue() const noexcept;

    private:
        friend class DeviceImpl;
        friend const eastl::shared_ptr<Private::HostTimelineState> &Private::hostTimelineStateOf(const HostTimeline &timeline);
        explicit HostTimeline(eastl::shared_ptr<Private::HostTimelineState> state);
        eastl::shared_ptr<Private::HostTimelineState> mState;
    };

    /// Retains completion of one accepted GPU submission without exposing queues or native synchronization.
    /// Copies share completion state, which remains queryable after queue and device teardown.
    class Submission final
    {
    public:
        /// Creates an unbound handle; querying it or using it as a dependency is an error.
        Submission();
        /// Releases this handle without waiting for GPU completion.
        ~Submission();
        /// Reports whether this handle is bound to accepted GPU work.
        [[nodiscard]]
        bool isValid() const noexcept;
        /// Compares whether two handles refer to the same underlying submission state.
        bool operator==(const Submission &) const noexcept = default;
        /// Returns the monotonic per-queue serial associated with this submission.
        [[nodiscard]]
        uint64_t timelineValue() const;
        /// Reports successful completion without waiting; GPU failures throw DeviceLostError.
        bool isComplete() const;
        /// Waits at a host boundary; returns false on timeout and throws on GPU failure.
        bool wait(uint64_t timeoutNanoseconds = std::numeric_limits<uint64_t>::max()) const;
        /// Reports whether the backend pushes completion callbacks natively without host wait threads.
        [[nodiscard]]
        bool hasNativeCompletionCallback() const noexcept;
        /// Registers a native completion callback when supported by the backend; returns false otherwise.
        bool tryAddNativeCompletionCallback(eastl::function<void()> callback) const;

    private:
        friend class QueueImpl;
        friend const eastl::shared_ptr<Private::SubmissionState> &Private::submissionStateOf(const Submission &submission);
        /// Binds a queue-created completion state after successful native submission.
        explicit Submission(eastl::shared_ptr<Private::SubmissionState> state);
        eastl::shared_ptr<Private::SubmissionState> mState;
    };

    /// Waits until at least one submission in submissions completes, hostTimeline reaches hostTargetValue, or timeout expires.
    bool waitAny(
        eastl::span<const Submission> submissions,
        const HostTimeline *hostTimeline = nullptr,
        uint64_t hostTargetValue = 0,
        uint64_t timeoutNanoseconds = std::numeric_limits<uint64_t>::max());
}
