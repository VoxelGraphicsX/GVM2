#include "GpuGraphTestExecutor.hpp"

GpuGraphTestExecutor::GpuGraphTestExecutor(uint32_t acceptedCalls)
    : mExecutor(1), mAcceptedCalls(acceptedCalls)
{
}

GpuGraphTestExecutor::~GpuGraphTestExecutor() = default;

GVM::Core::ScheduleResult GpuGraphTestExecutor::trySchedule(GVM::Core::ExecutorTask task) noexcept
{
    if (mCalls.fetch_add(1, eastl::memory_order_relaxed) >= mAcceptedCalls)
    {
        return GVM::Core::ScheduleResult::Unavailable;
    }
    return mExecutor.trySchedule(eastl::move(task));
}

uint32_t GpuGraphTestExecutor::concurrency() const noexcept
{
    return 1;
}
