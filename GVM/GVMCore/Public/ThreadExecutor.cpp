#include "ThreadExecutor.hpp"

#include <EASTL/algorithm.h>
#include <stdexcept>

namespace GVM::Core
{
    ThreadExecutor::ThreadExecutor(uint32_t workerCount, uint32_t maximumQueuedTasks)
        : mMaximumQueuedTasks(maximumQueuedTasks)
    {
        if (maximumQueuedTasks == 0)
        {
            throw std::invalid_argument("ThreadExecutor requires a nonzero queue capacity.");
        }
        if (workerCount == 0)
        {
            workerCount = eastl::max(1u, std::thread::hardware_concurrency());
        }
        mWorkers.reserve(workerCount);
        try
        {
            for (uint32_t index = 0; index < workerCount; ++index)
            {
                mWorkers.emplace_back(&ThreadExecutor::consume, this);
            }
        }
        catch (...)
        {
            shutdown();
            throw;
        }
    }

    ThreadExecutor::~ThreadExecutor()
    {
        shutdown();
    }

    ScheduleResult ThreadExecutor::trySchedule(ExecutorTask task) noexcept
    {
        if (!task)
        {
            return ScheduleResult::Unavailable;
        }
        try
        {
            {
                std::lock_guard lock(mMutex);
                if (mStopping)
                {
                    return ScheduleResult::Stopping;
                }
                if (mTasks.size() >= mMaximumQueuedTasks)
                {
                    return ScheduleResult::Unavailable;
                }
                mTasks.push_back(eastl::move(task));
            }
            mReady.notify_one();
            return ScheduleResult::Accepted;
        }
        catch (...)
        {
            return ScheduleResult::Unavailable;
        }
    }

    uint32_t ThreadExecutor::concurrency() const noexcept
    {
        return static_cast<uint32_t>(mWorkers.size());
    }

    void ThreadExecutor::consume()
    {
        for (;;)
        {
            ExecutorTask task;
            {
                std::unique_lock lock(mMutex);
                while (!mStopping && mTasks.empty())
                {
                    mReady.wait(lock);
                }
                if (mTasks.empty())
                {
                    return;
                }
                task = eastl::move(mTasks.front());
                mTasks.pop_front();
            }
            task.execute();
        }
    }

    void ThreadExecutor::shutdown() noexcept
    {
        {
            std::lock_guard lock(mMutex);
            mStopping = true;
        }
        mReady.notify_all();
        for (auto &worker : mWorkers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }
}
