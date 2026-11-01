#include "TaskTestExecutor.hpp"

#include <stdexcept>

TaskTestExecutor::TaskTestExecutor(uint32_t capacity)
    : mCapacity(capacity)
{
}

GVM::Core::ScheduleResult TaskTestExecutor::trySchedule(GVM::Core::ExecutorTask task) noexcept
{
    if (mCalls++ >= mRejectFrom)
    {
        return GVM::Core::ScheduleResult::Unavailable;
    }
    try
    {
        mTasks.push_back(eastl::move(task));
        return GVM::Core::ScheduleResult::Accepted;
    }
    catch (...)
    {
        return GVM::Core::ScheduleResult::Unavailable;
    }
}

uint32_t TaskTestExecutor::concurrency() const noexcept
{
    return mCapacity;
}

bool TaskTestExecutor::executeNext()
{
    if (mTasks.empty())
    {
        return false;
    }
    auto task = eastl::move(mTasks.front());
    mTasks.pop_front();
    task.execute();
    return true;
}

void TaskTestExecutor::drain()
{
    for (uint32_t count = 0; count < 100000; ++count)
    {
        if (!executeNext())
        {
            return;
        }
    }
    throw std::runtime_error("TaskTestExecutor did not drain within its callback budget.");
}

void TaskTestExecutor::rejectFrom(uint32_t index)
{
    mRejectFrom = index;
}

size_t TaskTestExecutor::pending() const
{
    return mTasks.size();
}
