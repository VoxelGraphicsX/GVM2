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
