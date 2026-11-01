#pragma once

#include <GVMCore/Public/ITaskExecutor.hpp>
#include <EASTL/deque.h>

/// Provides deterministic, manually advanced executor callbacks for runtime state-machine tests.
class TaskTestExecutor final : public GVM::Core::ITaskExecutor
{
public:
    /// Creates an executor that never invokes callbacks inside trySchedule.
    explicit TaskTestExecutor(uint32_t capacity = 1);
    /// Accepts callbacks until the configured rejection index, retaining their ownership.
    GVM::Core::ScheduleResult trySchedule(GVM::Core::ExecutorTask task) noexcept override;
    /// Reports the simulated admission capacity used by the runtime.
    uint32_t concurrency() const noexcept override;
    /// Executes one accepted callback outside the submission call stack.
    bool executeNext();
    /// Executes a bounded number of callbacks and fails if the graph does not terminate.
    void drain();
    /// Rejects this and every later schedule attempt using a zero-based call index.
    void rejectFrom(uint32_t index);
    /// Returns the number of accepted callbacks that have not been advanced.
    size_t pending() const;

private:
    eastl::deque<GVM::Core::ExecutorTask> mTasks;
    uint32_t mCapacity;
    uint32_t mCalls = 0;
    uint32_t mRejectFrom = UINT32_MAX;
};
