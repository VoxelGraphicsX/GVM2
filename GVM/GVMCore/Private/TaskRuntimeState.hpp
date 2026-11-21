#pragma once

#include "../Public/TaskRuntime.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace GVM::Core::Private
{
    class GraphDevice;
    class GpuGraphExecution;
    class PassWeightProfiler;
    /// Coordinates admission and dispatch lifetimes without owning an application's executor.
    class TaskRuntimeState final
    {
    public:
        /// Validates limits and freezes executor capacity before accepting any graph.
        TaskRuntimeState(ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor, RHI::DeviceImpl *device = nullptr);
        /// Releases an already drained runtime.
        ~TaskRuntimeState();
        /// Returns the optional graph device domain; CPU-only runtimes return an empty handle.
        const eastl::shared_ptr<GraphDevice> &gpuDevice() const;
        /// Returns the optional cross-frame GPU timestamp pass weight profiler when enabled.
        PassWeightProfiler *passWeightProfiler() const noexcept;
        /// Returns the logical pass budget across all GPU launch groups of one execution.
        uint32_t maximumPasses() const noexcept;
        /// Returns the finite maximum number of logical passes in a single GPU batch.
        uint32_t maximumPassesPerBatch() const noexcept;
        /// Reserves one execution slot unless admission is closed or full.
        bool tryAdmit();
        /// Releases exactly one admitted execution slot and wakes a draining owner.
        void complete() noexcept;
        /// Protects an executor call against concurrent runtime destruction, without holding the admission lock.
        ScheduleResult schedule(ExecutorTask task) noexcept;
        /// Protects a batch executor call against concurrent runtime destruction without holding locks on the fast path.
        size_t scheduleBatch(ExecutorTask *tasks, size_t count) noexcept;
        /// Stops admitting graphs and waits for this runtime's executions and dispatch calls.
        void closeAndWait() noexcept;
        /// Returns the immutable operation-count limit for one execution.
        uint32_t maximumTasks() const noexcept;
        /// Returns the immutable maximum number of accepted jobs for one execution.
        uint32_t maximumInFlight() const noexcept;
        /// Delegates waiting to the configured executor while retaining runtime drain lifetime.
        bool waitCondition(
            std::unique_lock<std::mutex> &lock,
            std::condition_variable &condition,
            uint64_t timeoutNanoseconds,
            ITaskExecutor::WaitPredicate predicate,
            const void *context) noexcept;
        /// Delegates condition notification to the configured executor so cooperative fiber waiters wake immediately.
        void notifyCondition(std::condition_variable &condition) noexcept;
        /// Retains runtime drain lifetime while an execution publishes terminal completion notifications.
        void beginCallbackDrain() noexcept;
        /// Releases runtime drain lifetime retained by beginCallbackDrain().
        void endCallbackDrain() noexcept;

    private:
        /// Decrements an atomic drain counter, serializing the final 1-to-0 transition under mMutex.
        void releaseDrainCounter(std::atomic<uint32_t> &counter) noexcept;

        eastl::shared_ptr<GraphDevice> mGpuDevice;
        eastl::unique_ptr<PassWeightProfiler> mPassWeightProfiler;
        ITaskExecutor &mExecutor;
        TaskRuntimeDescriptor mDescriptor;
        uint32_t mMaximumInFlight;
        std::mutex mMutex;
        std::condition_variable mDrained;
        uint32_t mActive = 0;
        std::atomic<uint32_t> mDispatching{0};
        std::atomic<uint32_t> mWaiting{0};
        std::atomic<bool> mClosingAtomic{false};
        bool mClosing = false;
    };
}
