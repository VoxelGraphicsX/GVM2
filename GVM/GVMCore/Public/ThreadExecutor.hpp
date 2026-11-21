#pragma once

#include "ITaskExecutor.hpp"
#include <EASTL/deque.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace GVM::Core
{
    /// Provides a fiber-free, zero-external-dependency work-stealing worker pool; destroy it on a host thread after its clients drain.
    class ThreadExecutor final : public ITaskExecutor
    {
    public:
        /// Starts workers; zero selects hardware concurrency, with a minimum of one worker.
        /// UINT32_MAX (default) permits unbounded queued bursts; smaller values enforce a hard queue limit.
        explicit ThreadExecutor(uint32_t workerCount = 0, uint32_t maximumQueuedTasks = std::numeric_limits<uint32_t>::max());
        /// Stops accepting work and joins workers after all accepted callbacks finish.
        ~ThreadExecutor() override;
        ThreadExecutor(const ThreadExecutor &) = delete;
        ThreadExecutor &operator=(const ThreadExecutor &) = delete;

        /// Enqueues without blocking for capacity; rejection never invokes the callback.
        ScheduleResult trySchedule(ExecutorTask task) noexcept override;
        /// Enqueues up to count tasks cleanly across worker queues, waking only needed sleeping workers.
        size_t scheduleBatch(ExecutorTask *tasks, size_t count) noexcept override;
        /// Reports the immutable worker count selected at construction.
        uint32_t concurrency() const noexcept override;

    private:
        struct alignas(64) WorkerQueue
        {
            std::mutex mutex;
            eastl::deque<ExecutorTask> tasks;
        };

        /// Consumes local tasks and steals from sibling workers until shutdown drains all queues.
        void consume(uint32_t workerIndex);
        /// Tries to pop one task from workerIndex's local queue (LIFO) or steal from others (FIFO).
        bool tryPopOrSteal(uint32_t workerIndex, ExecutorTask &outTask) noexcept;
        /// Reserves capacity for up to desired tasks atomically without exceeding mMaximumQueuedTasks.
        size_t reserveCapacity(size_t desired) noexcept;
        /// Wakes up to count sleeping workers if any are parked.
        void wakeWorkers(size_t count) noexcept;
        /// Marks the executor closed, waits for active producers, and joins all workers.
        void shutdown() noexcept;

        eastl::vector<eastl::unique_ptr<WorkerQueue>> mQueues;
        eastl::vector<std::thread> mWorkers;
        std::mutex mWakeMutex;
        std::condition_variable mWakeCv;
        uint32_t mMaximumQueuedTasks;
        std::atomic<size_t> mQueuedCount{0};
        std::atomic<uint32_t> mNextQueue{0};
        std::atomic<uint32_t> mSleepingWorkers{0};
        std::atomic<uint32_t> mActiveProducers{0};
        std::atomic<bool> mStopping{false};
    };
}
