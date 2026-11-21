#include <GVMCore/Public/MarlTaskExecutor.hpp>
#include <GVMCore/GVMCore.Public.hpp>
#include <gtest/gtest.h>
#include <GVMTestCommon.hpp>

GVM::Core::Execution<float> submitGeneratedGpuGraphForMarlTest(GVM::Core::TaskRuntime &runtime, float scale);

namespace
{
    /// Runs a mixed CPU + parallel GPU recording + async GPU readback graph on MarlTaskExecutor,
    /// including a nested GPU Execution::wait() inside a single-worker marl fiber without deadlocking.
    TEST(GpuTaskGraphMarlTests, SingleWorkerMarlFiberCompletesNestedGpuTaskGraphWithoutDeadlock)
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

        auto outer = runtime.submit([&runtime](float scale)
        {
            GVM::Core::TaskGraph graph;
            return graph.add([&runtime](float innerScale) -> float
            {
                auto inner = submitGeneratedGpuGraphForMarlTest(runtime, innerScale);
                EXPECT_TRUE(inner.wait(10'000'000'000ull));
                return inner.get();
            }, scale);
        }, 2.0f);

        ASSERT_TRUE(outer.wait(10'000'000'000ull));
        ASSERT_EQ(outer.status(), GVM::Core::ExecutionStatus::Succeeded) << outer.error().c_str();
        EXPECT_FLOAT_EQ(outer.get(), 446.5f);
    }

    /// Runs concurrent mixed CPU/GPU executions across multiple marl workers and drains cleanly on runtime teardown.
    TEST(GpuTaskGraphMarlTests, MultiWorkerMarlSchedulerExecutesConcurrentGpuGraphs)
    {
        eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
            GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
        ASSERT_NE(instance, nullptr);
        auto *device = instance->createDevice();

        auto config = marl::Scheduler::Config::allCores().setWorkerThreadCount(3);
        marl::Scheduler scheduler(config);
        GVM::Core::MarlTaskExecutor executor(scheduler);
        {
            GVM::Core::TaskRuntime runtime(*device, executor);
            eastl::vector<GVM::Core::Execution<float>> executions;
            for (uint32_t index = 0; index < 4; ++index)
            {
                executions.push_back(submitGeneratedGpuGraphForMarlTest(runtime, float(index)));
            }
            for (uint32_t index = 0; index < executions.size(); ++index)
            {
                ASSERT_TRUE(executions[index].wait(10'000'000'000ull));
                ASSERT_EQ(executions[index].status(), GVM::Core::ExecutionStatus::Succeeded)
                    << executions[index].error().c_str();
                EXPECT_FLOAT_EQ(executions[index].get(), 127.5f * float(index + 1) + 64.0f);
            }
        }
    }
} // namespace
