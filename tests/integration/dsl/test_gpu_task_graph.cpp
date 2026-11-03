#include <gtest/gtest.h>
#include <GVMTestCommon.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKTesting.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKNativeRecordingProbe.hpp>
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

/// Returns independently owned CPU readback data after all graph allocations and its runtime are released.
TEST(GpuTaskGraphTests, CpuReadbackResultOutlivesRuntime)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    eastl::vector<float> result;
    {
        GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 1});
        auto execution = runtime.submit([](GpuGraphFixture::Input input)
        {
            GVM::Core::TaskGraph graph;
            auto values = graph.add(GpuGraphFixture::generate, input);
            auto buffer = graph.add(GpuGraphFixture::Upload{}, values);
            return graph.add(GVM::Core::ReadbackNode<float>{}, buffer);
        }, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        result = execution.get();
    }
    ASSERT_EQ(result.size(), 256u);
    EXPECT_FLOAT_EQ(result.front(), 0.0f);
    EXPECT_FLOAT_EQ(result.back(), 255.0f / 256.0f);
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
TEST(GpuTaskGraphTests, OnePassBatchesPreserveDataflow)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
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
    auto next = runtime.submit([](GpuGraphFixture::Input input)
    {
        GVM::Core::TaskGraph graph;
        return graph.add(GpuGraphFixture::generate, input);
    }, GpuGraphFixture::Input{});
    ASSERT_TRUE(next.wait(10'000'000'000ull));
    EXPECT_EQ(next.status(), GVM::Core::ExecutionStatus::Succeeded) << next.error().c_str();
}

/// Confirms two passes record simultaneously on the injected workers even on a single GPU queue.
TEST(GpuTaskGraphTests, IndependentPassesRecordInParallel)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::ThreadExecutor executor(2);
    GVM::Core::TaskRuntime runtime(*device, executor);
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->allocateProbeBuffer = true;
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
    // Advance the normal queue collection point before checking physical resource retirement.
    device->getQueue({})->submit({});
    EXPECT_TRUE(probe->buffer.isNull());
}

/// Completes a ready second branch on the GPU while the first branch is still held inside its CPU recording callback.
TEST(GpuTaskGraphTests, ReadyQueueBypassesBlockedFirstRecording)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    auto *graphics = device->getMainQueue();
    auto *compute = device->getQueue({.prefer = QueuePreference::Compute});
    if (!Vulkan::Testing::isVulkanDevice(device) || graphics == compute)
    {
        GTEST_SKIP() << "This probe uses counters on two native Vulkan queues.";
    }
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->released = false;
    probe->gatedPass = 0;
    const auto before = Vulkan::Testing::getQueueUserSubmitCount(graphics);
    const auto blockedBefore = Vulkan::Testing::getQueueUserSubmitCount(compute);
    auto execution = runtime.submit([probe](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        return graph.add(GpuGraphTestNode(probe), count);
    }, 2u);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (Vulkan::Testing::getQueueUserSubmitCount(graphics) == before && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_GT(Vulkan::Testing::getQueueUserSubmitCount(graphics), before);
    EXPECT_TRUE(graphics->submit({}, {}).wait(1'000'000'000));
    EXPECT_EQ(Vulkan::Testing::getQueueUserSubmitCount(compute), blockedBefore);
    {
        std::lock_guard lock(probe->mutex);
        probe->released = true;
    }
    probe->changed.notify_all();
    ASSERT_TRUE(execution.wait(5'000'000'000));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
}

/// Proves render and compute native Vulkan bodies enter recording on distinct graph workers before any submission.
TEST(GpuTaskGraphTests, NativeRenderAndComputeRecordOnGraphWorkers)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
    {
        GTEST_SKIP() << "The native dispatcher probe is Vulkan-specific.";
    }
    auto *device = instance->createDevice();
    auto *graphics = device->getMainQueue();
    auto *compute = device->getQueue({.prefer = GVM::RHI::QueuePreference::Compute});
    const auto graphicsBefore = GVM::RHI::Vulkan::Testing::getQueueUserSubmitCount(graphics);
    const auto computeBefore = GVM::RHI::Vulkan::Testing::getQueueUserSubmitCount(compute);
    const auto target = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment |
        GVM::RHI::TextureUsage::CopySrc, .size = {2, 2, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto callbacks = eastl::make_shared<GpuGraphRecordingProbe>();
    {
        GVM::RHI::Vulkan::Testing::VKNativeRecordingProbe native(2);
        GVM::Core::ThreadExecutor executor(2);
        GVM::Core::TaskRuntime runtime(*device, executor);
        auto execution = runtime.submit([callbacks, view = target->createView()](uint32_t count)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(GpuGraphTestNode(callbacks, false, view), count);
        }, 2u);
        EXPECT_TRUE(native.waitForRecordings());
        EXPECT_EQ(native.maximumActive(), 2u);
        EXPECT_EQ(native.threadCount(), 2u);
        native.release();
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_GT(GVM::RHI::Vulkan::Testing::getQueueUserSubmitCount(graphics), graphicsBefore);
        EXPECT_GT(GVM::RHI::Vulkan::Testing::getQueueUserSubmitCount(compute), computeBefore);
        auto readback = device->getMainQueue()->readTextureAsync({.texture = target},
            {.bytesPerRow = 8, .rowsPerImage = 2}, {2, 2, 1});
        device->getMainQueue()->submit({});
        readback->wait();
        const auto pixels = readback->getData();
        EXPECT_EQ(pixels[0], 255u);
        EXPECT_EQ(pixels[1], 0u);
        EXPECT_EQ(pixels[3], 255u);
    }
    device->freeTexture(target);
}

/// Drains a native recording error and permits a subsequent graph on the same device and injected executor.
TEST(GpuTaskGraphTests, NativeRecordingFailureLeavesRuntimeUsable)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
    {
        GTEST_SKIP() << "The native dispatcher probe is Vulkan-specific.";
    }
    auto *device = instance->createDevice();
    GVM::Core::ThreadExecutor executor(2);
    GVM::Core::TaskRuntime runtime(*device, executor);
    {
        GVM::RHI::Vulkan::Testing::VKNativeRecordingProbe native(2, true);
        auto callbacks = eastl::make_shared<GpuGraphRecordingProbe>();
        auto execution = runtime.submit([callbacks](uint32_t count)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(GpuGraphTestNode(callbacks), count);
        }, 2u);
        EXPECT_TRUE(native.waitForRecordings());
        native.release();
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Failed);
    }
    auto next = runtime.submit(GpuGraphFixture::Entries::buildGraph, GpuGraphFixture::Input{});
    ASSERT_TRUE(next.wait(10'000'000'000ull));
    EXPECT_EQ(next.status(), GVM::Core::ExecutionStatus::Succeeded) << next.error().c_str();
    EXPECT_FLOAT_EQ(next.get(), 446.5f);
}

/// Stops unstarted pass recordings and waits for already accepted callbacks before publishing cancellation.
TEST(GpuTaskGraphTests, CancellationDrainsRecording)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->allocateProbeBuffer = true;
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
    // Advance the normal queue collection point before checking physical resource retirement.
    device->getQueue({})->submit({});
    EXPECT_TRUE(probe->buffer.isNull());
}

/// Reports a recording exception without submitting partial pass work or blocking the monitor.
TEST(GpuTaskGraphTests, RecordingFailureTerminates)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->allocateProbeBuffer = true;
    probe->fail = true;
    auto execution = runtime.submit([probe](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        return graph.add(GpuGraphTestNode(probe), count);
    }, 8u);
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    EXPECT_EQ(execution.status(), GVM::Core::ExecutionStatus::Failed);
    EXPECT_THROW(execution.get(), std::runtime_error);
    // Advance the normal queue collection point before checking physical resource retirement.
    device->getQueue({})->submit({});
    EXPECT_TRUE(probe->buffer.isNull());
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

/// Consumes an existing externally owned buffer without changing its RHI handle or owner contract.
TEST(GpuTaskGraphTests, ExistingBufferInputKeepsItsOwnerContract)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    auto input = device->createBuffer({.label = "ExistingBuffer",
        .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
        .size = 256 * sizeof(float)});
    auto values = GpuGraphFixture::generate({});
    auto *queue = device->getQueue({});
    queue->writeBuffer(input, values.data(), values.size() * sizeof(float));
    ASSERT_TRUE(queue->submit({}, {}).wait(10'000'000'000ull));
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 1});
        auto execution = runtime.submit([](GVM::RHI::Buffer buffer)
        {
            GVM::Core::TaskGraph graph;
            auto filtered = graph.add(GpuGraphFixture::Filter{}, buffer);
            auto readback = graph.add(GVM::Core::ReadbackNode<float>{}, filtered);
            return graph.add(GpuGraphFixture::sum, readback);
        }, input);
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_FLOAT_EQ(execution.get(), 446.5f);
    }
    EXPECT_FALSE(input.isNull());
    device->freeBuffer(input);
}

/// Publishes a readback while an unrelated pass is still recording on the same injected executor.
TEST(GpuTaskGraphTests, ReadbackAdvancesBeforeUnrelatedRecordingFinishes)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 2});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->released = false;
    auto execution = runtime.submit([probe](GpuGraphFixture::Input input)
    {
        GVM::Core::TaskGraph graph;
        auto values = graph.add(GpuGraphFixture::generate, input);
        auto buffer = graph.add(GpuGraphFixture::Upload{}, values);
        auto copied = graph.add(GVM::Core::ReadbackNode<float>{}, buffer);
        graph.add(GpuGraphTestNode(probe), 1u);
        return graph.add([probe](eastl::vector<float> result)
        {
            {
                std::lock_guard lock(probe->mutex);
                probe->released = true;
            }
            probe->changed.notify_all();
            return GpuGraphFixture::sum(result);
        }, copied);
    }, GpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 127.5f);
}

/// Keeps readback bursts within a small executor queue while preserving ordinary CPU continuations.
TEST(GpuTaskGraphTests, ReadbackContinuationsRespectAdmissionBudget)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::ThreadExecutor executor(1, 2);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), executor, {.maximumInFlightTasksPerExecution = 1});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->released = false;
    auto execution = runtime.submit([probe](GpuGraphFixture::Input input)
    {
        GVM::Core::TaskGraph graph;
        auto values = graph.add(GpuGraphFixture::generate, input);
        auto buffer = graph.add(GpuGraphFixture::Upload{}, values);
        auto copied = graph.add(GVM::Core::ReadbackNode<float>{}, buffer);
        auto output = graph.add([probe](eastl::vector<float> result)
        {
            std::unique_lock lock(probe->mutex);
            probe->started = 1;
            probe->changed.notify_all();
            if (!probe->changed.wait_for(lock, std::chrono::seconds(3), [&probe] { return probe->released; }))
            {
                throw std::runtime_error("Readback admission probe was not released.");
            }
            return GpuGraphFixture::sum(result);
        }, copied);
        for (uint32_t index = 0; index < 32; ++index)
        {
            graph.add(GVM::Core::ReadbackNode<float>{}, buffer);
        }
        return output;
    }, GpuGraphFixture::Input{});
    {
        std::unique_lock lock(probe->mutex);
        EXPECT_TRUE(probe->changed.wait_for(lock, std::chrono::seconds(3), [&probe] { return probe->started == 1; }));
    }
    EXPECT_FALSE(execution.wait(100'000'000ull));
    {
        std::lock_guard lock(probe->mutex);
        probe->released = true;
    }
    probe->changed.notify_all();
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 127.5f);
}

/// Projects one GPU output without introducing a CPU resource task or a separate public buffer type.
TEST(GpuTaskGraphTests, GeneratedGpuFieldsPreserveProducerDependencies)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 2});
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildProjectedGpuGraph, GpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 255.0f);
}

/// Validates actual DSL raster shader execution and render-to-compute texture synchronization.
TEST(GpuTaskGraphTests, GeneratedRasterGraphUsesExistingTextureAndFramebufferTypes)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 2});
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildRasterGraph, GpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 1.0f);
}

/// Reads the GPU result from a dependent raster graph while verifying that an available second queue adds no native submissions.
TEST(GpuTaskGraphTests, DependentRasterGraphAvoidsNativeQueueHandoffs)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    if (!Vulkan::Testing::isVulkanDevice(device))
    {
        GTEST_SKIP() << "The native submission counters require Vulkan.";
    }
    auto *graphics = device->getQueue({});
    auto *compute = device->getQueue({.prefer = QueuePreference::Compute});
    if (graphics == compute)
    {
        GTEST_SKIP() << "Two distinct queue candidates are required.";
    }
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
    const auto graphicsBefore = Vulkan::Testing::getQueueBackendSubmitCount(graphics);
    const auto computeBefore = Vulkan::Testing::getQueueBackendSubmitCount(compute);
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildRasterGraph, GpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 1.0f);
    EXPECT_EQ(Vulkan::Testing::getQueueBackendSubmitCount(graphics) - graphicsBefore, 1u);
    EXPECT_EQ(Vulkan::Testing::getQueueBackendSubmitCount(compute) - computeBefore, 0u);
}

/// Measures the actual single-queue submission reduction without assuming a GPU speedup.
TEST(GpuTaskGraphTests, VulkanSingleQueueBatchesReduceSubmissions)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    if (!GVM::RHI::Vulkan::Testing::isVulkanDevice(device))
    {
        GTEST_SKIP() << "Vulkan-only submission counters.";
    }
    GVM::RHI::Vulkan::Testing::useSingleQueue(*device);
    auto *queue = device->getQueue({});
    const auto before = GVM::RHI::Vulkan::Testing::getQueueUserSubmitCount(queue);
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 1, .maximumPassesPerBatch = 1});
        auto execution = runtime.submit(GpuGraphFixture::Entries::buildGraph, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_FLOAT_EQ(execution.get(), 446.5f);
    }
    const auto unbatched = GVM::RHI::Vulkan::Testing::getQueueUserSubmitCount(queue) - before;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2, .maximumPassesPerBatch = 64});
        auto execution = runtime.submit(GpuGraphFixture::Entries::buildGraph, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_FLOAT_EQ(execution.get(), 446.5f);
    }
    const auto batched = GVM::RHI::Vulkan::Testing::getQueueUserSubmitCount(queue) - before - unbatched;
    EXPECT_EQ(unbatched, 5u);
    EXPECT_EQ(batched, 1u);
    RecordProperty("unbatched_submissions", int(unbatched));
    RecordProperty("batched_submissions", int(batched));
}

/// Transfers successful GPU allocations to the existing explicit device destruction contract.
TEST(GpuTaskGraphTests, ExportedBuffersOutliveExecutionAndRuntimeUntilExplicitDestruction)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GpuGraphFixture::BranchOutputs outputs;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        auto execution = runtime.submit(GpuGraphFixture::Entries::buildBufferOutputs, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        outputs = execution.get();
        EXPECT_EQ(execution.get().scaled, outputs.scaled);
    }
    auto *queue = device->getQueue({});
    auto scaled = queue->readBufferAsync(outputs.scaled);
    auto biased = queue->readBufferAsync(outputs.biased);
    ASSERT_TRUE(queue->submit({}, {}).wait(10'000'000'000ull));
    scaled->wait();
    biased->wait();
    float lastScaled = 0.0f;
    float firstBiased = 0.0f;
    std::memcpy(&lastScaled, scaled->getData().data() + 255 * sizeof(float), sizeof(float));
    std::memcpy(&firstBiased, biased->getData().data(), sizeof(float));
    EXPECT_FLOAT_EQ(lastScaled, 255.0f / 128.0f);
    EXPECT_FLOAT_EQ(firstBiased, 0.25f);
    device->freeBuffer(outputs.scaled);
    device->freeBuffer(outputs.biased);
    queue->submit({});
    EXPECT_TRUE(outputs.scaled.isNull());
    EXPECT_TRUE(outputs.biased.isNull());
}

/// Exports an ordinary texture handle and verifies its raster contents after runtime destruction.
TEST(GpuTaskGraphTests, ExportedTextureUsesExplicitDestruction)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::RHI::Texture texture;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        auto execution = runtime.submit(GpuGraphFixture::Entries::buildImage, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        texture = execution.get();
    }
    auto *queue = device->getQueue({});
    auto readback = queue->readTextureAsync({.texture = texture}, {.bytesPerRow = 4, .rowsPerImage = 1}, {1, 1, 1});
    ASSERT_TRUE(queue->submit({}, {}).wait(10'000'000'000ull));
    readback->wait();
    EXPECT_EQ(readback->getData()[1], 255);
    device->freeTexture(texture);
    queue->submit({});
    EXPECT_TRUE(texture.isNull());
}

/// Counts a graph allocation label using the existing backend resource snapshot interface.
static uint32_t countGraphAllocations(GVM::RHI::DeviceImpl &device, const eastl::string &label)
{
    uint32_t count = 0;
    for (const auto &resource : device.getDiagnosticsResourceSnapshot().entries)
    {
        if (resource.label == label)
        {
            ++count;
        }
    }
    return count;
}

/// Reclaims successful outputs that were never retrieved instead of leaking an implicit ownership transfer.
TEST(GpuTaskGraphTests, UnretrievedGpuOutputIsReclaimed)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    auto *queue = device->getQueue({});
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 1});
    {
        auto execution = runtime.submit(GpuGraphFixture::Entries::buildBuffer, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        queue->submit({});
        EXPECT_EQ(countGraphAllocations(*device, "GraphInput"), 1u);
    }
    queue->submit({});
    EXPECT_EQ(countGraphAllocations(*device, "GraphInput"), 0u);
}

/// Treats copied buffer handles as aliases of the final version rather than content snapshots.
TEST(GpuTaskGraphTests, AliasedFieldsObserveFinalResourceVersion)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 2});
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildAliasedOutputGraph, GpuGraphFixture::Input{});
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 255.0f);
}
