#pragma once

#include "../Public/TaskRuntime.hpp"
#include <condition_variable>
#include <mutex>

namespace GVM::Core::Private
{
    class GraphDevice;
    class GpuGraphExecution;
    class GpuGraphService;
    /// Coordinates admission and dispatch lifetimes without owning an application's executor.
    class TaskRuntimeState final
    {
    public:
        /// Validates limits and freezes executor capacity before accepting any graph.
        TaskRuntimeState(ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor, RHI::DeviceImpl *device = nullptr);
        /// Releases an already drained runtime and its joined GPU monitor.
        ~TaskRuntimeState();
        /// Returns the optional graph device domain; CPU-only runtimes return an empty handle.
        const eastl::shared_ptr<GraphDevice> &gpuDevice() const;
        /// Retains a GPU wave before any recording callbacks are scheduled.
        void enqueueGpu(eastl::shared_ptr<GpuGraphExecution> execution);
        /// Returns the logical pass budget across all GPU waves of one execution.
        uint32_t maximumPasses() const noexcept;
        /// Returns the finite maximum number of logical passes in a single GPU batch.
        uint32_t maximumPassesPerBatch() const noexcept;
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
        eastl::shared_ptr<GraphDevice> mGpuDevice;
        eastl::unique_ptr<GpuGraphService> mGpuService;
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
