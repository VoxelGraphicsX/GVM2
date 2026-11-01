#pragma once

#include "Execution.hpp"
#include "TaskGraph.hpp"
#include "ThreadExecutor.hpp"
#include <EASTL/unique_ptr.h>

namespace GVM::Core
{
    namespace Private
    {
        class TaskRuntimeState;
    }

    /// Bounds accepted CPU graph executions and their ready work without configuring GPU queues.
    struct TaskRuntimeDescriptor
    {
        uint32_t maximumActiveExecutions = 32;
        uint32_t maximumTasksPerExecution = 65536;
        uint32_t maximumInFlightTasksPerExecution = 0;
        uint32_t workerCount = 0;
    };

    /// Drives graphs independently of a Renderer; destruction drains only this runtime's work.
    /// Native builders, callables and values must own their captures and contain no GPU resources.
    class TaskRuntime final
    {
    public:
        /// Creates a CPU runtime with one default thread pool and explicit admission limits.
        explicit TaskRuntime(const TaskRuntimeDescriptor &descriptor = {});
        /// Borrows an executor that must outlive this runtime and all of its accepted callbacks.
        explicit TaskRuntime(ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor = {});
        /// Closes admission and drains accepted work; call only from a host thread.
        ~TaskRuntime();
        TaskRuntime(const TaskRuntime &) = delete;
        TaskRuntime &operator=(const TaskRuntime &) = delete;

        /// Owns the builder and input snapshot, then builds and evaluates a graph asynchronously.
        /// The builder returns Task<T>; neither submission nor graph joins wait for worker children.
        template <class Builder, class Input>
        auto submit(Builder builder, Input input)
        {
            using Output = eastl::invoke_result_t<Builder &, Input>;
            using Value = typename Output::Value;
            static_assert(eastl::is_same_v<Output, Task<Value>>, "A graph entry must return a Task value.");
            static_assert(!eastl::is_pointer_v<Input>, "Graph entry inputs must own their values.");
            auto prepare = [builder = eastl::move(builder), input = eastl::move(input)]() mutable
            {
                auto output = eastl::invoke(builder, eastl::move(input));
                return Private::TaskGraphOutput{eastl::move(output.mDefinition), output.mIndex};
            };
            return Execution<Value>(submitEntry(eastl::move(prepare)));
        }

    private:
        /// Admits an owning entry or returns a terminal rejected execution without invoking it.
        eastl::shared_ptr<Private::TaskExecutionState> submitEntry(eastl::function<Private::TaskGraphOutput()> prepare);

        eastl::unique_ptr<ThreadExecutor> mOwnedExecutor;
        eastl::shared_ptr<Private::TaskRuntimeState> mState;
    };
}
