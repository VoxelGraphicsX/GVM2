#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/type_traits.h>
#include <EASTL/any.h>
#include <cstdint>
#include <limits>

namespace GVM::Core
{
    /// Describes execution lifecycle; cancellation becomes terminal only after accepted jobs drain.
    enum class ExecutionStatus
    {
        Pending,
        Running,
        Succeeded,
        Failed,
        Cancelled,
        Rejected
    };

    namespace Private
    {
        class TaskExecutionState;
        /// Reads the lifecycle of a retained execution without waiting.
        ExecutionStatus executionStatus(const eastl::shared_ptr<TaskExecutionState> &state);
        /// Reads a bounded diagnostic after failure or rejection, returning empty text otherwise.
        eastl::string executionError(const eastl::shared_ptr<TaskExecutionState> &state);
        /// Waits only at a host boundary; returns false when the timeout expires.
        bool waitExecution(const eastl::shared_ptr<TaskExecutionState> &state, uint64_t timeoutNanoseconds);
        /// Requests cooperative cancellation of work that has not started.
        void cancelExecution(const eastl::shared_ptr<TaskExecutionState> &state);
        /// Copies a successful result without waiting, rethrowing an original task failure.
        eastl::any executionResult(const eastl::shared_ptr<TaskExecutionState> &state);
    }

    /// Retains one asynchronous execution and returns owned results only after success.
    template <class T>
    class Execution final
    {
    public:
        /// Returns lifecycle state without waiting for tasks or GPU work.
        ExecutionStatus status() const
        {
            return Private::executionStatus(mState);
        }
        /// Returns true for every terminal state, including rejection, cancellation and failure.
        bool ready() const
        {
            const auto current = status();
            return current != ExecutionStatus::Pending && current != ExecutionStatus::Running;
        }
        /// Returns a bounded error message without waiting; ready() does not imply success.
        eastl::string error() const
        {
            return Private::executionError(mState);
        }
        /// Waits at a host boundary; graph workers must express continuations instead of waiting.
        bool wait(uint64_t timeoutNanoseconds = std::numeric_limits<uint64_t>::max()) const
        {
            return Private::waitExecution(mState, timeoutNanoseconds);
        }
        /// Cancels unstarted work while retaining accepted work until its terminal accounting.
        void cancel() const
        {
            Private::cancelExecution(mState);
        }
        /// Returns an owned successful value; throws if not ready, cancelled, rejected or failed.
        T get() const
        {
            auto result = Private::executionResult(mState);
            if constexpr (!eastl::is_void_v<T>)
            {
                return eastl::any_cast<T>(eastl::move(result));
            }
        }

    private:
        friend class TaskRuntime;
        /// Binds the typed host handle to runtime-owned execution state.
        explicit Execution(eastl::shared_ptr<Private::TaskExecutionState> state)
            : mState(eastl::move(state))
        {
        }
        eastl::shared_ptr<Private::TaskExecutionState> mState;
    };
}
