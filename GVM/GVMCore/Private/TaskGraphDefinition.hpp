#pragma once

#include <EASTL/functional.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/any.h>
#include <cstdint>
#include <mutex>

namespace GVM::Core::Private
{
    using TaskResults = eastl::vector<eastl::any>;

    /// Describes one owning operation and its earlier graph dependencies, independent of workers.
    struct TaskDefinition
    {
        eastl::vector<uint32_t> predecessors;
        eastl::function<eastl::any(const TaskResults &)> evaluate;
    };

    /// Owns a graph definition; execution snapshots isolate callable state between submissions.
    class TaskGraphDefinition final
    {
    public:
        /// Appends an operation whose predecessors must already belong to this definition.
        uint32_t append(TaskDefinition definition);
        /// Copies the current operations into an isolated execution plan within the supplied limit.
        eastl::vector<TaskDefinition> snapshot(uint32_t maximumTasks) const;

    private:
        mutable std::mutex mMutex;
        eastl::vector<TaskDefinition> mTasks;
    };

    /// Identifies the definition and output of a graph entry without borrowing a builder's stack.
    struct TaskGraphOutput
    {
        eastl::shared_ptr<TaskGraphDefinition> definition;
        uint32_t index = 0;
    };
}
