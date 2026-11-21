// Include MarlTaskExecutor.hpp first to verify self-contained header compilation.
#include <GVMCore/Public/MarlTaskExecutor.hpp>
#include <GVMCore/Public/TaskRuntime.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

namespace
{
    using namespace GVM::Core;

    struct ScopedMarlBind final
    {
        explicit ScopedMarlBind(marl::Scheduler &scheduler)
        {
            scheduler.bind();
        }
        ~ScopedMarlBind()
        {
            marl::Scheduler::unbind();
        }
        ScopedMarlBind(const ScopedMarlBind &) = delete;
        ScopedMarlBind &operator=(const ScopedMarlBind &) = delete;
    };

    TEST(MarlTaskExecutorTests, RunsTaskGraphThroughOptionalMarlAdapter)
    {
        auto config = marl::Scheduler::Config::allCores().setWorkerThreadCount(3);
        marl::Scheduler scheduler(config);
        MarlTaskExecutor executor(scheduler);
        EXPECT_EQ(executor.concurrency(), 3u);

        TaskRuntime runtime(executor);
        auto execution = runtime.submit([](int base)
        {
            TaskGraph graph;
            auto a = graph.add([](int v) { return v * 2; }, base);
            auto b = graph.add([](int v) { return v + 5; }, base);
            return graph.add([](int x, int y) { return x + y; }, a, b);
        }, 10);

        ASSERT_TRUE(execution.wait(5'000'000'000ull));
        ASSERT_EQ(execution.status(), ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_EQ(execution.get(), 35);
    }

    TEST(MarlTaskExecutorTests, NestedExecutionWaitDoesNotDeadlockSingleWorkerScheduler)
    {
        marl::Scheduler::Config config;
        config.workerThread.count = 1;
        marl::Scheduler scheduler(config);
        MarlTaskExecutor executor(scheduler);
        EXPECT_EQ(executor.concurrency(), 1u);

        TaskRuntime runtime(executor);
        auto outer = runtime.submit([&runtime](int seed)
        {
            TaskGraph graph;
            return graph.add([&runtime](int value)
            {
                auto inner = runtime.submit([](int innerSeed)
                {
                    TaskGraph innerGraph;
                    auto left = innerGraph.add([](int x) { return x + 7; }, innerSeed);
                    auto right = innerGraph.add([](int x) { return x * 3; }, innerSeed);
                    return innerGraph.add([](int l, int r) { return l + r; }, left, right);
                }, value);

                EXPECT_TRUE(inner.wait(2'000'000'000ull));
                return inner.get();
            }, seed);
        }, 11);

        ASSERT_TRUE(outer.wait(2'000'000'000ull));
        ASSERT_EQ(outer.status(), ExecutionStatus::Succeeded) << outer.error().c_str();
        EXPECT_EQ(outer.get(), (11 + 7) + (11 * 3));
    }

    TEST(MarlTaskExecutorTests, SingleThreadZeroWorkerBoundSchedulerPumpsCooperativelyOnWait)
    {
        marl::Scheduler::Config config;
        config.workerThread.count = 0;
        marl::Scheduler scheduler(config);
        ScopedMarlBind bindGuard(scheduler);
        MarlTaskExecutor executor(scheduler);
        EXPECT_EQ(executor.concurrency(), 1u);

        TaskRuntime runtime(executor);
        auto execution = runtime.submit([](int base)
        {
            TaskGraph graph;
            auto step1 = graph.add([](int x) { return x + 4; }, base);
            auto step2 = graph.add([](int x) { return x * 5; }, step1);
            return step2;
        }, 6);

        ASSERT_TRUE(execution.wait(2'000'000'000ull));
        ASSERT_EQ(execution.status(), ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_EQ(execution.get(), 50);
    }

    TEST(MarlTaskExecutorTests, BoundThreadWaitRespectsTimeoutAndCompletesAfterExternalSignal)
    {
        {
            marl::Scheduler::Config config;
            config.workerThread.count = 0;
            marl::Scheduler scheduler(config);
            ScopedMarlBind bindGuard(scheduler);
            MarlTaskExecutor executor(scheduler);
            TaskRuntime runtime(executor);

            std::atomic<bool> gate{false};
            auto execution = runtime.submit([&](int seed)
            {
                TaskGraph graph;
                return graph.add([&](int x) -> int
                {
                    while (!gate.load(std::memory_order_acquire))
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    return x + 42;
                }, seed);
            }, 0);

            gate.store(true, std::memory_order_release);
            ASSERT_TRUE(execution.wait(2'000'000'000ull));
            EXPECT_EQ(execution.get(), 42);
        }

        marl::Scheduler::Config workerCfg;
        workerCfg.workerThread.count = 1;
        marl::Scheduler workerScheduler(workerCfg);
        MarlTaskExecutor workerExecutor(workerScheduler);
        TaskRuntime workerRuntime(workerExecutor);

        std::atomic<bool> slowTaskStarted{false};
        std::atomic<bool> releaseSlowTask{false};
        auto slowExec = workerRuntime.submit([&](int seed)
        {
            TaskGraph g;
            return g.add([&](int x) -> int
            {
                slowTaskStarted.store(true, std::memory_order_release);
                while (!releaseSlowTask.load(std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return x + 99;
            }, seed);
        }, 0);

        while (!slowTaskStarted.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        {
            ScopedMarlBind bindGuard(workerScheduler);
            EXPECT_FALSE(slowExec.wait(5'000'000ull));
            releaseSlowTask.store(true, std::memory_order_release);
            EXPECT_TRUE(slowExec.wait(2'000'000'000ull));
        }
        EXPECT_EQ(slowExec.get(), 99);
    }

    TEST(MarlTaskExecutorTests, ScheduleBatchRunsAllTasksAndRejectsWhenStopping)
    {
        marl::Scheduler::Config config;
        config.workerThread.count = 4;
        marl::Scheduler scheduler(config);
        // SameThread flags passed from an unbound thread safely fall back to worker enqueue instead of crashing.
        MarlTaskExecutor executor(scheduler, 0, marl::Task::Flags::SameThread);

        EXPECT_EQ(executor.scheduleBatch(nullptr, 4), 0u);

        std::atomic<uint32_t> counter{0};
        ExecutorTask batch[10];
        for (size_t i = 0; i < 8; ++i)
        {
            batch[i] = ExecutorTask([&counter]()
            {
                counter.fetch_add(1u, std::memory_order_relaxed);
            });
        }
        // Trailing empty tasks at indices 8 and 9 must stop leading batch acceptance cleanly.
        EXPECT_EQ(executor.scheduleBatch(batch, 10), 8u);

        TaskRuntime runtime(executor);
        auto fanOut = runtime.submit([](int base)
        {
            TaskGraph graph;
            std::vector<Task<int>> leaves;
            leaves.reserve(48);
            for (int i = 0; i < 48; ++i)
            {
                leaves.push_back(graph.add([i](int v) { return v + i; }, base));
            }
            Task<int> acc = leaves[0];
            for (size_t i = 1; i < leaves.size(); ++i)
            {
                acc = graph.add([](int a, int b) { return a + b; }, acc, leaves[i]);
            }
            return acc;
        }, 1);
        ASSERT_TRUE(fanOut.wait(5'000'000'000ull));
        ASSERT_EQ(fanOut.status(), ExecutionStatus::Succeeded) << fanOut.error().c_str();
        EXPECT_EQ(fanOut.get(), 48 + (47 * 48) / 2);

        executor.shutdown();
        EXPECT_EQ(counter.load(std::memory_order_acquire), 8u);

        bool ranAfterShutdown = false;
        ExecutorTask postShutdown[2]{
            ExecutorTask([&]() { ranAfterShutdown = true; }),
            ExecutorTask([&]() { ranAfterShutdown = true; }),
        };
        EXPECT_EQ(executor.scheduleBatch(postShutdown, 2), 0u);
        EXPECT_FALSE(ranAfterShutdown);
        EXPECT_TRUE(static_cast<bool>(postShutdown[0]));
    }

    TEST(MarlTaskExecutorTests, BoundFiberWaitWakesImmediatelyAndPoolsEventNodes)
    {
        marl::Scheduler::Config config;
        config.workerThread.count = 1;
        marl::Scheduler scheduler(config);
        ScopedMarlBind bindGuard(scheduler);
        MarlTaskExecutor executor(scheduler);

        std::mutex mutex;
        std::condition_variable cv;
        bool ready = false;
        const auto pred = [](const void *ctx) noexcept -> bool
        {
            return *static_cast<const bool *>(ctx);
        };

#if defined(GVM_TESTING_MARL_STUB)
        uint64_t allocationsAfterWarmup = 0;
#endif
        for (int round = 0; round < 6; ++round)
        {
            {
                std::lock_guard lock(mutex);
                ready = false;
            }
            ASSERT_EQ(executor.trySchedule(ExecutorTask([&]()
            {
                {
                    std::lock_guard lock(mutex);
                    ready = true;
                }
                executor.notifyCondition(cv);
            })), ScheduleResult::Accepted);

            std::unique_lock lock(mutex);
            EXPECT_TRUE(executor.waitCondition(lock, cv, std::numeric_limits<uint64_t>::max(), pred, &ready));
            EXPECT_TRUE(ready);
#if defined(GVM_TESTING_MARL_STUB)
            if (round == 0)
            {
                allocationsAfterWarmup = marl::Event::allocationCount();
            }
#endif
        }
#if defined(GVM_TESTING_MARL_STUB)
        // Subsequent serial waits reuse the pooled FiberWaiter node without allocating new marl::Event Shared blocks.
        EXPECT_EQ(marl::Event::allocationCount(), allocationsAfterWarmup);
#endif
    }

    TEST(MarlTaskExecutorTests, PeerBoundSchedulerYieldsFiberWhenWaitingOnAnotherMarlRuntime)
    {
        // Scheduler A has 0 worker threads (bound to current thread) and runs a local task that must progress
        // while the calling fiber waits on a TaskRuntime backed by Scheduler B (1 worker thread).
        marl::Scheduler::Config cfgA;
        cfgA.workerThread.count = 0;
        marl::Scheduler schedulerA(cfgA);
        ScopedMarlBind bindA(schedulerA);

        marl::Scheduler::Config cfgB;
        cfgB.workerThread.count = 1;
        marl::Scheduler schedulerB(cfgB);
        MarlTaskExecutor executorB(schedulerB);
        TaskRuntime runtimeB(executorB);

        std::atomic<bool> schedulerAPumped{false};
        auto execB = runtimeB.submit([&](int seed)
        {
            TaskGraph graph;
            return graph.add([&](int v)
            {
                while (!schedulerAPumped.load(std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
                return v + 100;
            }, seed);
        }, 23);

        // Enqueue work onto Scheduler A that runs only when execB.wait() cooperatively pumps Scheduler A's fiber.
        schedulerA.enqueue(marl::Task([&]()
        {
            schedulerAPumped.store(true, std::memory_order_release);
        }));

        ASSERT_TRUE(execB.wait(2'000'000'000ull));
        EXPECT_TRUE(schedulerAPumped.load(std::memory_order_acquire));
        EXPECT_EQ(execB.get(), 123);
    }

    TEST(MarlTaskExecutorTests, ShutdownCalledFromInsideAcceptedMarlTaskDrainsPeersWithoutSelfDeadlock)
    {
        marl::Scheduler::Config cfg;
        cfg.workerThread.count = 2;
        marl::Scheduler scheduler(cfg);
        MarlTaskExecutor executor(scheduler);

        std::atomic<bool> peerFinished{false};
        std::atomic<bool> selfShutdownReturned{false};

        ASSERT_EQ(executor.trySchedule(ExecutorTask([&peerFinished]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            peerFinished.store(true, std::memory_order_release);
        })), ScheduleResult::Accepted);

        ASSERT_EQ(executor.trySchedule(ExecutorTask([&executor, &peerFinished, &selfShutdownReturned]()
        {
            executor.shutdown();
            EXPECT_TRUE(peerFinished.load(std::memory_order_acquire));
            selfShutdownReturned.store(true, std::memory_order_release);
        })), ScheduleResult::Accepted);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!selfShutdownReturned.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        EXPECT_TRUE(selfShutdownReturned.load(std::memory_order_acquire));
        EXPECT_EQ(executor.trySchedule(ExecutorTask([]() {})), ScheduleResult::Stopping);
    }

    TEST(MarlTaskExecutorTests, NestedNativeEventWaitSelfShutdownDoesNotDeadlock)
    {
        marl::Scheduler::Config cfg;
        cfg.workerThread.count = 1;
        marl::Scheduler scheduler(cfg);
        MarlTaskExecutor executor(scheduler);

        marl::Event releaseOuter{marl::Event::Mode::Manual};
        std::atomic<bool> innerShutdownDone{false};
        std::atomic<bool> outerFinished{false};

        ASSERT_EQ(executor.trySchedule(ExecutorTask([&]()
        {
            releaseOuter.wait();
            outerFinished.store(true, std::memory_order_release);
        })), ScheduleResult::Accepted);

        ASSERT_EQ(executor.trySchedule(ExecutorTask([&]()
        {
            releaseOuter.signal();
            executor.shutdown();
            innerShutdownDone.store(true, std::memory_order_release);
        })), ScheduleResult::Accepted);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((!innerShutdownDone.load(std::memory_order_acquire) ||
                !outerFinished.load(std::memory_order_acquire)) &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EXPECT_TRUE(innerShutdownDone.load(std::memory_order_acquire));
        EXPECT_TRUE(outerFinished.load(std::memory_order_acquire));
    }
} // namespace
