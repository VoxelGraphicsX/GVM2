#include <gtest/gtest.h>
#include "generate_result.hpp"

/// Compiles the real DSL entry into a native host descriptor and evaluates it without a GPU device.
TEST(CpuTaskGraphDslTests, NativeHostInvokesGeneratedEntry)
{
    GVM::Core::ThreadExecutor executor(1);
    GVM::Core::TaskRuntime runtime(executor);
    auto execution = runtime.submit(CpuGraphFixture::Entries::buildCpuGraph, CpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(5000000000));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded);
    EXPECT_FLOAT_EQ(execution.get(), 127.5f);
}

/// Ensures graph functions and ordinary CPU class state use the same API in generated DSL host code.
TEST(CpuTaskGraphDslTests, DslHostUsesTheSameSubmissionApi)
{
    CpuGraphFixture::Input input;
    input.scale = 2.0f;
    EXPECT_FLOAT_EQ(CpuGraphFixture::evaluateCpuGraph(input), 255.0f);
}

/// Resolves a deferred record field both as a callable input and as an exported host result.
TEST(CpuTaskGraphDslTests, DeferredFieldsRemainTypedAndOwned)
{
    GVM::Core::TaskRuntime runtime({.workerCount = 1});
    auto calculation = runtime.submit(CpuGraphFixture::Entries::buildProjectedGraph, CpuGraphFixture::Input{});
    ASSERT_TRUE(calculation.wait(5'000'000'000ull));
    ASSERT_EQ(calculation.status(), GVM::Core::ExecutionStatus::Succeeded) << calculation.error().c_str();
    EXPECT_FLOAT_EQ(calculation.get(), 127.5f);
    auto projected = runtime.submit(CpuGraphFixture::Entries::buildProjectedOutput, CpuGraphFixture::Input{});
    ASSERT_TRUE(projected.wait(5'000'000'000ull));
    ASSERT_EQ(projected.status(), GVM::Core::ExecutionStatus::Succeeded) << projected.error().c_str();
    auto values = projected.get();
    ASSERT_EQ(values.size(), 256u);
    EXPECT_FLOAT_EQ(values.back(), 255.0f / 256.0f);
    auto nested = runtime.submit(CpuGraphFixture::Entries::buildNestedProjection, CpuGraphFixture::Input{});
    ASSERT_TRUE(nested.wait(5'000'000'000ull));
    ASSERT_EQ(nested.status(), GVM::Core::ExecutionStatus::Succeeded) << nested.error().c_str();
    EXPECT_FLOAT_EQ(nested.get(), 127.5f);
}

/// Verifies UGL-generated nested [[Export]] subgraphs, explicit TaskGraph& functors, and Task vector fan-in run without deadlock on 1 worker.
TEST(CpuTaskGraphDslTests, NestedDslSubgraphAndTaskVectorCompleteOnSingleWorker)
{
    GVM::Core::ThreadExecutor singleWorker(1);
    GVM::Core::TaskRuntime runtime(singleWorker, {.maximumActiveExecutions = 4, .maximumTasksPerExecution = 512});
    CpuGraphFixture::Input input;
    input.count = 256u;
    input.scale = 2.0f;
    auto execution = runtime.submit(CpuGraphFixture::Entries::buildNestedCpuSubgraph, input);
    ASSERT_TRUE(execution.wait(5'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 510.0f);

    auto rangeForExec = runtime.submit(CpuGraphFixture::Entries::buildRangeForCpuGraph, CpuGraphFixture::Input{});
    ASSERT_TRUE(rangeForExec.wait(5'000'000'000ull));
    ASSERT_EQ(rangeForExec.status(), GVM::Core::ExecutionStatus::Succeeded) << rangeForExec.error().c_str();
    EXPECT_FLOAT_EQ(rangeForExec.get(), 127.5f);
}
