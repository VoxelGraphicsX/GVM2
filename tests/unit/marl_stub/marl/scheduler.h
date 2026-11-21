#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace marl
{
    class Task
    {
    public:
        enum class Flags : uint32_t
        {
            None = 0,
            SameThread = 1,
        };

        Task() = default;

        template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Task>>>
        Task(F &&fn, Flags flags = Flags::None)
            : mFn(std::forward<F>(fn)), mFlags(flags)
        {
        }

        explicit operator bool() const noexcept { return static_cast<bool>(mFn); }
        Flags flags() const noexcept { return mFlags; }
        void operator()() const { mFn(); }

    private:
        std::function<void()> mFn;
        Flags mFlags = Flags::None;
    };

    class Scheduler
    {
    public:
        struct Config
        {
            struct WorkerThread
            {
                int count = 0;
            } workerThread;

            static Config allCores()
            {
                Config cfg;
                const unsigned int hc = std::thread::hardware_concurrency();
                cfg.workerThread.count = static_cast<int>(std::max(1u, hc));
                return cfg;
            }

            Config &setWorkerThreadCount(int count)
            {
                workerThread.count = count;
                return *this;
            }
        };

        explicit Scheduler(const Config &config)
            : mConfig(config)
        {
            const int count = config.workerThread.count;
            for (int i = 0; i < count; ++i)
            {
                mWorkers.emplace_back([this]()
                {
                    sBoundScheduler = this;
                    for (;;)
                    {
                        Task task;
                        {
                            std::unique_lock lock(mMutex);
                            mCv.wait(lock, [this]() { return mStopping || !mTasks.empty(); });
                            if (mStopping && mTasks.empty())
                            {
                                sBoundScheduler = nullptr;
                                return;
                            }
                            task = std::move(mTasks.front());
                            mTasks.pop();
                        }
                        task();
                    }
                });
            }
        }

        ~Scheduler()
        {
            {
                std::lock_guard lock(mMutex);
                mStopping = true;
            }
            mCv.notify_all();
            for (auto &w : mWorkers)
            {
                if (w.joinable())
                {
                    w.join();
                }
            }
        }

        static Scheduler *get() noexcept { return sBoundScheduler; }
        void bind() noexcept { sBoundScheduler = this; }
        static void unbind() noexcept { sBoundScheduler = nullptr; }

        const Config &config() const noexcept { return mConfig; }

        void enqueue(Task &&task)
        {
            if (task.flags() == Task::Flags::SameThread && sBoundScheduler == nullptr)
            {
                std::abort();
            }
            if (mConfig.workerThread.count == 0 && sBoundScheduler == nullptr)
            {
                std::abort();
            }
            {
                std::lock_guard lock(mMutex);
                mTasks.push(std::move(task));
            }
            mCv.notify_one();
        }

        bool runOnePendingTask()
        {
            Task task;
            {
                std::lock_guard lock(mMutex);
                if (mTasks.empty())
                {
                    return false;
                }
                task = std::move(mTasks.front());
                mTasks.pop();
            }
            if (task)
            {
                task();
            }
            return true;
        }

    private:
        inline static thread_local Scheduler *sBoundScheduler = nullptr;

        Config mConfig;
        std::mutex mMutex;
        std::condition_variable mCv;
        std::queue<Task> mTasks;
        std::vector<std::thread> mWorkers;
        bool mStopping = false;
        bool mBoundOnConstruct = false;
    };
} // namespace marl
