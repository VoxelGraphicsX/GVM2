#include <gtest/gtest.h>
#include <GVMCore/Public/TaskRuntime.hpp>
#include "TaskTestExecutor.hpp"

#include <EASTL/optional.h>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
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

    /// Verifies ExecutorTask handles both inline SBO captures and oversized heap-allocated captures cleanly.
    TEST(TaskRuntimeTests, ExecutorTaskSupportsInlineAndHeapFallbackStorage)
    {
        std::atomic<int> sum{0};
        // Small capture fits inside inline 96B SBO.
        ExecutorTask small([&sum]() { sum.fetch_add(7, std::memory_order_relaxed); });
        EXPECT_TRUE(static_cast<bool>(small));
        ExecutorTask movedSmall(std::move(small));
        EXPECT_FALSE(static_cast<bool>(small));
        movedSmall.execute();
        EXPECT_FALSE(static_cast<bool>(movedSmall));

        // Large non-inline capture (> 96 bytes) falls back to heap and releases cleanly on move/execute/destroy.
        struct LargeCapture
        {
            std::array<uint64_t, 32> payload{};
            std::atomic<int> *target = nullptr;
            std::shared_ptr<int> lifetime;
        };
        auto token = std::make_shared<int>(42);
        std::weak_ptr<int> weakToken = token;
        {
            LargeCapture capture;
            capture.payload[0] = 11;
            capture.payload[31] = 22;
            capture.target = &sum;
            capture.lifetime = std::move(token);
            ExecutorTask large([c = std::move(capture)]()
            {
                c.target->fetch_add(static_cast<int>(c.payload[0] + c.payload[31]), std::memory_order_relaxed);
            });
            ExecutorTask movedLarge;
            movedLarge = std::move(large);
            EXPECT_FALSE(static_cast<bool>(large));
            EXPECT_TRUE(static_cast<bool>(movedLarge));
            movedLarge.execute();
            EXPECT_FALSE(static_cast<bool>(movedLarge));
        }
        EXPECT_TRUE(weakToken.expired());
        EXPECT_EQ(sum.load(), 40);
    }

    /// Verifies default ThreadExecutor handles large task bursts (>4096) without queue overflow rejection.
    TEST(TaskRuntimeTests, ThreadExecutorHandlesLargeTaskBurstAndStealing)
    {
        constexpr uint32_t kBurstCount = 8192;
        ThreadExecutor executor(4);
        std::atomic<uint32_t> completed{0};
        std::mutex doneMutex;
        std::condition_variable doneCv;

        eastl::vector<ExecutorTask> batch;
        batch.reserve(kBurstCount);
        for (uint32_t i = 0; i < kBurstCount; ++i)
        {
            batch.emplace_back([&completed, &doneMutex, &doneCv]()
            {
                if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 == kBurstCount)
                {
                    std::lock_guard lock(doneMutex);
                    doneCv.notify_all();
                }
            });
        }
        const size_t accepted = executor.scheduleBatch(batch.data(), batch.size());
        EXPECT_EQ(accepted, kBurstCount);

        std::unique_lock lock(doneMutex);
        EXPECT_TRUE(doneCv.wait_for(lock, std::chrono::seconds(10), [&]
        {
            return completed.load(std::memory_order_acquire) == kBurstCount;
        }));
    }

    /// Verifies a large CPU fan-out DAG (>4096 tasks) completes smoothly on default ThreadExecutor.
    TEST(TaskRuntimeTests, LargeFanOutDagExceedingOldQueueCapSucceeds)
    {
        constexpr uint32_t kBranches = 5000;
        TaskRuntime runtime({.maximumTasksPerExecution = 16384, .workerCount = 4});
        auto execution = runtime.submit([](uint32_t count)
        {
            TaskGraph graph;
            eastl::vector<Task<int>> nodes;
            nodes.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                nodes.push_back(graph.add([](uint32_t v) { return static_cast<int>(v); }, i));
            }
            auto prev = nodes[0];
            for (uint32_t i = 1; i < count; ++i)
            {
                prev = graph.add(add, prev, nodes[i]);
            }
            return prev;
        }, kBranches);
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), ExecutionStatus::Succeeded) << execution.error().c_str();
        const int expected = static_cast<int>((kBranches * (kBranches - 1)) / 2);
        EXPECT_EQ(execution.get(), expected);
    }

    namespace
    {
        struct RecursiveTreeSumSubgraph
        {
            Task<int> operator()(TaskGraph &sub, int depth, int startValue, int span) const
            {
                if (span <= 0)
                {
                    eastl::vector<Task<int>> emptyChildren;
                    return sub.add([](eastl::vector<int> vals)
                    {
                        return static_cast<int>(vals.size());
                    }, std::move(emptyChildren));
                }
                if (depth <= 0 || span == 1)
                {
                    return sub.add([startValue, span]()
                    {
                        int acc = 0;
                        for (int i = 0; i < span; ++i)
                        {
                            acc += (startValue + i);
                        }
                        return acc;
                    });
                }

                const int mid = span / 2;
                eastl::vector<Task<int>> children;
                children.push_back(sub.add(RecursiveTreeSumSubgraph{}, depth - 1, startValue, mid));
                children.push_back(sub.add(RecursiveTreeSumSubgraph{}, depth - 1, startValue + mid, span - mid));
                return sub.add([](eastl::vector<int> parts)
                {
                    int total = 0;
                    for (int v : parts)
                    {
                        total += v;
                    }
                    return total;
                }, std::move(children));
            }
        };
    } // namespace

    /// Verifies multi-level recursive nested TaskGraphs + eastl::vector<Task<T>> fan-in complete without deadlock on a 1-worker ThreadExecutor.
    TEST(TaskRuntimeTests, NestedTaskGraphAndTaskVectorCompleteWithoutDeadlockOnSingleWorker)
    {
        ThreadExecutor singleWorker(1);
        TaskRuntime runtime(singleWorker, {.maximumActiveExecutions = 4, .maximumTasksPerExecution = 1024});

        auto execution = runtime.submit([](int count)
        {
            TaskGraph root;
            auto sumPart = root.add(RecursiveTreeSumSubgraph{}, 4, 1, count);
            auto emptyPart = root.add(RecursiveTreeSumSubgraph{}, 2, 0, 0);
            return root.add(add, sumPart, emptyPart);
        }, 32);

        ASSERT_TRUE(execution.wait(5'000'000'000ull));
        ASSERT_EQ(execution.status(), ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_EQ(execution.get(), (32 * 33) / 2);
    }

    /// Verifies parent + nested child subgraphs share maximumTasksPerExecution and propagate nested child exceptions cleanly.
    TEST(TaskRuntimeTests, NestedTaskGraphSharesTaskBudgetAndPropagatesExceptions)
    {
        ThreadExecutor executor(2);
        TaskRuntime tightRuntime(executor, {.maximumActiveExecutions = 4, .maximumTasksPerExecution = 6});

        auto overBudget = tightRuntime.submit([](int count)
        {
            TaskGraph root;
            return root.add(RecursiveTreeSumSubgraph{}, 4, 1, count);
        }, 16);
        ASSERT_TRUE(overBudget.wait(5'000'000'000ull));
        EXPECT_EQ(overBudget.status(), ExecutionStatus::Failed);
        EXPECT_THROW(overBudget.get(), std::length_error);

        TaskRuntime normalRuntime(executor, {.maximumActiveExecutions = 4, .maximumTasksPerExecution = 256});
        auto failingNested = normalRuntime.submit([](int x)
        {
            TaskGraph root;
            return root.add([](TaskGraph &sub, int v)
            {
                return sub.add([](int inner) -> int
                {
                    if (inner < 0)
                    {
                        throw std::domain_error("nested domain failure");
                    }
                    return inner;
                }, v);
            }, x);
        }, -5);
        ASSERT_TRUE(failingNested.wait(5'000'000'000ull));
        EXPECT_EQ(failingNested.status(), ExecutionStatus::Failed);
        EXPECT_THROW(failingNested.get(), std::domain_error);
    }

    struct TestBlockingGate
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false;
        bool released = false;
    };

    /// Verifies Execution::cancel() cascades into active nested child subgraphs and settles cleanly.
    TEST(TaskRuntimeTests, NestedTaskGraphCascadesCancellationToChildren)
    {
        ThreadExecutor executor(2);
        TaskRuntime runtime(executor, {.maximumActiveExecutions = 4, .maximumTasksPerExecution = 256});

        auto gate = std::make_shared<TestBlockingGate>();
        std::atomic<bool> tailExecuted{false};

        auto execution = runtime.submit([gate, &tailExecuted](int seed)
        {
            TaskGraph root;
            auto childOut = root.add([gate, &tailExecuted](TaskGraph &sub, int v)
            {
                auto first = sub.add([gate, v]()
                {
                    {
                        std::lock_guard lock(gate->mutex);
                        gate->entered = true;
                    }
                    gate->cv.notify_all();
                    std::unique_lock lock(gate->mutex);
                    gate->cv.wait(lock, [&gate] { return gate->released; });
                    return v + 1;
                });
                return sub.add([&tailExecuted](int next)
                {
                    tailExecuted.store(true, std::memory_order_release);
                    return next + 1;
                }, first);
            }, seed);
            return root.add([](int finalVal) { return finalVal * 2; }, childOut);
        }, 10);

        {
            std::unique_lock lock(gate->mutex);
            ASSERT_TRUE(gate->cv.wait_for(lock, std::chrono::seconds(3), [&gate] { return gate->entered; }));
        }
        execution.cancel();
        {
            std::lock_guard lock(gate->mutex);
            gate->released = true;
        }
        gate->cv.notify_all();

        ASSERT_TRUE(execution.wait(5'000'000'000ull));
        EXPECT_EQ(execution.status(), ExecutionStatus::Cancelled);
        EXPECT_FALSE(tailExecuted.load(std::memory_order_acquire));
    }

    /// Verifies non-worker ThreadExecutor::scheduleBatch consumes a strict contiguous prefix [0 .. accepted - 1] when hitting maxQueuedTasks.
    TEST(TaskRuntimeTests, ThreadExecutorScheduleBatchConsumesStrictContiguousPrefix)
    {
        ThreadExecutor bounded(2, /*maxQueuedTasks=*/4);
        auto blocker = std::make_shared<TestBlockingGate>();
        std::atomic<uint32_t> workersBlocked{0};

        // Block both worker threads so queued tasks remain in worker deques until released.
        for (uint32_t w = 0; w < 2; ++w)
        {
            ASSERT_EQ(bounded.trySchedule(ExecutorTask([blocker, &workersBlocked]()
            {
                if (workersBlocked.fetch_add(1, std::memory_order_acq_rel) + 1 == 2)
                {
                    std::lock_guard lock(blocker->mutex);
                    blocker->entered = true;
                    blocker->cv.notify_all();
                }
                std::unique_lock lock(blocker->mutex);
                blocker->cv.wait(lock, [&blocker] { return blocker->released; });
            })), ScheduleResult::Accepted);
        }
        {
            std::unique_lock lock(blocker->mutex);
            ASSERT_TRUE(blocker->cv.wait_for(lock, std::chrono::seconds(3), [&blocker] { return blocker->entered; }));
        }

        // Pre-fill 1 queued slot -> 3 slots remain out of 4.
        std::atomic<uint32_t> ranMask{0};
        ASSERT_EQ(bounded.trySchedule(ExecutorTask([&ranMask]()
        {
            ranMask.fetch_or(1u << 31u, std::memory_order_relaxed);
        })), ScheduleResult::Accepted);

        eastl::vector<ExecutorTask> batch;
        for (uint32_t i = 0; i < 6; ++i)
        {
            batch.emplace_back([&ranMask, i]()
            {
                ranMask.fetch_or(1u << i, std::memory_order_relaxed);
            });
        }

        const size_t accepted = bounded.scheduleBatch(batch.data(), batch.size());
        EXPECT_EQ(accepted, 3u);
        for (size_t i = 0; i < batch.size(); ++i)
        {
            if (i < accepted)
            {
                EXPECT_FALSE(static_cast<bool>(batch[i])) << "Expected consumed slot at index " << i;
            }
            else
            {
                EXPECT_TRUE(static_cast<bool>(batch[i])) << "Expected untouched unaccepted slot at index " << i;
            }
        }

        {
            std::lock_guard lock(blocker->mutex);
            blocker->released = true;
        }
        blocker->cv.notify_all();
    }

    /// Recursively nests a linear chain of subgraphs to exercise iterative stack-bounded completion and cancellation.
    struct DeepLinearSubgraphChain
    {
        std::shared_ptr<TestBlockingGate> leafGate;

        /// Builds depth levels of nested subgraphs without branching.
        Task<int> operator()(TaskGraph &sub, int remainingDepth, int acc) const
        {
            if (remainingDepth <= 0)
            {
                auto gate = leafGate;
                return sub.add([gate, acc]()
                {
                    if (gate)
                    {
                        {
                            std::lock_guard lock(gate->mutex);
                            gate->entered = true;
                        }
                        gate->cv.notify_all();
                        std::unique_lock lock(gate->mutex);
                        gate->cv.wait(lock, [&gate] { return gate->released; });
                    }
                    return acc + 1;
                });
            }
            DeepLinearSubgraphChain next{leafGate};
            return sub.add(next, remainingDepth - 1, acc + 1);
        }
    };

    /// Repeatedly races multi-worker child subgraph completion against startChild() progress verification.
    TEST(TaskRuntimeTests, MultiWorkerNestedSubgraphDoesNotFalselyFailProgressCheck)
    {
        ThreadExecutor executor(8);
        TaskRuntime runtime(executor, {.maximumActiveExecutions = 16, .maximumTasksPerExecution = 256});

        for (int iter = 0; iter < 150; ++iter)
        {
            auto execution = runtime.submit([](int seed)
            {
                TaskGraph root;
                return root.add([](TaskGraph &sub, int base)
                {
                    auto step1 = sub.add([](int v) { return v + 3; }, base);
                    return sub.add([](int v) { return v * 2; }, step1);
                }, seed);
            }, iter);
            ASSERT_TRUE(execution.wait(5'000'000'000ull));
            ASSERT_EQ(execution.status(), ExecutionStatus::Succeeded) << execution.error().c_str();
            EXPECT_EQ(execution.get(), (iter + 3) * 2);
        }
    }

    /// Enforces maximumInFlightTasksPerExecution across all parallel sibling subgraphs of one root execution.
    TEST(TaskRuntimeTests, NestedSubgraphsShareRootMaximumInFlightTasksLimit)
    {
        ThreadExecutor executor(4);
        TaskRuntime runtime(executor, {
            .maximumActiveExecutions = 4,
            .maximumTasksPerExecution = 256,
            .maximumInFlightTasksPerExecution = 1,
        });

        std::atomic<uint32_t> activeTasks{0};
        std::atomic<uint32_t> peakConcurrentTasks{0};

        auto execution = runtime.submit([&activeTasks, &peakConcurrentTasks](int seed)
        {
            TaskGraph root;
            eastl::vector<Task<int>> branches;
            for (int b = 0; b < 4; ++b)
            {
                branches.push_back(root.add([&activeTasks, &peakConcurrentTasks](TaskGraph &sub, int branchSeed)
                {
                    auto first = sub.add([&activeTasks, &peakConcurrentTasks](int x)
                    {
                        const uint32_t current = activeTasks.fetch_add(1u, std::memory_order_acq_rel) + 1u;
                        uint32_t observedPeak = peakConcurrentTasks.load(std::memory_order_relaxed);
                        while (observedPeak < current &&
                               !peakConcurrentTasks.compare_exchange_weak(observedPeak, current, std::memory_order_relaxed))
                        {
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        activeTasks.fetch_sub(1u, std::memory_order_acq_rel);
                        return x + 10;
                    }, branchSeed);
                    return sub.add([&activeTasks, &peakConcurrentTasks](int y)
                    {
                        const uint32_t current = activeTasks.fetch_add(1u, std::memory_order_acq_rel) + 1u;
                        uint32_t observedPeak = peakConcurrentTasks.load(std::memory_order_relaxed);
                        while (observedPeak < current &&
                               !peakConcurrentTasks.compare_exchange_weak(observedPeak, current, std::memory_order_relaxed))
                        {
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        activeTasks.fetch_sub(1u, std::memory_order_acq_rel);
                        return y * 2;
                    }, first);
                }, seed + b));
            }
            return root.add([](eastl::vector<int> values)
            {
                int sum = 0;
                for (int v : values)
                {
                    sum += v;
                }
                return sum;
            }, std::move(branches));
        }, 1);

        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_EQ(peakConcurrentTasks.load(std::memory_order_acquire), 1u);
        EXPECT_EQ(execution.get(), (11 * 2) + (12 * 2) + (13 * 2) + (14 * 2));
    }

    /// Exercises deep linear subgraph completion and cancellation without recursive stack growth.
    TEST(TaskRuntimeTests, DeepNestedSubgraphsCompleteAndCancelIteratively)
    {
        ThreadExecutor executor(2);
        TaskRuntime runtime(executor, {.maximumActiveExecutions = 4, .maximumTasksPerExecution = 1024});

        auto completed = runtime.submit([](int depth)
        {
            TaskGraph root;
            return root.add(DeepLinearSubgraphChain{nullptr}, depth, 0);
        }, 256);
        ASSERT_TRUE(completed.wait(10'000'000'000ull));
        ASSERT_EQ(completed.status(), ExecutionStatus::Succeeded) << completed.error().c_str();
        EXPECT_EQ(completed.get(), 257);

        auto gate = std::make_shared<TestBlockingGate>();
        auto cancelled = runtime.submit([gate](int depth)
        {
            TaskGraph root;
            return root.add(DeepLinearSubgraphChain{gate}, depth, 0);
        }, 256);
        {
            std::unique_lock lock(gate->mutex);
            ASSERT_TRUE(gate->cv.wait_for(lock, std::chrono::seconds(5), [&gate] { return gate->entered; }));
        }
        cancelled.cancel();
        {
            std::lock_guard lock(gate->mutex);
            gate->released = true;
        }
        gate->cv.notify_all();
        ASSERT_TRUE(cancelled.wait(10'000'000'000ull));
        EXPECT_EQ(cancelled.status(), ExecutionStatus::Cancelled);
    }
}
