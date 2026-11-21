#include "ThreadExecutor.hpp"

#include <EASTL/algorithm.h>
#include <stdexcept>

namespace GVM::Core
{
    namespace
    {
        struct WorkerThreadTag
        {
            const ThreadExecutor *owner = nullptr;
            uint32_t index = UINT32_MAX;
        };

        thread_local WorkerThreadTag tCurrentWorker;
    } // namespace

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
        mQueues.reserve(workerCount);
        for (uint32_t index = 0; index < workerCount; ++index)
        {
            mQueues.push_back(eastl::make_unique<WorkerQueue>());
        }
        mWorkers.reserve(workerCount);
        try
        {
            for (uint32_t index = 0; index < workerCount; ++index)
            {
                mWorkers.emplace_back(&ThreadExecutor::consume, this, index);
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

    size_t ThreadExecutor::reserveCapacity(size_t desired) noexcept
    {
        if (desired == 0)
        {
            return 0;
        }
        if (mMaximumQueuedTasks == std::numeric_limits<uint32_t>::max())
        {
            mQueuedCount.fetch_add(desired, std::memory_order_acq_rel);
            return desired;
        }
        size_t current = mQueuedCount.load(std::memory_order_relaxed);
        for (;;)
        {
            if (current >= mMaximumQueuedTasks)
            {
                return 0;
            }
            const size_t available = mMaximumQueuedTasks - current;
            const size_t take = desired < available ? desired : available;
            if (mQueuedCount.compare_exchange_weak(
                    current,
                    current + take,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                return take;
            }
        }
    }

    void ThreadExecutor::wakeWorkers(size_t count) noexcept
    {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const uint32_t sleeping = mSleepingWorkers.load(std::memory_order_relaxed);
        if (sleeping == 0 || count == 0)
        {
            return;
        }
        {
            std::lock_guard lock(mWakeMutex);
        }
        if (count >= mWorkers.size() || count >= sleeping)
        {
            mWakeCv.notify_all();
        }
        else
        {
            for (size_t i = 0; i < count; ++i)
            {
                mWakeCv.notify_one();
            }
        }
    }

    ScheduleResult ThreadExecutor::trySchedule(ExecutorTask task) noexcept
    {
        if (!task)
        {
            return ScheduleResult::Unavailable;
        }
        struct ProducerGuard
        {
            std::atomic<uint32_t> &counter;
            ~ProducerGuard() { counter.fetch_sub(1, std::memory_order_release); }
        };
        mActiveProducers.fetch_add(1, std::memory_order_acq_rel);
        ProducerGuard guard{mActiveProducers};

        if (mStopping.load(std::memory_order_acquire))
        {
            return ScheduleResult::Stopping;
        }
        if (reserveCapacity(1) == 0)
        {
            return ScheduleResult::Unavailable;
        }
        try
        {
            const uint32_t queueCount = static_cast<uint32_t>(mQueues.size());
            const bool isLocalWorker = tCurrentWorker.owner == this && tCurrentWorker.index < queueCount;
            const uint32_t target = isLocalWorker
                ? tCurrentWorker.index
                : (mNextQueue.fetch_add(1, std::memory_order_relaxed) % queueCount);
            {
                std::lock_guard lock(mQueues[target]->mutex);
                mQueues[target]->tasks.push_back(std::move(task));
            }
            wakeWorkers(1);
            return ScheduleResult::Accepted;
        }
        catch (...)
        {
            mQueuedCount.fetch_sub(1, std::memory_order_release);
            return ScheduleResult::Unavailable;
        }
    }

    size_t ThreadExecutor::scheduleBatch(ExecutorTask *tasks, size_t count) noexcept
    {
        if (tasks == nullptr || count == 0)
        {
            return 0;
        }
        struct ProducerGuard
        {
            std::atomic<uint32_t> &counter;
            ~ProducerGuard() { counter.fetch_sub(1, std::memory_order_release); }
        };
        mActiveProducers.fetch_add(1, std::memory_order_acq_rel);
        ProducerGuard guard{mActiveProducers};

        if (mStopping.load(std::memory_order_acquire))
        {
            return 0;
        }
        size_t validCount = 0;
        while (validCount < count && static_cast<bool>(tasks[validCount]))
        {
            ++validCount;
        }
        if (validCount == 0)
        {
            return 0;
        }
        const size_t reserved = reserveCapacity(validCount);
        if (reserved == 0)
        {
            return 0;
        }

        const uint32_t queueCount = static_cast<uint32_t>(mQueues.size());
        const bool isLocalWorker = tCurrentWorker.owner == this && tCurrentWorker.index < queueCount;
        size_t pushed = 0;
        try
        {
            if (isLocalWorker)
            {
                auto &localQueue = *mQueues[tCurrentWorker.index];
                std::lock_guard lock(localQueue.mutex);
                for (; pushed < reserved; ++pushed)
                {
                    localQueue.tasks.push_back(std::move(tasks[pushed]));
                }
            }
            else
            {
                const uint32_t startQueue = mNextQueue.fetch_add(static_cast<uint32_t>(reserved), std::memory_order_relaxed) % queueCount;
                const size_t basePerQueue = reserved / queueCount;
                const size_t remainder = reserved % queueCount;
                for (uint32_t q = 0; q < queueCount && pushed < reserved; ++q)
                {
                    const size_t quota = basePerQueue + (q < remainder ? 1u : 0u);
                    if (quota == 0)
                    {
                        break;
                    }
                    const uint32_t target = (startQueue + q) % queueCount;
                    std::lock_guard lock(mQueues[target]->mutex);
                    const size_t chunkEnd = pushed + quota;
                    for (; pushed < chunkEnd; ++pushed)
                    {
                        mQueues[target]->tasks.push_back(std::move(tasks[pushed]));
                    }
                }
            }
        }
        catch (...)
        {
            if (pushed < reserved)
            {
                mQueuedCount.fetch_sub(reserved - pushed, std::memory_order_release);
            }
        }
        if (pushed > 0)
        {
            wakeWorkers(pushed);
        }
        return pushed;
    }

    uint32_t ThreadExecutor::concurrency() const noexcept
    {
        return static_cast<uint32_t>(mWorkers.size());
    }

    bool ThreadExecutor::tryPopOrSteal(uint32_t workerIndex, ExecutorTask &outTask) noexcept
    {
        const uint32_t queueCount = static_cast<uint32_t>(mQueues.size());
        {
            auto &local = *mQueues[workerIndex];
            std::lock_guard lock(local.mutex);
            if (!local.tasks.empty())
            {
                outTask = std::move(local.tasks.back());
                local.tasks.pop_back();
                mQueuedCount.fetch_sub(1, std::memory_order_release);
                return true;
            }
        }
        for (uint32_t offset = 1; offset < queueCount; ++offset)
        {
            const uint32_t victimIndex = (workerIndex + offset) % queueCount;
            auto &victim = *mQueues[victimIndex];
            std::unique_lock lock(victim.mutex, std::try_to_lock);
            if (lock.owns_lock() && !victim.tasks.empty())
            {
                outTask = std::move(victim.tasks.front());
                victim.tasks.pop_front();
                mQueuedCount.fetch_sub(1, std::memory_order_release);
                return true;
            }
        }
        for (uint32_t offset = 1; offset < queueCount; ++offset)
        {
            const uint32_t victimIndex = (workerIndex + offset) % queueCount;
            auto &victim = *mQueues[victimIndex];
            std::lock_guard lock(victim.mutex);
            if (!victim.tasks.empty())
            {
                outTask = std::move(victim.tasks.front());
                victim.tasks.pop_front();
                mQueuedCount.fetch_sub(1, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    void ThreadExecutor::consume(uint32_t workerIndex)
    {
        tCurrentWorker = {this, workerIndex};
        for (;;)
        {
            ExecutorTask task;
            if (tryPopOrSteal(workerIndex, task))
            {
                task.execute();
                continue;
            }
            if (mStopping.load(std::memory_order_acquire) && mQueuedCount.load(std::memory_order_acquire) == 0)
            {
                return;
            }
            mSleepingWorkers.fetch_add(1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if (mQueuedCount.load(std::memory_order_acquire) == 0 && !mStopping.load(std::memory_order_acquire))
            {
                std::unique_lock lock(mWakeMutex);
                mWakeCv.wait(lock, [this]
                {
                    return mStopping.load(std::memory_order_acquire) || mQueuedCount.load(std::memory_order_acquire) > 0;
                });
            }
            mSleepingWorkers.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    void ThreadExecutor::shutdown() noexcept
    {
        mStopping.store(true, std::memory_order_seq_cst);
        while (mActiveProducers.load(std::memory_order_acquire) != 0)
        {
            std::this_thread::yield();
        }
        {
            std::lock_guard lock(mWakeMutex);
        }
        mWakeCv.notify_all();
        for (auto &worker : mWorkers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }
}
