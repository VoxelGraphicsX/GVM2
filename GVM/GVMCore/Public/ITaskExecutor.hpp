#pragma once

#include "ExecutorTask.hpp"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace GVM::Core
{
    /// Reports whether an owning job was accepted; rejected jobs must never execute.
    enum class ScheduleResult
    {
        Accepted,
        Stopping,
        Unavailable
    };

    /// Executes ready jobs without knowing graph dependencies, GPU queues, or renderer state.
    class ITaskExecutor
    {
    public:
        using WaitPredicate = bool (*)(const void *) noexcept;

        /// Destroys an executor only after all its accepted jobs have drained.
        virtual ~ITaskExecutor();
        /// Accepts one job exactly once, without executing it on the calling stack.
        virtual ScheduleResult trySchedule(ExecutorTask task) noexcept = 0;
        /// Accepts a contiguous batch of ready jobs; returns the number of leading tasks accepted.
        virtual size_t scheduleBatch(ExecutorTask *tasks, size_t count) noexcept;
        /// Reports configured worker capacity; a usable executor has at least one worker.
        virtual uint32_t concurrency() const noexcept = 0;
        /// Waits for predicate(context) under lock; fiber-backed executors may cooperatively yield the active fiber.
        virtual bool waitCondition(
            std::unique_lock<std::mutex> &lock,
            std::condition_variable &condition,
            uint64_t timeoutNanoseconds,
            WaitPredicate predicate,
            const void *context) const noexcept;
        /// Wakes all waiters blocked in waitCondition on condition (both OS threads and cooperative fibers).
        virtual void notifyCondition(std::condition_variable &condition) const noexcept;
    };
}
