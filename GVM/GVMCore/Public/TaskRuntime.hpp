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

    /// Bounds graph executions, ready CPU work and GPU pass batches without exposing queues.
    struct TaskRuntimeDescriptor
    {
        uint32_t maximumActiveExecutions = 32;
        uint32_t maximumTasksPerExecution = 65536;
        uint32_t maximumInFlightTasksPerExecution = 0;
        uint32_t workerCount = 0;
        uint32_t maximumPassesPerExecution = 4096;
        uint32_t maximumPassesPerBatch = 64;
        /// Enables zero-blocking cross-frame GPU timestamp feedback to refine pass scheduling weights.
        bool enablePassTimestampFeedback = false;
        /// GPU pass duration in nanoseconds mapped to one cross-queue handoff cost unit (default 50 us).
        uint32_t queueHandoffThresholdNs = 50000;
    };

    /// Snapshot of one GPU pass's smoothed hardware duration and learned scheduler weight.
    struct PassProfileFeedbackEntry
    {
        eastl::string label;
        uint64_t profileKey = 0;
        double smoothedDurationNs = 0.0;
        uint32_t sampleCount = 0;
        uint16_t learnedWeight = 0;
    };

    /// Drives graphs independently of a Renderer; destruction drains only this runtime's work.
    /// Native CPU callables must own their captures and contain no GPU resources; borrowed GPU handles flow between RenderNodes and remain subject to their existing owner lifetime.
    class TaskRuntime final
    {
    public:
        /// Creates a CPU runtime with one default thread pool and explicit admission limits.
        explicit TaskRuntime(const TaskRuntimeDescriptor &descriptor = {});
        /// Borrows an executor that must outlive this runtime and all of its accepted callbacks.
        explicit TaskRuntime(ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor = {});
        /// Enables GPU subgraphs; the device must outlive accepted work and all unreleased GPU output allocations.
        explicit TaskRuntime(RHI::DeviceImpl &device, const TaskRuntimeDescriptor &descriptor = {});
        /// Shares an application's executor for CPU tasks and GPU pass recording without creating another worker pool.
        TaskRuntime(RHI::DeviceImpl &device, ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor = {});
        /// Closes admission and drains accepted work; call only from a host thread.
        ~TaskRuntime();
        TaskRuntime(const TaskRuntime &) = delete;
        TaskRuntime &operator=(const TaskRuntime &) = delete;

        /// Owns the builder and input snapshot, then builds and evaluates a graph asynchronously.
        /// The builder returns Task<T>; neither submission nor graph joins wait for worker children.
        /// Separate executions have no implicit ordering. Keep dependent GPU uses in one graph, or
        /// wait for the previous execution to succeed before reusing an external writable resource.
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
                return Private::makeTaskGraphOutput(eastl::move(output));
            };
            return Execution<Value>(submitEntry(eastl::move(prepare)));
        }

        /// Returns the latest learned GPU pass durations and scheduling weights when enablePassTimestampFeedback is active.
        eastl::vector<PassProfileFeedbackEntry> passProfileSnapshot() const;

    private:
        /// Admits an owning entry or returns a terminal rejected execution without invoking it.
        eastl::shared_ptr<Private::TaskExecutionState> submitEntry(eastl::function<Private::TaskGraphOutput()> prepare);

        eastl::unique_ptr<ThreadExecutor> mOwnedExecutor;
        eastl::shared_ptr<Private::TaskRuntimeState> mState;
    };
}
