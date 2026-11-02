#include <gtest/gtest.h>
#include <GVMTestCommon.hpp>
#include "generate_result.hpp"
#include "GpuGraphTestNode.hpp"
#include "GpuGraphTestExecutor.hpp"
#include <chrono>
#include <EASTL/optional.h>

/// Executes the generated mixed graph on one injected worker without blocking it on GPU completion.
TEST(GpuTaskGraphTests, GeneratedGraphCompletesWithOneWorker)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();
    {
        GVM::Core::ThreadExecutor executor(1);
        GVM::Core::TaskRuntime runtime(*device, executor);
        auto execution = runtime.submit(GpuGraphFixture::Entries::buildGraph, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_FLOAT_EQ(execution.get(), 446.5f);
    }
}

/// Reuses the same entry concurrently while every node invocation owns distinct resources and Args.
TEST(GpuTaskGraphTests, ConcurrentExecutionsKeepInvocationStateSeparate)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 4});
        eastl::vector<GVM::Core::Execution<float>> executions;
        for (uint32_t index = 0; index < 8; ++index)
        {
            GpuGraphFixture::Input input;
            input.scale = float(index);
            executions.push_back(runtime.submit(GpuGraphFixture::Entries::buildGraph, input));
        }
        for (uint32_t index = 0; index < executions.size(); ++index)
        {
            ASSERT_TRUE(executions[index].wait(10'000'000'000ull));
            ASSERT_EQ(executions[index].status(), GVM::Core::ExecutionStatus::Succeeded) << executions[index].error().c_str();
            EXPECT_FLOAT_EQ(executions[index].get(), 127.5f * float(index + 1) + 64.0f);
        }
    }
}

/// Rejects inferred GPU work on a CPU-only runtime without exposing an execution-domain switch in graph.add.
TEST(GpuTaskGraphTests, CpuRuntimeRejectsGpuWork)
{
    GVM::Core::TaskRuntime runtime({.workerCount = 1});
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildGraph, GpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(5'000'000'000ull));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Failed);
    EXPECT_THROW(execution.get(), std::logic_error);
}

/// Keeps an owned GPU result alive after its runtime and execution handle have been destroyed.
TEST(GpuTaskGraphTests, BufferResultOutlivesRuntime)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::GraphBuffer result;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 1});
        auto execution = runtime.submit(GpuGraphFixture::Entries::buildBuffer, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        result = execution.get();
    }
    EXPECT_EQ(result.size(), 256 * sizeof(float));
    EXPECT_THROW(result.slice(result.size(), 1), std::out_of_range);
    eastl::vector<float> values(256);
    auto *queue = device->getQueue({});
    auto readback = queue->readBufferAsync(result.range());
    auto completion = queue->submit({}, {});
    ASSERT_TRUE(completion.wait(10'000'000'000ull));
    readback->wait();
    std::memcpy(values.data(), readback->getData().data(), result.size());
    EXPECT_FLOAT_EQ(values.front(), 0.0f);
    EXPECT_FLOAT_EQ(values.back(), 255.0f / 256.0f);
}

/// Drains and joins GPU completion before destroying the borrowed runtime device.
TEST(GpuTaskGraphTests, RuntimeDestructionDrainsAcceptedGpuWork)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    eastl::optional<GVM::Core::Execution<float>> execution;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 1});
        execution = runtime.submit(GpuGraphFixture::Entries::buildGraph, GpuGraphFixture::Input{});
    }
    ASSERT_EQ(execution->status(), GVM::Core::ExecutionStatus::Succeeded) << execution->error().c_str();
    EXPECT_FLOAT_EQ(execution->get(), 446.5f);
}

/// Preserves GPU dependencies across the smallest allowed submission batches.
TEST(GpuTaskGraphTests, SingleQueueOnePassBatchesPreserveDataflow)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    EXPECT_EQ(device->getQueue({.prefer = GVM::RHI::QueuePreference::Graphics}),
        device->getQueue({.prefer = GVM::RHI::QueuePreference::Compute}));
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 3, .maximumPassesPerBatch = 1});
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildGraph, GpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 446.5f);
}

/// Fails an oversized subgraph before submission and keeps the runtime usable for CPU work.
TEST(GpuTaskGraphTests, PassBudgetFailureDoesNotPoisonRuntime)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 2, .maximumPassesPerExecution = 2});
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildGraph, GpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Failed);
    EXPECT_THROW(execution.get(), std::length_error);
    auto next = runtime.submit(GpuGraphFixture::Entries::buildBuffer, GpuGraphFixture::Input{});
    ASSERT_TRUE(next.wait(10'000'000'000ull));
    EXPECT_EQ(next.status(), GVM::Core::ExecutionStatus::Succeeded) << next.error().c_str();
}

/// Confirms two passes record simultaneously on the injected workers even on a single GPU queue.
TEST(GpuTaskGraphTests, IndependentPassesRecordInParallel)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::ThreadExecutor executor(2);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), executor);
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->released = false;
    auto execution = runtime.submit([probe](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        return graph.add(GpuGraphTestNode(probe), count);
    }, 2u);
    {
        std::unique_lock lock(probe->mutex);
        EXPECT_TRUE(probe->changed.wait_for(lock, std::chrono::seconds(3), [&probe] { return probe->started == 2; }));
        EXPECT_EQ(probe->maximumActive, 2u);
        probe->released = true;
    }
    probe->changed.notify_all();
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
}

/// Stops unstarted pass recordings and waits for already accepted callbacks before publishing cancellation.
TEST(GpuTaskGraphTests, CancellationDrainsRecording)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 2});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->released = false;
    auto execution = runtime.submit([probe](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        return graph.add(GpuGraphTestNode(probe), count);
    }, 16u);
    {
        std::unique_lock lock(probe->mutex);
        EXPECT_TRUE(probe->changed.wait_for(lock, std::chrono::seconds(3), [&probe] { return probe->started == 2; }));
    }
    execution.cancel();
    EXPECT_FALSE(execution.ready());
    {
        std::lock_guard lock(probe->mutex);
        probe->released = true;
    }
    probe->changed.notify_all();
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Cancelled);
    EXPECT_EQ(probe->started, 2u);
}

/// Reports a recording exception without submitting partial pass work or blocking the monitor.
TEST(GpuTaskGraphTests, RecordingFailureTerminates)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 2});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->fail = true;
    auto execution = runtime.submit([probe](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        return graph.add(GpuGraphTestNode(probe), count);
    }, 8u);
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Failed);
    EXPECT_THROW(execution.get(), std::runtime_error);
}

/// Keeps mutable legacy descriptors on an ordered recording lane.
TEST(GpuTaskGraphTests, OpaquePassesKeepLegacyRecordingOrder)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 4});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    auto execution = runtime.submit([probe](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        return graph.add(GpuGraphTestNode(probe, true), count);
    }, 8u);
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_EQ(probe->started, 8u);
    EXPECT_EQ(probe->maximumActive, 1u);
}

/// Resumes graph expansion after a readback and schedules a second GPU wave on the same worker.
TEST(GpuTaskGraphTests, ReadbackCanFeedAnotherGpuWave)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 1});
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildTwoWaves, GpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 127.5f);
}

/// Records compute and graphics together while preserving the final graphics attachment contents.
TEST(GpuTaskGraphTests, RenderAndComputeRecordTogetherWithOrderedGraphics)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    auto texture = device->createTexture({.label = "GraphRenderTarget",
        .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto view = texture->createView();
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
        probe->released = false;
        auto execution = runtime.submit([probe, view](uint32_t count)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(GpuGraphTestNode(probe, false, view), count);
        }, 3u);
        {
            std::unique_lock lock(probe->mutex);
            EXPECT_TRUE(probe->changed.wait_for(lock, std::chrono::seconds(3), [&probe] { return probe->started == 2; }));
            EXPECT_EQ(probe->maximumActive, 2u);
            probe->released = true;
        }
        probe->changed.notify_all();
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        auto *queue = device->getQueue({});
        auto readback = queue->readTextureAsync({.texture = texture}, {.bytesPerRow = 4, .rowsPerImage = 1}, {1, 1, 1});
        auto completion = queue->submit({}, {});
        ASSERT_TRUE(completion.wait(10'000'000'000ull));
        readback->wait();
        const auto pixel = readback->getData();
        EXPECT_EQ(pixel[0], 0);
        EXPECT_EQ(pixel[1], 255);
        EXPECT_EQ(pixel[2], 0);
        EXPECT_EQ(pixel[3], 255);
    }
    view = {};
    device->freeTexture(texture);
}

/// Terminates cleanly when an injected executor rejects recording or the GPU completion continuation.
TEST(GpuTaskGraphTests, ExecutorRejectionDrainsGpuState)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    for (uint32_t accepted : {2u, 3u})
    {
        GpuGraphTestExecutor executor(accepted);
        GVM::Core::TaskRuntime runtime(*device, executor);
        auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
        auto execution = runtime.submit([probe](uint32_t count)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(GpuGraphTestNode(probe), count);
        }, 1u);
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Failed);
        EXPECT_THROW(execution.get(), std::runtime_error);
        EXPECT_EQ(probe->started, accepted == 2 ? 0u : 1u);
    }
}

/// Expands independent node instances simultaneously before freezing their pass wave.
TEST(GpuTaskGraphTests, IndependentRenderNodesBuildInParallel)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 2});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->released = false;
    probe->observeBuild = true;
    auto execution = runtime.submit([probe](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        graph.add(GpuGraphTestNode(probe), count);
        return graph.add(GpuGraphTestNode(probe), count);
    }, 0u);
    {
        std::unique_lock lock(probe->mutex);
        EXPECT_TRUE(probe->changed.wait_for(lock, std::chrono::seconds(3), [&probe] { return probe->started == 2; }));
        EXPECT_EQ(probe->maximumActive, 2u);
        probe->released = true;
    }
    probe->changed.notify_all();
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
}

/// Retains an owning literal GPU input while generated immutable Args hold only native buffer ranges.
TEST(GpuTaskGraphTests, LiteralGpuInputRemainsOwnedThroughSubmission)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::GraphBuffer input;
    {
        GVM::Core::TaskRuntime producer(*device, {.workerCount = 1});
        auto produced = producer.submit(GpuGraphFixture::Entries::buildBuffer, GpuGraphFixture::Input{});
        ASSERT_TRUE(produced.wait(10'000'000'000ull));
        input = produced.get();
    }
    GVM::Core::TaskRuntime consumer(*device, {.workerCount = 1});
    auto execution = consumer.submit([](GVM::Core::GraphBuffer buffer)
    {
        GVM::Core::TaskGraph graph;
        auto filtered = graph.add(GpuGraphFixture::Filter{}, buffer);
        auto readback = graph.add(GVM::Core::ReadbackNode<float>{}, filtered);
        return graph.add(GpuGraphFixture::sum, readback);
    }, eastl::move(input));
    EXPECT_THROW(input.range(), std::logic_error);
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 446.5f);
}
