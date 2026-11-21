#include <GVMCore/Public/MarlTaskExecutor.hpp>
#if defined(GVM_TESTING_MARL_STUB)
#error "gvm_marl_upstream_smoke_tests must compile against upstream google/marl, not marl_stub."
#endif

#include "../../unit/test_marl_executor.cpp"
#include "test_gpu_task_graph_marl.cpp"
#include "generate_result.hpp"

GVM::Core::Execution<float> submitGeneratedGpuGraphForMarlTest(GVM::Core::TaskRuntime &runtime, float scale)
{
    GpuGraphFixture::Input input;
    input.scale = scale;
    return runtime.submit(GpuGraphFixture::Entries::buildGraph, input);
}

namespace
{
    /// Verifies that 4 concurrent fibers running on a 1-worker real google/marl scheduler
    /// suspend cooperatively on Execution::wait() while recording/awaiting GPU graphs and
    /// harvesting cross-frame pass timestamp feedback.
    TEST(MarlUpstreamSmokeTests, SingleWorkerRealMarlFibersSuspendAcrossConcurrentProfiledGpuGraphs)
    {
        eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
            GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
        ASSERT_NE(instance, nullptr);
        auto *device = instance->createDevice();

        marl::Scheduler::Config config;
        config.setWorkerThreadCount(1);
        marl::Scheduler scheduler(config);
        GVM::Core::MarlTaskExecutor executor(scheduler);
        GVM::Core::TaskRuntime runtime(*device, executor, {.enablePassTimestampFeedback = true});

        const auto runWave = [&](uint32_t iterations)
        {
            return runtime.submit([&runtime](uint32_t iterCount)
            {
                GVM::Core::TaskGraph graph;
                auto f0 = graph.add([&runtime](uint32_t iters) -> float
                {
                    auto exec = runtime.submit(GpuGraphFixture::Entries::buildProfiledGraph, iters);
                    EXPECT_TRUE(exec.wait(10'000'000'000ull));
                    return exec.get();
                }, iterCount);
                auto f1 = graph.add([&runtime](uint32_t iters) -> float
                {
                    auto exec = runtime.submit(GpuGraphFixture::Entries::buildProfiledGraph, iters);
                    EXPECT_TRUE(exec.wait(10'000'000'000ull));
                    return exec.get();
                }, iterCount);
                auto f2 = graph.add([&runtime](uint32_t iters) -> float
                {
                    auto exec = runtime.submit(GpuGraphFixture::Entries::buildProfiledGraph, iters);
                    EXPECT_TRUE(exec.wait(10'000'000'000ull));
                    return exec.get();
                }, iterCount);
                auto f3 = graph.add([&runtime](uint32_t iters) -> float
                {
                    auto exec = runtime.submit(GpuGraphFixture::Entries::buildProfiledGraph, iters);
                    EXPECT_TRUE(exec.wait(10'000'000'000ull));
                    return exec.get();
                }, iterCount);
                return graph.add([](float a, float b, float c, float d)
                {
                    return (a + b + c + d) * 0.25f;
                }, f0, f1, f2, f3);
            }, iterations);
        };

        auto wave0 = runWave(2048u);
        ASSERT_TRUE(wave0.wait(15'000'000'000ull));
        ASSERT_EQ(wave0.status(), GVM::Core::ExecutionStatus::Succeeded) << wave0.error().c_str();
        const float expected = wave0.get();
        EXPECT_GT(expected, 0.0f);

        auto wave1 = runWave(2048u);
        ASSERT_TRUE(wave1.wait(15'000'000'000ull));
        ASSERT_EQ(wave1.status(), GVM::Core::ExecutionStatus::Succeeded) << wave1.error().c_str();
        EXPECT_FLOAT_EQ(wave1.get(), expected);

        if (device->getMainQueue()->getTimestampQuerySupport().supported)
        {
            const auto snapshot = runtime.passProfileSnapshot();
            EXPECT_FALSE(snapshot.empty());
        }
    }

    /// Sums a dynamic vector of scalar branch results produced by nested GPU subgraphs.
    float sumBranchOutputs(eastl::vector<float> values)
    {
        float total = 0.0f;
        for (float v : values)
        {
            total += v;
        }
        return total;
    }

    /// Verifies native continuation-based nested GPU subgraphs and dynamic Task vector fan-in
    /// complete without blocking on a 1-worker upstream google/marl scheduler.
    TEST(MarlUpstreamSmokeTests, SingleWorkerRealMarlCompletesContinuationNestedGpuSubgraphs)
    {
        eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
            GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
        ASSERT_NE(instance, nullptr);
        auto *device = instance->createDevice();

        marl::Scheduler::Config config;
        config.setWorkerThreadCount(1);
        marl::Scheduler scheduler(config);
        GVM::Core::MarlTaskExecutor executor(scheduler);
        GVM::Core::TaskRuntime runtime(*device, executor);

        auto execution = runtime.submit([](uint32_t branchCount)
        {
            GVM::Core::TaskGraph root;
            eastl::vector<GVM::Core::Task<float>> branches;
            branches.reserve(branchCount);
            for (uint32_t i = 0; i < branchCount; ++i)
            {
                GpuGraphFixture::Input input;
                input.scale = static_cast<float>(i + 1u);
                branches.push_back(root.add(GpuGraphFixture::Entries::buildParentNestedGpuGraph, input));
            }
            return root.add(sumBranchOutputs, branches);
        }, 3u);

        ASSERT_TRUE(execution.wait(15'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        // Each branch i computes 127.5f * (scale + 1) + 64.0f for scales 1, 2, 3 -> 319 + 446.5 + 574 = 1339.5.
        EXPECT_FLOAT_EQ(execution.get(), 1339.5f);
    }

    /// Verifies out-of-order resumption of real upstream marl fibers blocked on native marl::Events
    /// preserves per-fiber active-slot tracking across suspension and worker migration when self-shutting down.
    TEST(MarlUpstreamSmokeTests, OutOfOrderNativeMarlEventResumeDoesNotDeadlockSelfShutdown)
    {
        for (int workerCount : {1, 2})
        {
            marl::Scheduler::Config config;
            config.setWorkerThreadCount(workerCount);
            marl::Scheduler scheduler(config);
            GVM::Core::MarlTaskExecutor executor(scheduler);

            marl::Event taskAEntered{marl::Event::Mode::Manual};
            marl::Event taskBEntered{marl::Event::Mode::Manual};
            marl::Event releaseTaskA{marl::Event::Mode::Manual};
            marl::Event releaseTaskB{marl::Event::Mode::Manual};
            std::atomic<bool> taskAFinished{false};
            std::atomic<bool> taskBShutdownFinished{false};

            ASSERT_EQ(executor.trySchedule(GVM::Core::ExecutorTask([&]()
            {
                taskAEntered.signal();
                releaseTaskA.wait();
                taskAFinished.store(true, std::memory_order_release);
            })), GVM::Core::ScheduleResult::Accepted);

            ASSERT_EQ(executor.trySchedule(GVM::Core::ExecutorTask([&]()
            {
                taskBEntered.signal();
                releaseTaskB.wait();
                executor.shutdown();
                taskBShutdownFinished.store(true, std::memory_order_release);
            })), GVM::Core::ScheduleResult::Accepted);

            const auto waitEvent = [](marl::Event &ev)
            {
                const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!ev.test() && std::chrono::steady_clock::now() < limit)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return ev.test();
            };
            ASSERT_TRUE(waitEvent(taskAEntered));
            ASSERT_TRUE(waitEvent(taskBEntered));

            releaseTaskA.signal();
            const auto deadlineA = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!taskAFinished.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < deadlineA)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT_TRUE(taskAFinished.load(std::memory_order_acquire));

            releaseTaskB.signal();
            const auto deadlineB = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!taskBShutdownFinished.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < deadlineB)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            EXPECT_TRUE(taskBShutdownFinished.load(std::memory_order_acquire));
        }
    }
} // namespace
