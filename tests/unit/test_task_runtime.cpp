#include <gtest/gtest.h>
#include <GVMCore/Public/TaskRuntime.hpp>
#include "TaskTestExecutor.hpp"

#include <EASTL/optional.h>
#include <atomic>
#include <chrono>
#include <semaphore>
#include <stdexcept>

namespace
{
    using namespace GVM::Core;

    /// Generates the CPU-only design fixture without a device or renderer.
    eastl::vector<float> generateSamples(uint32_t count)
    {
        eastl::vector<float> result(count);
        for (uint32_t index = 0; index < count; ++index)
        {
            result[index] = static_cast<float>(index) / 256.0f;
        }
        return result;
    }

    /// Reduces an owned CPU value in a stable order.
    float sumSamples(eastl::vector<float> values)
    {
        float sum = 0;
        for (float value : values)
        {
            sum += value;
        }
        return sum;
    }

    /// Returns an output whose local builder is destroyed before execution starts.
    Task<float> buildSamples(uint32_t count)
    {
        TaskGraph graph;
        return graph.add(sumSamples, graph.add(generateSamples, count));
    }

    /// Squares a CPU input for a fork/join dependency fixture.
    int square(int input)
    {
        return input * input;
    }

    /// Adds two independently resolved input values.
    int add(int left, int right)
    {
        return left + right;
    }

    /// Builds two independent branches and one join without waiting inside a worker.
    Task<int> buildForkJoin(int input)
    {
        TaskGraph graph;
        auto left = graph.add(square, input);
        auto right = graph.add(square, input + 1);
        return graph.add(add, left, right);
    }

    /// Holds deterministic concurrency gates owned by every participating callback.
    struct RecordingGate
    {
        std::counting_semaphore<2> entered{0};
        std::counting_semaphore<2> release{0};
    };

    /// Exercises the default implementation without initializing GPU infrastructure.
    TEST(TaskRuntimeTests, DefaultExecutorProducesCpuReference)
    {
        TaskRuntime runtime({.workerCount = 1});
        auto execution = runtime.submit(buildSamples, 256u);
        ASSERT_TRUE(execution.wait(5000000000));
        EXPECT_EQ(execution.status(), ExecutionStatus::Succeeded);
        EXPECT_FLOAT_EQ(execution.get(), 127.5f);
    }

    /// Verifies a fork/join and repeated dependencies complete with one ordinary worker.
    TEST(TaskRuntimeTests, SingleWorkerDoesNotWaitOnChildren)
    {
        ThreadExecutor executor(1);
        TaskRuntime runtime(executor);
        auto execution = runtime.submit(buildForkJoin, 3);
        ASSERT_TRUE(execution.wait(5000000000));
        EXPECT_EQ(execution.get(), 25);
        auto repeated = runtime.submit([](int input)
        {
            TaskGraph graph;
            auto producer = graph.add(square, input);
            return graph.add(add, producer, producer);
        }, 4);
        ASSERT_TRUE(repeated.wait(5000000000));
        EXPECT_EQ(repeated.get(), 32);
    }

    /// Ensures acceptance does not call a builder inline and get never silently blocks.
    TEST(TaskRuntimeTests, SubmissionIsDeferredAndGetIsNonblocking)
    {
        TaskTestExecutor executor;
        TaskRuntime runtime(executor);
        auto execution = runtime.submit(buildForkJoin, 3);
        EXPECT_EQ(executor.pending(), 1u);
        EXPECT_FALSE(execution.ready());
        EXPECT_FALSE(execution.wait(0));
        EXPECT_THROW(execution.get(), std::logic_error);
        executor.drain();
        EXPECT_EQ(execution.get(), 25);
    }

    /// Checks independent ready operations actually overlap on the injected worker pool.
    TEST(TaskRuntimeTests, IndependentBranchesOverlap)
    {
        auto gate = eastl::make_shared<RecordingGate>();
        ThreadExecutor executor(2);
        TaskRuntime runtime(executor);
        auto execution = runtime.submit([gate](int input)
        {
            TaskGraph graph;
            auto operation = [gate](int value)
            {
                gate->entered.release();
                gate->release.acquire();
                return value;
            };
            auto first = graph.add(operation, input);
            auto second = graph.add(operation, input + 1);
            return graph.add(add, first, second);
        }, 8);
        const bool firstEntered = gate->entered.try_acquire_for(std::chrono::seconds(2));
        const bool secondEntered = gate->entered.try_acquire_for(std::chrono::seconds(2));
        gate->release.release(2);
        EXPECT_TRUE(firstEntered);
        EXPECT_TRUE(secondEntered);
        ASSERT_TRUE(execution.wait(5000000000));
        EXPECT_EQ(execution.get(), 17);
    }

    /// Mutating the caller's container after submit must not change the accepted snapshot.
    TEST(TaskRuntimeTests, CapturesOwnedInputAndPerBranchCopies)
    {
        TaskTestExecutor executor;
        TaskRuntime runtime(executor);
        eastl::vector<float> input{1, 2, 3};
        auto execution = runtime.submit([](eastl::vector<float> values)
        {
            TaskGraph graph;
            auto original = graph.add([](eastl::vector<float> data) { return data; }, values);
            auto changed = graph.add([](eastl::vector<float> data)
            {
                data[0] = 100;
                return data;
            }, original);
            auto sum = graph.add(sumSamples, original);
            return graph.add([](float value, eastl::vector<float> modified)
            {
                return value + modified[0];
            }, sum, changed);
        }, input);
        input[0] = 999;
        input.clear();
        executor.drain();
        EXPECT_FLOAT_EQ(execution.get(), 106.0f);
    }

    /// Cancellation before preparation skips the builder but still drains its accepted callback.
    TEST(TaskRuntimeTests, CancellationDrainsAcceptedPreparation)
    {
        TaskTestExecutor executor;
        TaskRuntime runtime(executor);
        auto calls = eastl::make_shared<int>(0);
        auto execution = runtime.submit([calls](int input)
        {
            ++*calls;
            return buildForkJoin(input);
        }, 3);
        execution.cancel();
        EXPECT_FALSE(execution.ready());
        executor.drain();
        EXPECT_EQ(*calls, 0);
        EXPECT_EQ(execution.status(), ExecutionStatus::Cancelled);
        EXPECT_THROW(execution.get(), std::runtime_error);
    }

    /// Queued branches must be accounted without running their operations after cancellation.
    TEST(TaskRuntimeTests, CancellationSkipsUnstartedBranches)
    {
        TaskTestExecutor executor(2);
        TaskRuntime runtime(executor);
        auto execution = runtime.submit(buildForkJoin, 3);
        EXPECT_TRUE(executor.executeNext());
        EXPECT_EQ(executor.pending(), 2u);
        execution.cancel();
        EXPECT_FALSE(execution.ready());
        executor.drain();
        EXPECT_TRUE(execution.ready());
        EXPECT_EQ(execution.status(), ExecutionStatus::Cancelled);
    }

    /// Terminal handles must retain their result, not the cancelled graph's captured input storage.
    TEST(TaskRuntimeTests, CancellationReleasesUnusedCaptures)
    {
        TaskTestExecutor executor;
        TaskRuntime runtime(executor);
        auto owner = eastl::make_shared<int>(7);
        eastl::weak_ptr<int> observer = owner;
        auto execution = runtime.submit([owner](int input)
        {
            TaskGraph graph;
            return graph.add([owner](int value)
            {
                return value + *owner;
            }, input);
        }, 3);
        owner.reset();
        EXPECT_TRUE(executor.executeNext());
        execution.cancel();
        executor.drain();
        EXPECT_EQ(execution.status(), ExecutionStatus::Cancelled);
        EXPECT_TRUE(observer.expired());
    }

    /// A terminal wait must include capture cleanup and return the admission slot before another submit.
    TEST(TaskRuntimeTests, TerminalPublicationFollowsCleanupAndAdmissionRelease)
    {
        auto operationGate = eastl::make_shared<RecordingGate>();
        auto cleanupGate = eastl::make_shared<RecordingGate>();
        auto owner = eastl::shared_ptr<int>(new int(7), [cleanupGate](int *value)
        {
            cleanupGate->entered.release();
            cleanupGate->release.acquire();
            delete value;
        });
        TaskRuntime runtime({.maximumActiveExecutions = 1, .workerCount = 1});
        auto execution = runtime.submit([owner, operationGate](int input)
        {
            TaskGraph graph;
            graph.add([operationGate](int value)
            {
                operationGate->entered.release();
                operationGate->release.acquire();
                return value;
            }, input);
            return graph.add([owner](int value)
            {
                return value + *owner;
            }, input);
        }, 3);
        owner.reset();
        const bool operationEntered = operationGate->entered.try_acquire_for(std::chrono::seconds(2));
        execution.cancel();
        operationGate->release.release();
        const bool cleanupEntered = cleanupGate->entered.try_acquire_for(std::chrono::seconds(2));
        EXPECT_TRUE(operationEntered);
        EXPECT_TRUE(cleanupEntered);
        EXPECT_FALSE(execution.ready());
        EXPECT_FALSE(execution.wait(0));
        cleanupGate->release.release();
        ASSERT_TRUE(execution.wait(5000000000));
        EXPECT_EQ(execution.status(), ExecutionStatus::Cancelled);
        auto next = runtime.submit(buildForkJoin, 3);
        ASSERT_TRUE(next.wait(5000000000));
        EXPECT_EQ(next.get(), 25);
    }

    /// Exceptions terminate successors and retain the original error type for get.
    TEST(TaskRuntimeTests, FailureDoesNotExecuteSuccessorOrPoisonRuntime)
    {
        TaskTestExecutor executor(2);
        TaskRuntime runtime(executor);
        auto calls = eastl::make_shared<int>(0);
        auto execution = runtime.submit([calls](int input)
        {
            TaskGraph graph;
            auto failure = graph.add([](int) -> int
            {
                throw std::domain_error("invalid sample");
            }, input);
            return graph.add([calls](int value)
            {
                ++*calls;
                return value;
            }, failure);
        }, 1);
        executor.drain();
        EXPECT_EQ(*calls, 0);
        EXPECT_EQ(execution.status(), ExecutionStatus::Failed);
        EXPECT_EQ(execution.error(), "invalid sample");
        EXPECT_THROW(execution.get(), std::domain_error);
        auto next = runtime.submit(buildForkJoin, 3);
        executor.drain();
        EXPECT_EQ(next.get(), 25);
    }

    /// Executor rejection before preparation returns a terminal rejected execution.
    TEST(TaskRuntimeTests, ExecutorRejectionAlwaysTerminates)
    {
        TaskTestExecutor executor;
        executor.rejectFrom(0);
        TaskRuntime runtime(executor);
        auto execution = runtime.submit(buildForkJoin, 3);
        EXPECT_TRUE(execution.ready());
        EXPECT_EQ(execution.status(), ExecutionStatus::Rejected);
        EXPECT_EQ(executor.pending(), 0u);
        EXPECT_THROW(execution.get(), std::runtime_error);
    }

    /// Rejection after graph acceptance must drain already accepted siblings without a stuck join.
    TEST(TaskRuntimeTests, PartialDispatchRejectionDrainsAcceptedWork)
    {
        TaskTestExecutor executor(2);
        executor.rejectFrom(2);
        TaskRuntime runtime(executor);
        auto execution = runtime.submit(buildForkJoin, 3);
        EXPECT_TRUE(executor.executeNext());
        EXPECT_FALSE(execution.ready());
        executor.drain();
        EXPECT_EQ(execution.status(), ExecutionStatus::Failed);
        EXPECT_FALSE(execution.error().empty());
    }

    /// Admission is bounded and a terminal execution releases its capacity for the next caller.
    TEST(TaskRuntimeTests, AdmissionBudgetIsReleased)
    {
        TaskTestExecutor executor;
        TaskRuntime runtime(executor, {.maximumActiveExecutions = 1});
        auto accepted = runtime.submit(buildForkJoin, 3);
        auto rejected = runtime.submit(buildForkJoin, 4);
        EXPECT_EQ(rejected.status(), ExecutionStatus::Rejected);
        executor.drain();
        EXPECT_EQ(accepted.get(), 25);
        auto next = runtime.submit(buildForkJoin, 4);
        executor.drain();
        EXPECT_EQ(next.get(), 41);
    }

    /// Oversized and foreign plans fail before executing any graph operation.
    TEST(TaskRuntimeTests, RejectsInvalidAndOverBudgetPlans)
    {
        TaskTestExecutor executor;
        TaskRuntime runtime(executor, {.maximumTasksPerExecution = 1});
        auto execution = runtime.submit(buildForkJoin, 3);
        executor.drain();
        EXPECT_EQ(execution.status(), ExecutionStatus::Failed);
        EXPECT_THROW(execution.get(), std::length_error);
        TaskGraph first;
        TaskGraph second;
        auto producer = first.add(square, 3);
        EXPECT_THROW(second.add(square, producer), std::invalid_argument);
        Task<int> unbound;
        EXPECT_THROW(first.add(square, unbound), std::invalid_argument);
    }

    /// Closing one runtime must not destroy or drain unrelated jobs in a shared executor.
    TEST(TaskRuntimeTests, BorrowedExecutorRemainsUsable)
    {
        TaskTestExecutor executor;
        {
            TaskRuntime runtime(executor);
            auto execution = runtime.submit(buildForkJoin, 3);
            executor.drain();
            EXPECT_EQ(execution.get(), 25);
        }
        bool completed = false;
        EXPECT_EQ(executor.trySchedule(ExecutorTask([&completed] { completed = true; })), ScheduleResult::Accepted);
        EXPECT_FALSE(completed);
        executor.drain();
        EXPECT_TRUE(completed);
    }

    /// A successful output remains owned after its runtime and builder are destroyed.
    TEST(TaskRuntimeTests, ResultsOutliveRuntime)
    {
        eastl::optional<Execution<float>> result;
        {
            TaskRuntime runtime({.workerCount = 1});
            result = runtime.submit(buildSamples, 256u);
        }
        EXPECT_TRUE(result->ready());
        EXPECT_FLOAT_EQ(result->get(), 127.5f);
    }

    /// Void outputs complete normally and require no artificial result allocation.
    TEST(TaskRuntimeTests, SupportsVoidOutput)
    {
        TaskRuntime runtime({.workerCount = 1});
        auto execution = runtime.submit([](int value)
        {
            TaskGraph graph;
            return graph.add([](int) {}, value);
        }, 1);
        ASSERT_TRUE(execution.wait(5000000000));
        EXPECT_NO_THROW(execution.get());
    }
}
