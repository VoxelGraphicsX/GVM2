#pragma once

#include "TaskGraphDefinition.hpp"
#include "../Public/Execution.hpp"
#include "../Public/ITaskExecutor.hpp"
#include <condition_variable>
#include <exception>
#include <mutex>

namespace GVM::Core::Private
{
    class TaskRuntimeState;

    /// Owns one execution's immutable plan, result slots and continuation counters.
    class TaskExecutionState final : public eastl::enable_shared_from_this<TaskExecutionState>
    {
    public:
        /// Creates pending state without publishing work to an executor.
        explicit TaskExecutionState(eastl::shared_ptr<TaskRuntimeState> runtime);
        /// Publishes the preparation job after runtime admission; rejection is terminal.
        void start(eastl::function<TaskGraphOutput()> prepare);
        /// Marks a non-admitted execution rejected without calling its builder.
        void reject();
        /// Reads lifecycle state under the execution lock.
        ExecutionStatus status() const;
        /// Returns a bounded original failure message or the terminal rejection reason.
        eastl::string error() const;
        /// Waits for terminal accounting at a host boundary, with an optional finite timeout.
        bool wait(uint64_t timeoutNanoseconds) const;
        /// Requests cancellation without interrupting a callback that has already started.
        void cancel();
        /// Copies the final owned result only after success; failures rethrow their original exception.
        eastl::any result() const;

    private:
        /// Builds an isolated plan, allocates stable slots, and makes dependency-free jobs ready.
        void prepare(eastl::function<TaskGraphOutput()> builder) noexcept;
        /// Publishes ready jobs up to the admission budget without waiting on child work.
        void dispatchReady() noexcept;
        /// Executes an accepted node outside locks, capturing all user exceptions.
        void evaluate(uint32_t index) noexcept;
        /// Completes one accepted job and publishes its successors exactly once.
        void finish(uint32_t index, eastl::any value, std::exception_ptr failure) noexcept;
        /// Records a scheduling failure for a job that the executor did not accept.
        void scheduleFailed(uint32_t index, ScheduleResult result) noexcept;
        /// Selects a terminal outcome only when every accepted job has stopped using its inputs.
        bool finishLocked() noexcept;
        /// Releases inputs and admission before publishing the terminal outcome to host waiters.
        void notifyTerminal() noexcept;
        /// Tests lifecycle state with mMutex held.
        bool terminalLocked() const noexcept;

        static constexpr uint32_t Preparation = UINT32_MAX;
        eastl::shared_ptr<TaskRuntimeState> mRuntime;
        mutable std::mutex mMutex;
        mutable std::condition_variable mCompleted;
        ExecutionStatus mStatus = ExecutionStatus::Pending;
        ExecutionStatus mStopStatus = ExecutionStatus::Pending;
        std::exception_ptr mFailure;
        const char *mDiagnostic = "";
        eastl::vector<TaskDefinition> mTasks;
        eastl::vector<eastl::vector<uint32_t>> mSuccessors;
        eastl::vector<uint32_t> mPending;
        eastl::vector<uint32_t> mReady;
        TaskResults mResults;
        eastl::any mFinalResult;
        uint32_t mOutput = 0;
        uint32_t mInFlight = 0;
        uint32_t mRemaining = 0;
        size_t mReadyCursor = 0;
        bool mPrepared = false;
        bool mDispatching = false;
        bool mTerminalPublished = false;
    };
}
