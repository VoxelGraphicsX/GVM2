#pragma once

#include "../Public/TaskRuntime.hpp"
#include <condition_variable>
#include <mutex>

namespace GVM::Core::Private
{
    /// Coordinates admission and dispatch lifetimes without owning an application's executor.
    class TaskRuntimeState final
    {
    public:
        /// Validates limits and freezes executor capacity before accepting any graph.
        TaskRuntimeState(ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor);
        /// Reserves one execution slot unless admission is closed or full.
        bool tryAdmit();
        /// Releases exactly one admitted execution slot and wakes a draining owner.
        void complete() noexcept;
        /// Protects an executor call against concurrent runtime destruction, without holding the admission lock.
        ScheduleResult schedule(ExecutorTask task) noexcept;
        /// Stops admitting graphs and waits for this runtime's executions and dispatch calls.
        void closeAndWait() noexcept;
        /// Returns the immutable operation-count limit for one execution.
        uint32_t maximumTasks() const noexcept;
        /// Returns the immutable maximum number of accepted jobs for one execution.
        uint32_t maximumInFlight() const noexcept;

    private:
        ITaskExecutor &mExecutor;
        TaskRuntimeDescriptor mDescriptor;
        uint32_t mMaximumInFlight;
        std::mutex mMutex;
        std::condition_variable mDrained;
        uint32_t mActive = 0;
        uint32_t mDispatching = 0;
        bool mClosing = false;
    };
}
