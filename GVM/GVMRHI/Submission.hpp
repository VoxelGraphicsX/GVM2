#pragma once

#include <EASTL/shared_ptr.h>
#include <cstdint>
#include <limits>

namespace GVM::RHI
{
    namespace Private
    {
        class SubmissionState;
    }
    class QueueImpl;

    /// Retains completion of one accepted GPU submission without exposing queues or native synchronization.
    /// Copies share completion state, which remains queryable after queue and device teardown.
    class Submission final
    {
    public:
        /// Creates an unbound handle; querying it or using it as a dependency is an error.
        Submission();
        /// Releases this handle without waiting for GPU completion.
        ~Submission();
        /// Reports successful completion without waiting; GPU failures throw DeviceLostError.
        bool isComplete() const;
        /// Waits at a host boundary; returns false on timeout and throws on GPU failure.
        bool wait(uint64_t timeoutNanoseconds = std::numeric_limits<uint64_t>::max()) const;

    private:
        friend class QueueImpl;
        /// Binds a queue-created completion state after successful native submission.
        explicit Submission(eastl::shared_ptr<Private::SubmissionState> state);
        eastl::shared_ptr<Private::SubmissionState> mState;
    };
}
