#ifndef SAMPLE_HOST_SHARED_APP_WATCHDOG_HPP
#define SAMPLE_HOST_SHARED_APP_WATCHDOG_HPP

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace SampleHostShared
{
    enum class WatchdogPhase : int
    {
        Startup = 0,
        Idle = 1,
        LoadingScene = 2,
        RenderingWarmup = 3,
        Rendering = 4,
        Shutdown = 5,
    };

    inline const char *watchdogPhaseName(WatchdogPhase phase)
    {
        switch (phase)
        {
        case WatchdogPhase::Startup:
            return "startup";
        case WatchdogPhase::Idle:
            return "idle";
        case WatchdogPhase::LoadingScene:
            return "loading-scene";
        case WatchdogPhase::RenderingWarmup:
            return "rendering-warmup";
        case WatchdogPhase::Rendering:
            return "rendering";
        case WatchdogPhase::Shutdown:
            return "shutdown";
        default:
            return "unknown";
        }
    }

    inline double watchdogCurrentTimeMillis()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        return static_cast<double>(microseconds) * 0.001;
    }

    class AppWatchdog final
    {
    public:
        explicit AppWatchdog(std::string label, double stallTimeoutMs, WatchdogPhase ignoredPhase = WatchdogPhase::LoadingScene, double renderingWarmupTimeoutMs = 0.0)
            : mLabel(std::move(label))
            , mStallTimeoutMs(stallTimeoutMs)
            , mIgnoredPhase(ignoredPhase)
            , mRenderingWarmupTimeoutMs(renderingWarmupTimeoutMs > 0.0 ? renderingWarmupTimeoutMs : stallTimeoutMs)
        {
        }

        void start()
        {
            mStopped.store(false);
            touch(WatchdogPhase::Startup);
            mWorker = std::thread([this]() {
                run();
            });
        }

        void touch(WatchdogPhase phase)
        {
            mLastProgressMs.store(watchdogCurrentTimeMillis());
            mCurrentPhase.store(static_cast<int>(phase));
        }

        void stop()
        {
            mStopped.store(true);
            if (mWorker.joinable())
            {
                mWorker.join();
            }
        }

        ~AppWatchdog()
        {
            stop();
        }

    private:
        void run()
        {
            while (!mStopped.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                if (mStopped.load())
                {
                    return;
                }

                const WatchdogPhase phase = static_cast<WatchdogPhase>(mCurrentPhase.load());
                if (phase == mIgnoredPhase)
                {
                    continue;
                }

                const double phaseTimeoutMs = timeoutForPhase(phase);
                const double elapsedMs = watchdogCurrentTimeMillis() - mLastProgressMs.load();
                if (elapsedMs > phaseTimeoutMs)
                {
                    std::cerr << "[" << mLabel << "] watchdog timeout after " << elapsedMs << " ms in phase " << watchdogPhaseName(phase) << " (budget " << phaseTimeoutMs << " ms). Forcing process exit to avoid a hard lock." << std::endl;
                    std::fflush(stderr);
                    std::_Exit(124);
                }
            }
        }

        double timeoutForPhase(WatchdogPhase phase) const
        {
            if (phase == WatchdogPhase::RenderingWarmup)
            {
                return mRenderingWarmupTimeoutMs;
            }
            return mStallTimeoutMs;
        }

        std::string mLabel;
        double mStallTimeoutMs = 0.0;
        WatchdogPhase mIgnoredPhase = WatchdogPhase::LoadingScene;
        double mRenderingWarmupTimeoutMs = 0.0;
        std::atomic<double> mLastProgressMs{0.0};
        std::atomic<int> mCurrentPhase{static_cast<int>(WatchdogPhase::Startup)};
        std::atomic<bool> mStopped{true};
        std::thread mWorker;
    };
} // namespace SampleHostShared

#endif
