#pragma once

#include "ITaskExecutor.hpp"
#include <EASTL/deque.h>
#include <EASTL/vector.h>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace GVM::Core
{
    /// Provides a bounded portable worker pool; destroy it on a host thread after its clients drain.
    class ThreadExecutor final : public ITaskExecutor
    {
    public:
        /// Starts workers; zero selects hardware concurrency, with a minimum of one worker.
        explicit ThreadExecutor(uint32_t workerCount = 0, uint32_t maximumQueuedTasks = 4096);
        /// Stops accepting work and joins workers after all accepted callbacks finish.
        ~ThreadExecutor() override;
        ThreadExecutor(const ThreadExecutor &) = delete;
        ThreadExecutor &operator=(const ThreadExecutor &) = delete;

        /// Enqueues without blocking for capacity; rejection never invokes the callback.
        ScheduleResult trySchedule(ExecutorTask task) noexcept override;
        /// Reports the immutable worker count selected at construction.
        uint32_t concurrency() const noexcept override;

    private:
        /// Consumes ready callbacks outside the queue lock until shutdown has drained the queue.
        void consume();
        /// Marks the queue closed and joins each worker without holding the queue lock.
        void shutdown() noexcept;

        std::mutex mMutex;
        std::condition_variable mReady;
        eastl::deque<ExecutorTask> mTasks;
        eastl::vector<std::thread> mWorkers;
        uint32_t mMaximumQueuedTasks;
        bool mStopping = false;
    };
}
