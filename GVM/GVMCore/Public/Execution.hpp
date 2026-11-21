#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/type_traits.h>
#include <EASTL/any.h>
#include <cstdint>
#include <exception>
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

        /// Guards locked access to a completed execution's result and commits GPU ownership transfer only on non-throwing return.
        class ExecutionResultClaim final
        {
        public:
            /// Locks the execution state and validates terminal completion before accessing the stored result.
            explicit ExecutionResultClaim(const eastl::shared_ptr<TaskExecutionState> &state);
            /// Commits single-transfer GPU ownership only when no new exception is active during scope exit.
            ~ExecutionResultClaim();
            ExecutionResultClaim(const ExecutionResultClaim &) = delete;
            ExecutionResultClaim &operator=(const ExecutionResultClaim &) = delete;

            /// Returns the stored result value while the execution lock remains held.
            eastl::any &value() const noexcept
            {
                return *mValue;
            }
            /// Reports whether retrieving this result transfers single-claim GPU resource ownership.
            bool singleTransfer() const noexcept
            {
                return mSingleTransfer;
            }
            /// Marks that a move-only result is about to be moved out of the execution slot.
            void noteMoveAttempt() noexcept
            {
                mMovedFrom = true;
            }

        private:
            const TaskExecutionState *mState = nullptr;
            eastl::any *mValue = nullptr;
            int mUncaughtOnEntry = 0;
            bool mSingleTransfer = false;
            bool mMovedFrom = false;
        };
    }

    /// Retains one execution; successful get returns owned CPU data or GPU allocations requiring explicit destruction.
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
        /// Returns a successful value; CPU results remain repeatable, while exported GPU allocations transfer ownership once and require explicit device destruction.
        /// Unretrieved GPU outputs remain owned by this execution and are reclaimed on destruction.
        T get() const
        {
            Private::ExecutionResultClaim claim(mState);
            if constexpr (!eastl::is_void_v<T>)
            {
                if (claim.singleTransfer())
                {
                    if constexpr (eastl::is_nothrow_move_constructible_v<T>)
                    {
                        return eastl::any_cast<T>(eastl::move(claim.value()));
                    }
                    else if constexpr (eastl::is_copy_constructible_v<T>)
                    {
                        return T(eastl::any_cast<const T &>(claim.value()));
                    }
                    else
                    {
                        claim.noteMoveAttempt();
                        return eastl::any_cast<T>(eastl::move(claim.value()));
                    }
                }
                return T(eastl::any_cast<const T &>(claim.value()));
            }
        }
        /// Explicitly claims the execution result; recommended when retrieving exported GPU allocations with single-transfer ownership.
        T take() const
        {
            return get();
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
