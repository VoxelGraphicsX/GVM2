#pragma once

#include "ExecutorTask.hpp"
#include <cstdint>

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
        /// Destroys an executor only after all its accepted jobs have drained.
        virtual ~ITaskExecutor();
        /// Accepts one job exactly once, without executing it on the calling stack.
        virtual ScheduleResult trySchedule(ExecutorTask task) noexcept = 0;
        /// Reports configured worker capacity; a usable executor has at least one worker.
        virtual uint32_t concurrency() const noexcept = 0;
    };
}
