#pragma once

#define GVM_TESTING_MARL_STUB 1

#include "scheduler.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

namespace marl
{
    class Event
    {
    public:
        enum class Mode : uint8_t
        {
            Auto,
            Manual,
        };

        explicit Event(Mode mode = Mode::Auto, bool initialState = false)
            : mShared(std::make_shared<Shared>(mode, initialState))
        {
            sAllocationCount.fetch_add(1, std::memory_order_relaxed);
        }

        void signal() const
        {
            std::lock_guard lock(mShared->mutex);
            if (mShared->signalled)
            {
                return;
            }
            mShared->signalled = true;
            if (mShared->mode == Mode::Auto)
            {
                mShared->cv.notify_one();
            }
            else
            {
                mShared->cv.notify_all();
            }
        }

        void clear() const
        {
            std::lock_guard lock(mShared->mutex);
            mShared->signalled = false;
        }

        void wait() const
        {
            if (auto *scheduler = Scheduler::get())
            {
                for (;;)
                {
                    if (test())
                    {
                        return;
                    }
                    if (!scheduler->runOnePendingTask())
                    {
                        std::unique_lock lock(mShared->mutex);
                        if (mShared->consumeIfSignalledLocked())
                        {
                            return;
                        }
                        mShared->cv.wait_for(lock, std::chrono::microseconds(50));
                        if (mShared->consumeIfSignalledLocked())
                        {
                            return;
                        }
                    }
                }
            }
            std::unique_lock lock(mShared->mutex);
            mShared->cv.wait(lock, [this]() { return mShared->signalled; });
            if (mShared->mode == Mode::Auto)
            {
                mShared->signalled = false;
            }
        }

        template <typename Rep, typename Period>
        bool wait_for(const std::chrono::duration<Rep, Period> &duration) const
        {
            return wait_until(std::chrono::system_clock::now() + duration);
        }

        template <typename Clock, typename Duration>
        bool wait_until(const std::chrono::time_point<Clock, Duration> &timeout) const
        {
            static_assert(std::is_same_v<Clock, std::chrono::system_clock>,
                "Upstream google/marl Fiber::wait requires std::chrono::system_clock time_points.");
            if (auto *scheduler = Scheduler::get())
            {
                for (;;)
                {
                    if (test())
                    {
                        return true;
                    }
                    if (Clock::now() >= timeout)
                    {
                        return false;
                    }
                    if (!scheduler->runOnePendingTask())
                    {
                        std::unique_lock lock(mShared->mutex);
                        if (mShared->consumeIfSignalledLocked())
                        {
                            return true;
                        }
                        const auto now = Clock::now();
                        if (now >= timeout)
                        {
                            return false;
                        }
                        const auto slice = std::min<std::chrono::nanoseconds>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - now),
                            std::chrono::microseconds(50));
                        mShared->cv.wait_for(lock, slice);
                        if (mShared->consumeIfSignalledLocked())
                        {
                            return true;
                        }
                    }
                }
            }
            std::unique_lock lock(mShared->mutex);
            if (!mShared->cv.wait_until(lock, timeout, [this]() { return mShared->signalled; }))
            {
                return false;
            }
            if (mShared->mode == Mode::Auto)
            {
                mShared->signalled = false;
            }
            return true;
        }

        bool test() const
        {
            std::lock_guard lock(mShared->mutex);
            return mShared->consumeIfSignalledLocked();
        }

        bool isSignalled() const
        {
            std::lock_guard lock(mShared->mutex);
            return mShared->signalled;
        }

        static uint64_t allocationCount() noexcept
        {
            return sAllocationCount.load(std::memory_order_relaxed);
        }

    private:
        struct Shared
        {
            Shared(Mode m, bool init) : mode(m), signalled(init) {}

            bool consumeIfSignalledLocked()
            {
                if (!signalled)
                {
                    return false;
                }
                if (mode == Mode::Auto)
                {
                    signalled = false;
                }
                return true;
            }

            Mode mode;
            std::mutex mutex;
            std::condition_variable cv;
            bool signalled = false;
        };

        inline static std::atomic<uint64_t> sAllocationCount{0};
        std::shared_ptr<Shared> mShared;
    };
} // namespace marl
