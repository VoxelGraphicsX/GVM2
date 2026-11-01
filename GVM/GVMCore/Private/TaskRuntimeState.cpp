#include "TaskRuntimeState.hpp"

#include <EASTL/algorithm.h>
#include <stdexcept>

namespace GVM::Core::Private
{
    TaskRuntimeState::TaskRuntimeState(ITaskExecutor &executor, const TaskRuntimeDescriptor &descriptor)
        : mExecutor(executor), mDescriptor(descriptor), mMaximumInFlight(executor.concurrency())
    {
        if (mMaximumInFlight == 0 || descriptor.maximumActiveExecutions == 0 || descriptor.maximumTasksPerExecution == 0)
        {
            throw std::invalid_argument("TaskRuntime requires a usable executor and nonzero admission limits.");
        }
        if (descriptor.maximumInFlightTasksPerExecution != 0)
        {
            mMaximumInFlight = eastl::min(mMaximumInFlight, descriptor.maximumInFlightTasksPerExecution);
        }
    }

    bool TaskRuntimeState::tryAdmit()
    {
        std::lock_guard lock(mMutex);
        if (mClosing || mActive >= mDescriptor.maximumActiveExecutions)
        {
            return false;
        }
        ++mActive;
        return true;
    }

    void TaskRuntimeState::complete() noexcept
    {
        {
            std::lock_guard lock(mMutex);
            --mActive;
        }
        mDrained.notify_all();
    }

    ScheduleResult TaskRuntimeState::schedule(ExecutorTask task) noexcept
    {
        {
            std::lock_guard lock(mMutex);
            ++mDispatching;
        }
        const auto result = mExecutor.trySchedule(eastl::move(task));
        {
            std::lock_guard lock(mMutex);
            --mDispatching;
        }
        mDrained.notify_all();
        return result;
    }

    void TaskRuntimeState::closeAndWait() noexcept
    {
        std::unique_lock lock(mMutex);
        mClosing = true;
        while (mActive != 0 || mDispatching != 0)
        {
            mDrained.wait(lock);
        }
    }

    uint32_t TaskRuntimeState::maximumTasks() const noexcept
    {
        return mDescriptor.maximumTasksPerExecution;
    }

    uint32_t TaskRuntimeState::maximumInFlight() const noexcept
    {
        return mMaximumInFlight;
    }
}
