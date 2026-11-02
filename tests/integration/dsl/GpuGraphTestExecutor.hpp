#pragma once
#include <GVMCore/Public/ThreadExecutor.hpp>
#include <EASTL/atomic.h>

/// Injects a deterministic scheduling rejection while executing accepted callbacks on real worker threads.
class GpuGraphTestExecutor final : public GVM::Core::ITaskExecutor
{
public:
    /// Accepts the first acceptedCalls scheduling attempts and rejects all later attempts.
    explicit GpuGraphTestExecutor(uint32_t acceptedCalls);
    /// Drains the portable executor after the runtime using it has been destroyed.
    ~GpuGraphTestExecutor() override;
    /// Forwards accepted work to one worker without executing callbacks inline.
    GVM::Core::ScheduleResult trySchedule(GVM::Core::ExecutorTask task) noexcept override;
    /// Reports the single worker used to make callback admission deterministic.
    uint32_t concurrency() const noexcept override;
private:
    GVM::Core::ThreadExecutor mExecutor;
    eastl::atomic<uint32_t> mCalls{0};
    uint32_t mAcceptedCalls;
};
