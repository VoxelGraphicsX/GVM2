#include <gtest/gtest.h>
#include <GVMTestCommon.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKTesting.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKNativeRecordingProbe.hpp>
#include "generate_result.hpp"
#include "GpuGraphTestNode.hpp"
#include "GpuGraphTestExecutor.hpp"
#include <chrono>
#include <EASTL/optional.h>

/// Joins two independent GPU writes and consumes their result in a render node without caller-supplied waits.
TEST(GpuTaskGraphTests, RenderNodeReadsJoinedComputeBranches)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    const float value = -0.0625f;
    auto input = device->createBuffer({.usage = BufferUsage::Storage | BufferUsage::CopyDst, .size = sizeof(value)});
    auto *queue = device->getMainQueue();
    queue->writeBuffer(input, &value, sizeof(value));
    ASSERT_TRUE(queue->submit({}, {}).wait(5'000'000'000ull));

    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        auto execution = runtime.submit([](Buffer source)
        {
            GVM::Core::TaskGraph graph;
            GpuGraphFixture::Filter filter;
            filter.elementCount = 1;
            auto combined = graph.add(filter, source);
            auto rendered = graph.add(GpuGraphFixture::PreviewNode{}, combined);
            auto result = graph.add(GVM::Core::ReadbackNode<float>{}, rendered);
            return graph.add(GpuGraphFixture::sum, result);
        }, input);
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        // Preview writes 4 * (2*x + x + 0.25) to an RGBA8Unorm attachment.
        EXPECT_NEAR(execution.get(), 0.25f, 1.0f / 255.0f);
    }
    device->freeBuffer(input);
}

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
    if (graphics == compute)
    {
        GTEST_SKIP() << "The device exposes only one queue.";
    }
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
    auto target = device->createTexture({.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc |
        TextureUsage::CopyDst, .size = {1, 1, 1}, .format = TextureFormat::RGBA8Unorm});
    const uint8_t black[] = {0, 0, 0, 255};
    const TextureDataLayout row = {.bytesPerRow = 4, .rowsPerImage = 1};
    graphics->writeTexture({.texture = target}, black, sizeof(black), row, {1, 1, 1});
    ASSERT_TRUE(graphics->submit({}, {}).wait(5'000'000'000ull));

    // Only Graph may submit from here. Each pending readback runs after its queue's first user pass.
    auto pixel = graphics->readTextureAsync({.texture = target}, row, {1, 1, 1});
    auto marker = device->createBuffer({.usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .size = 4});
    const uint32_t sentinel = 0x4d;
    compute->writeBuffer(marker, &sentinel, sizeof(sentinel));
    auto computeRead = compute->readBufferAsync(marker);
    auto held = eastl::make_shared<GpuGraphRecordingProbe>();
    held->released = false;
    auto execution = runtime.submit([held, view = target->createView()](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        const auto builtCount = graph.add(GpuGraphTestNode(held), count);
        return graph.add(GpuGraphTestNode(eastl::make_shared<GpuGraphRecordingProbe>(), false, view), builtCount);
    }, 1u);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    {
        std::unique_lock lock(held->mutex);
        EXPECT_TRUE(held->changed.wait_until(lock, deadline, [&held] { return held->active == 1; }));
    }
    while (!pixel->isReady() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(pixel->isReady()) << "The graphics queue did not finish while compute recording was held.";
    EXPECT_FALSE(computeRead->isReady());
    {
        std::lock_guard lock(held->mutex);
        EXPECT_EQ(held->active, 1u);
        held->released = true;
    }
    held->changed.notify_all();
    ASSERT_TRUE(execution.wait(5'000'000'000));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    ASSERT_TRUE(computeRead->isReady());
    ASSERT_TRUE(pixel->isReady());
    const auto bytes = pixel->getData();
    ASSERT_GE(bytes.size(), sizeof(black));
    EXPECT_EQ(bytes[0], 255u);
    EXPECT_EQ(bytes[1], 0u);
    EXPECT_EQ(bytes[2], 0u);
    EXPECT_EQ(bytes[3], 255u);
    device->freeBuffer(marker);
    device->freeTexture(target);
}

/// Completes a transfer branch while independent render and compute recordings are both blocked on CPU workers.
TEST(GpuTaskGraphTests, TransferCompletesWhileRenderAndComputeRecordingAreBlocked)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    auto *graphics = device->getMainQueue();
    auto *compute = device->getQueue({.prefer = QueuePreference::Compute});
    auto *transfer = device->getQueue({.prefer = QueuePreference::Transfer});
    if (graphics == compute || graphics == transfer || compute == transfer)
    {
        GTEST_SKIP() << "Three independent queue candidates are required.";
    }
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 3});
    const auto target = device->createTexture({.usage = TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = TextureFormat::RGBA8Unorm});
    const auto marker = device->createBuffer({.usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .size = 4});
    auto readback = transfer->readBufferAsync(marker);
    auto held = eastl::make_shared<GpuGraphRecordingProbe>();
    held->released = false;
    auto copied = eastl::make_shared<GpuGraphRecordingProbe>();
    copied->transferPasses = true;
    copied->buffer = marker;
    auto execution = runtime.submit([held, copied, view = target->createView()](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        const auto built = graph.add(GpuGraphTestNode(held, false, view), count);
        return graph.add(GpuGraphTestNode(copied), built);
    }, 2u);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    {
        std::unique_lock lock(held->mutex);
        EXPECT_TRUE(held->changed.wait_until(lock, deadline, [&held] { return held->active == 2; }));
    }
    while (!readback->isReady() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(readback->isReady()) << "The ready transfer queue was blocked behind unrelated CPU recordings.";
    {
        std::lock_guard lock(held->mutex);
        EXPECT_EQ(held->active, 2u);
        held->released = true;
    }
    held->changed.notify_all();
    ASSERT_TRUE(execution.wait(5'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    ASSERT_TRUE(readback->isReady());
    ASSERT_EQ(readback->getData().size(), 4u);
    for (uint8_t value : readback->getData())
    {
        EXPECT_EQ(value, 2u);
    }
    device->freeBuffer(marker);
    device->freeTexture(target);
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

/// Resumes graph expansion after a readback and schedules a second GPU launch group on the same worker.
TEST(GpuTaskGraphTests, ReadbackCanFeedAnotherGpuLaunchGroup)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    GVM::Core::TaskRuntime runtime(*instance->createDevice(), {.workerCount = 1});
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildTwoLaunchGroups, GpuGraphFixture::Input{});
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

/// Expands independent node instances simultaneously before freezing their GPU launch group.
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
        outputs = execution.take();
        EXPECT_THROW(execution.get(), std::logic_error);
        EXPECT_THROW(execution.take(), std::logic_error);
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

/// Submits an upload-and-readback transfer chain on the auxiliary transfer queue while a graphics pass is still recording.
TEST(GpuTaskGraphTests, TransferChainUsesAuxiliaryQueueWhileGraphicsRecords)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    auto *graphics = device->getMainQueue();
    auto *transfer = device->getQueue({.prefer = QueuePreference::Transfer});
    if (graphics == transfer)
    {
        GTEST_SKIP() << "The device exposes only one queue.";
    }
    auto target = device->createTexture({.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc,
        .size = {1, 1, 1}, .format = TextureFormat::RGBA8Unorm});
    auto marker = device->createBuffer({.usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .size = sizeof(uint32_t)});
    const uint32_t sentinel = 0x5au;
    transfer->writeBuffer(marker, &sentinel, sizeof(sentinel));
    auto transferRead = transfer->readBufferAsync(marker);
    auto held = eastl::make_shared<GpuGraphRecordingProbe>();
    held->released = false;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        auto execution = runtime.submit([held, transferRead, view = target->createView()](GpuGraphFixture::Input input)
        {
            GVM::Core::TaskGraph graph;
            auto values = graph.add(GpuGraphFixture::generate, input);
            auto buffer = graph.add(GpuGraphFixture::Upload{}, values);
            auto copied = graph.add(GVM::Core::ReadbackNode<float>{}, buffer);
            graph.add(GpuGraphTestNode(held, false, view), 1u);
            return graph.add([held, transferRead](eastl::vector<float> result)
            {
                EXPECT_TRUE(transferRead->isReady());
                {
                    std::lock_guard lock(held->mutex);
                    held->released = true;
                }
                held->changed.notify_all();
                return GpuGraphFixture::sum(result);
            }, copied);
        }, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_FLOAT_EQ(execution.get(), 127.5f);
    }
    ASSERT_TRUE(transferRead->isReady());
    uint32_t observed = 0;
    std::memcpy(&observed, transferRead->getData().data(), sizeof(observed));
    EXPECT_EQ(observed, sentinel);
    device->freeBuffer(marker);
    device->freeTexture(target);
}

namespace
{
    struct NativeExportedResources
    {
        GVM::RHI::Buffer buffer;
        GVM::RHI::Texture texture;
    };

    struct NativeStructProducer final : public GVM::Core::IRenderNode<uint32_t, NativeExportedResources>
    {
        NativeExportedResources build(GVM::Core::RenderNode &node, uint32_t) const
        {
            NativeExportedResources pair;
            pair.buffer = node.createBuffer({.label = "TraitBuffer",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc, .size = 16});
            node.fillBuffer(pair.buffer, 0x3cu);
            pair.texture = node.createTexture({.label = "TraitTexture",
                .usage = GVM::RHI::TextureUsage::CopySrc | GVM::RHI::TextureUsage::RenderAttachment,
                .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
            return pair;
        }
    };
}

namespace GVM::Core
{
    template <>
    struct GraphResourceTraits<NativeExportedResources>
    {
        static constexpr bool containsResources = true;

        template <class Visitor>
        static void visit(const NativeExportedResources &value, const Visitor &visitor)
        {
            visitGraphResource(value.buffer, visitor);
            visitGraphResource(value.texture, visitor);
        }
    };
}

/// Runs an independent GPU launch group and its downstream readback continuation while an unrelated CPU branch is still executing.
TEST(GpuTaskGraphTests, SlowCpuBranchDoesNotBlockIndependentGpuLaunchGroup)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 3});

    struct Gate
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool gpuDone = false;
    };
    auto gate = eastl::make_shared<Gate>();

    auto execution = runtime.submit([gate](GpuGraphFixture::Input input)
    {
        GVM::Core::TaskGraph graph;
        auto slowCpu = graph.add([gate](float scale)
        {
            std::unique_lock lock(gate->mutex);
            if (!gate->cv.wait_for(lock, std::chrono::seconds(5), [&gate] { return gate->gpuDone; }))
            {
                throw std::runtime_error("Independent GPU launch group was blocked by slow CPU branch.");
            }
            return scale + 1.0f;
        }, input.scale);

        auto values = graph.add(GpuGraphFixture::generate, input);
        auto buffer = graph.add(GpuGraphFixture::Upload{}, values);
        auto copied = graph.add(GVM::Core::ReadbackNode<float>{}, buffer);
        auto gpuSum = graph.add([gate](eastl::vector<float> result)
        {
            {
                std::lock_guard lock(gate->mutex);
                gate->gpuDone = true;
            }
            gate->cv.notify_all();
            return GpuGraphFixture::sum(result);
        }, copied);

        return graph.add([](float a, float b) { return a + b; }, slowCpu, gpuSum);
    }, GpuGraphFixture::Input{});

    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 130.5f);
}

/// Tracks GPU allocations inside native C++ structs via GraphResourceTraits across runtime destruction.
TEST(GpuTaskGraphTests, NativeStructExportsPreserveGpuAllocationsViaTraits)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    NativeExportedResources exported;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        auto execution = runtime.submit([](uint32_t value)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(NativeStructProducer{}, value);
        }, 0u);
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        exported = execution.get();
    }
    EXPECT_FALSE(exported.buffer.isNull());
    EXPECT_FALSE(exported.texture.isNull());
    auto *queue = device->getQueue({});
    auto readback = queue->readBufferAsync(exported.buffer);
    ASSERT_TRUE(queue->submit({}, {}).wait(5'000'000'000ull));
    readback->wait();
    ASSERT_EQ(readback->getData().size(), 16u);
    EXPECT_EQ(readback->getData()[0], 0x3cu);
    device->freeBuffer(exported.buffer);
    device->freeTexture(exported.texture);
    queue->submit({});
    EXPECT_TRUE(exported.buffer.isNull());
    EXPECT_TRUE(exported.texture.isNull());
}

/// Records and submits a large multi-batch pass graph within the sliding recording window without OOM.
TEST(GpuTaskGraphTests, LargePassGraphCompletesUnderSlidingRecordingWindow)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::TaskRuntime runtime(*device, {
        .workerCount = 4,
        .maximumPassesPerExecution = 1024,
        .maximumPassesPerBatch = 16,
    });
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    auto execution = runtime.submit([probe](uint32_t count)
    {
        GVM::Core::TaskGraph graph;
        return graph.add(GpuGraphTestNode(probe), count);
    }, 640u);
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_EQ(execution.get(), 640u);
}

namespace
{
    /// Rejects an invalid Submission passed to RenderNode::waitFor.
    struct InvalidWaitForNode final : public GVM::Core::IRenderNode<uint32_t, uint32_t>
    {
        /// Calls RenderNode::waitFor with an unbound Submission handle.
        uint32_t build(GVM::Core::RenderNode &node, uint32_t value) const
        {
            node.waitFor(GVM::RHI::Submission{});
            return value;
        }
    };

    /// Attaches an external Submission dependency to a buffer copy and reads back the copied value.
    struct AttachedWaitForNode final : public GVM::Core::IRenderNode<GVM::RHI::Buffer, GVM::RHI::Buffer>
    {
        GVM::RHI::Submission dependency;

        /// Stores the external submission handle awaited before copying the buffer.
        explicit AttachedWaitForNode(GVM::RHI::Submission externalDependency)
            : dependency(eastl::move(externalDependency))
        {
        }

        /// Waits on the external submission before copying source into a graph-owned readback buffer.
        GVM::RHI::Buffer build(GVM::Core::RenderNode &node, GVM::RHI::Buffer source) const
        {
            auto destination = node.createBuffer({
                .label = "WaitForCopyDst",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
                .size = sizeof(uint32_t),
            });
            node.waitFor(dependency);
            node.copyBuffer(source, destination);
            return destination;
        }
    };

    /// Records an unattached RenderNode::waitFor without emitting subsequent GPU passes.
    struct UnattachedWaitForNode final : public GVM::Core::IRenderNode<uint32_t, uint32_t>
    {
        GVM::RHI::Submission dependency;

        /// Stores the external submission handle flushed by RenderNode::finish.
        explicit UnattachedWaitForNode(GVM::RHI::Submission externalDependency)
            : dependency(eastl::move(externalDependency))
        {
        }

        /// Registers an external wait with no following passes so finish() must flush it.
        uint32_t build(GVM::Core::RenderNode &node, uint32_t value) const
        {
            node.waitFor(dependency);
            return value + 1u;
        }
    };

    /// Exercises RenderNode::renderToSwapchain input validation on native C++ nodes.
    struct InvalidSwapchainRenderNode final : public GVM::Core::IRenderNode<uint32_t, uint32_t>
    {
        GVM::RHI::SwapchainQueryResult target;
        GVM::RHI::Texture source;

        /// Captures the swapchain query result and source texture passed to renderToSwapchain.
        InvalidSwapchainRenderNode(GVM::RHI::SwapchainQueryResult swapchainTarget, GVM::RHI::Texture sourceTexture)
            : target(eastl::move(swapchainTarget))
            , source(eastl::move(sourceTexture))
        {
        }

        /// Invokes RenderNode::renderToSwapchain with the configured arguments.
        uint32_t build(GVM::Core::RenderNode &node, uint32_t value) const
        {
            node.renderToSwapchain(target, source);
            return value;
        }
    };
} // namespace

/// Executes generated 1D texture DSL codegen using explicit RenderNode&, writeTexture, copyTexture, and ReadbackTextureNode.
TEST(GpuTaskGraphTests, GeneratedTexture1DGraphCopiesAndReadsBackTexels)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
    const eastl::vector<uint8_t> texels = {
        10u, 20u, 30u, 40u,
        50u, 60u, 70u, 80u,
        90u, 100u, 110u, 120u,
        130u, 140u, 150u, 160u,
    };
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildTexture1DReadbackGraph, texels);
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    const auto readback = execution.get();
    ASSERT_EQ(readback.size(), texels.size());
    for (size_t index = 0; index < texels.size(); ++index)
    {
        EXPECT_EQ(readback[index], texels[index]);
    }
}

/// Executes generated 3D texture DSL codegen using explicit RenderNode&, writeTexture, copyTexture, and ReadbackTextureNode.
TEST(GpuTaskGraphTests, GeneratedTexture3DGraphCopiesAndReadsBackVolume)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
    eastl::vector<uint8_t> texels;
    texels.reserve(2u * 2u * 2u * 4u);
    for (uint32_t index = 0; index < 32u; ++index)
    {
        texels.push_back(static_cast<uint8_t>(index * 7u + 3u));
    }
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildTexture3DReadbackGraph, texels);
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    const auto readback = execution.get();
    ASSERT_EQ(readback.size(), texels.size());
    for (size_t index = 0; index < texels.size(); ++index)
    {
        EXPECT_EQ(readback[index], texels[index]);
    }
}

/// Executes multi-subpass pixelLocalPass with a transient pixel-local attachment inside a generated TaskGraph RenderNode.
TEST(GpuTaskGraphTests, GeneratedPixelLocalGraphResolvesTransientAttachment)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
    constexpr uint32_t extent = 4u;
    auto execution = runtime.submit(GpuGraphFixture::Entries::buildPixelLocalGraph, extent);
    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    const auto readback = execution.get();
    ASSERT_EQ(readback.size(), extent * extent * 4u);
    for (uint32_t pixel = 0; pixel < extent * extent; ++pixel)
    {
        const size_t offset = pixel * 4u;
        EXPECT_NEAR(readback[offset + 0u], 191, 2);
        EXPECT_NEAR(readback[offset + 1u], 191, 2);
        EXPECT_NEAR(readback[offset + 2u], 64, 2);
        EXPECT_EQ(readback[offset + 3u], 255u);
    }
}

/// Verifies RenderNode::waitFor rejects invalid submissions, orders attached passes, and flushes unattached waits in finish().
TEST(GpuTaskGraphTests, RenderNodeWaitForSynchronizesExternalSubmissionsAndFlushesUnattachedWaits)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    auto *mainQueue = device->getMainQueue();
    auto *auxiliaryQueue = device->getQueue({.prefer = QueuePreference::Compute});
    auto source = device->createBuffer({
        .label = "WaitForExternalSrc",
        .usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
        .size = sizeof(uint32_t),
    });

    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});

        auto invalidExec = runtime.submit([](uint32_t value)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(InvalidWaitForNode{}, value);
        }, 7u);
        ASSERT_TRUE(invalidExec.wait(10'000'000'000ull));
        EXPECT_EQ(invalidExec.status(), GVM::Core::ExecutionStatus::Failed);
        EXPECT_THROW(invalidExec.get(), std::invalid_argument);

        const uint32_t zero = 0u;
        mainQueue->writeBuffer(source, &zero, sizeof(zero));
        ASSERT_TRUE(mainQueue->submit({}, {}).wait(5'000'000'000ull));

        const uint32_t sentinel = 0x12345678u;
        auxiliaryQueue->writeBuffer(source, &sentinel, sizeof(sentinel));
        auto attachedDep = auxiliaryQueue->submit({}, {});
        auto attachedExec = runtime.submit([attachedDep](Buffer externalBuffer)
        {
            GVM::Core::TaskGraph graph;
            auto copied = graph.add(AttachedWaitForNode(attachedDep), externalBuffer);
            return graph.add(GVM::Core::ReadbackNode<uint32_t>{}, copied);
        }, source);
        ASSERT_TRUE(attachedExec.wait(10'000'000'000ull));
        ASSERT_EQ(attachedExec.status(), GVM::Core::ExecutionStatus::Succeeded) << attachedExec.error().c_str();
        const auto observed = attachedExec.get();
        ASSERT_EQ(observed.size(), 1u);
        EXPECT_EQ(observed[0], sentinel);

        const uint32_t nextSentinel = 0x55aa33cclu;
        auxiliaryQueue->writeBuffer(source, &nextSentinel, sizeof(nextSentinel));
        auto unattachedDep = auxiliaryQueue->submit({}, {});
        auto unattachedExec = runtime.submit([unattachedDep](uint32_t value)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(UnattachedWaitForNode(unattachedDep), value);
        }, 41u);
        ASSERT_TRUE(unattachedExec.wait(10'000'000'000ull));
        ASSERT_EQ(unattachedExec.status(), GVM::Core::ExecutionStatus::Succeeded) << unattachedExec.error().c_str();
        EXPECT_EQ(unattachedExec.get(), 42u);
        EXPECT_TRUE(unattachedDep.isComplete());
    }

    device->freeBuffer(source);
    mainQueue->submit({});
}

/// Validates RenderNode::renderToSwapchain input checks and verifies generated SwapchainPresentGraph entry compilation.
TEST(GpuTaskGraphTests, RenderToSwapchainValidatesInputsAndGeneratedEntryCompiles)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    auto texture = device->createTexture({
        .label = "SwapchainValidationSrc",
        .usage = TextureUsage::RenderAttachment | TextureUsage::TextureBinding,
        .size = {2u, 2u, 1u},
        .format = TextureFormat::RGBA8Unorm,
    });

    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});

        auto missingSwapchain = runtime.submit([texture](uint32_t value)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(InvalidSwapchainRenderNode(SwapchainQueryResult{}, texture), value);
        }, 1u);
        ASSERT_TRUE(missingSwapchain.wait(10'000'000'000ull));
        EXPECT_EQ(missingSwapchain.status(), GVM::Core::ExecutionStatus::Failed);
        EXPECT_THROW(missingSwapchain.get(), std::invalid_argument);

        SwapchainQueryResult fakeTarget;
        fakeTarget.status = SwapchainNextTextureQueryStatus::Success;
        fakeTarget.texture = texture;
        auto missingSource = runtime.submit([fakeTarget](uint32_t value)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(InvalidSwapchainRenderNode(fakeTarget, Texture{}), value);
        }, 1u);
        ASSERT_TRUE(missingSource.wait(10'000'000'000ull));
        EXPECT_EQ(missingSource.status(), GVM::Core::ExecutionStatus::Failed);
        EXPECT_THROW(missingSource.get(), std::invalid_argument);

        GpuGraphFixture::SwapchainPresentInput request;
        request.swapchainTarget = SwapchainQueryResult{};
        request.input = GpuGraphFixture::Input{};
        auto generatedExec = runtime.submit(GpuGraphFixture::Entries::buildSwapchainPresentGraph, request);
        ASSERT_TRUE(generatedExec.wait(10'000'000'000ull));
        EXPECT_EQ(generatedExec.status(), GVM::Core::ExecutionStatus::Failed);
        EXPECT_THROW(generatedExec.get(), std::invalid_argument);
    }

    device->freeTexture(texture);
    device->getMainQueue()->submit({});
}

namespace
{
    struct SharedBufferReadNode final : public GVM::Core::IRenderNode<GVM::RHI::Buffer, GVM::RHI::Buffer>
    {
        eastl::shared_ptr<GpuGraphRecordingProbe> holdProbe;

        explicit SharedBufferReadNode(eastl::shared_ptr<GpuGraphRecordingProbe> probe = {})
            : holdProbe(eastl::move(probe))
        {
        }

        GVM::RHI::Buffer build(GVM::Core::RenderNode &node, GVM::RHI::Buffer sharedSource) const
        {
            auto dst = node.createBuffer({
                .label = "SharedBufferReadDst",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
                .size = sizeof(uint32_t),
            });
            node.copyBuffer(sharedSource, dst);
            if (holdProbe)
            {
                GVM::Core::ComputePassTaskDescriptor task;
                task.resources.complete = true;
                task.dispatchFn = [probe = holdProbe](GVM::RHI::ComputePassEncoder)
                {
                    std::unique_lock lock(probe->mutex);
                    ++probe->started;
                    probe->changed.notify_all();
                    if (!probe->changed.wait_for(lock, std::chrono::seconds(5), [&probe] { return probe->released; }))
                    {
                        throw std::runtime_error("Held first RAR GpuLaunchGroup timed out waiting for concurrent reader.");
                    }
                };
                node.computePass("HoldFirstRarLaunchGroup", eastl::move(task));
            }
            return dst;
        }
    };

    struct GatedSharedBufferReadNode final : public GVM::Core::IRenderNode<uint32_t, GVM::RHI::Buffer>
    {
        GVM::RHI::Buffer sharedSource;

        explicit GatedSharedBufferReadNode(GVM::RHI::Buffer source)
            : sharedSource(eastl::move(source))
        {
        }

        GVM::RHI::Buffer build(GVM::Core::RenderNode &node, uint32_t) const
        {
            auto dst = node.createBuffer({
                .label = "GatedSharedBufferReadDst",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
                .size = sizeof(uint32_t),
            });
            node.copyBuffer(sharedSource, dst);
            return dst;
        }
    };
} // namespace

/// Launches a closed ReadbackNode subgraph immediately even while an unrelated RenderNode::build() is still blocked.
TEST(GpuTaskGraphTests, EagerReadbackLaunchBypassesSlowUnrelatedRenderBuild)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::TaskRuntime runtime(*device, {.workerCount = 3});

    auto buildGate = eastl::make_shared<GpuGraphRecordingProbe>();
    buildGate->released = false;
    buildGate->observeBuild = true;

    auto execution = runtime.submit([buildGate](GpuGraphFixture::Input input)
    {
        GVM::Core::TaskGraph graph;
        auto slowGpuBuild = graph.add(GpuGraphTestNode(buildGate), 0u);

        auto values = graph.add([buildGate](GpuGraphFixture::Input in)
        {
            std::unique_lock lock(buildGate->mutex);
            if (!buildGate->changed.wait_for(lock, std::chrono::seconds(5), [&buildGate] { return buildGate->started >= 1u; }))
            {
                throw std::runtime_error("Slow RenderNode::build() did not start in time.");
            }
            return GpuGraphFixture::generate(in);
        }, input);
        auto uploaded = graph.add(GpuGraphFixture::Upload{}, values);
        auto filtered = graph.add(GpuGraphFixture::Filter{}, uploaded);
        auto readback = graph.add(GVM::Core::ReadbackNode<float>{}, filtered);
        auto fastSum = graph.add([buildGate](eastl::vector<float> out)
        {
            {
                std::lock_guard lock(buildGate->mutex);
                buildGate->released = true;
            }
            buildGate->changed.notify_all();
            return GpuGraphFixture::sum(out);
        }, readback);

        return graph.add([](uint32_t, float s) { return s; }, slowGpuBuild, fastSum);
    }, GpuGraphFixture::Input{});

    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_FLOAT_EQ(execution.get(), 446.5f);
}

/// Allows concurrent GpuLaunchGroups to share a read-only Buffer (RAR) while an earlier reader group is still in flight.
TEST(GpuTaskGraphTests, ConcurrentGpuLaunchGroupsShareReadOnlyBufferWithoutBlocking)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    auto *mainQueue = device->getMainQueue();
    auto sharedBuffer = device->createBuffer({
        .label = "SharedReadOnlyBuffer",
        .usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
        .size = sizeof(uint32_t),
    });
    const uint32_t expected = 0x2468ace0u;
    mainQueue->writeBuffer(sharedBuffer, &expected, sizeof(expected));
    ASSERT_TRUE(mainQueue->submit({}, {}).wait(5'000'000'000ull));

    auto holdGroup1 = eastl::make_shared<GpuGraphRecordingProbe>();
    holdGroup1->released = false;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 3});
        auto execution = runtime.submit([holdGroup1](Buffer sharedSrc)
        {
            GVM::Core::TaskGraph graph;
            auto copy1 = graph.add(SharedBufferReadNode(holdGroup1), sharedSrc);
            auto readback1 = graph.add(GVM::Core::ReadbackNode<uint32_t>{}, copy1);

            // Delay the second reader's build until Group 1 has already launched and started recording.
            auto gateToken = graph.add([holdGroup1](uint32_t token)
            {
                std::unique_lock lock(holdGroup1->mutex);
                if (!holdGroup1->changed.wait_for(lock, std::chrono::seconds(5), [&holdGroup1] { return holdGroup1->started >= 1u; }))
                {
                    throw std::runtime_error("First RAR GpuLaunchGroup did not start recording in time.");
                }
                return token;
            }, 1u);
            auto copy2 = graph.add(GatedSharedBufferReadNode{sharedSrc}, gateToken);
            auto readback2 = graph.add(GVM::Core::ReadbackNode<uint32_t>{}, copy2);
            auto resolved2 = graph.add([holdGroup1](eastl::vector<uint32_t> vals)
            {
                {
                    std::lock_guard lock(holdGroup1->mutex);
                    holdGroup1->released = true;
                }
                holdGroup1->changed.notify_all();
                return vals.empty() ? 0u : vals[0];
            }, readback2);

            return graph.add([](eastl::vector<uint32_t> v1, uint32_t v2)
            {
                return (v1.empty() ? 0u : v1[0]) ^ v2;
            }, readback1, resolved2);
        }, sharedBuffer);

        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_EQ(execution.get(), 0u);
    }

    device->freeBuffer(sharedBuffer);
    mainQueue->submit({});
}

/// Prevents unsubmitted batches on a full graphics queue window from starving independent transfer batches beyond maxLookaheadBatches.
TEST(GpuTaskGraphTests, PerQueueSlidingWindowAvoidsCrossQueueHeadOfLineBlocking)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    auto *graphics = device->getMainQueue();
    auto *transfer = device->getQueue({.prefer = QueuePreference::Transfer});
    if (transfer == graphics)
    {
        GTEST_SKIP() << "Two distinct queues are required to test cross-queue window independence.";
    }

    auto target = device->createTexture({
        .usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc,
        .size = {1, 1, 1},
        .format = TextureFormat::RGBA8Unorm,
    });
    auto held = eastl::make_shared<GpuGraphRecordingProbe>();
    held->released = false;
    held->gatedPass = 0u;
    {
        // With workerCount = 2 and maximumPassesPerBatch = 1, maxLookaheadBatches = 4.
        // Emit 8 graphics render batches (exceeding maxLookaheadBatches = 4 while Batch 0 is held)
        // plus an independent transfer upload+readback chain in the same GpuLaunchGroup.
        GVM::Core::TaskRuntime runtime(*device, {
            .workerCount = 2,
            .maximumPassesPerBatch = 1,
        });
        auto execution = runtime.submit([held, view = target->createView()](GpuGraphFixture::Input input)
        {
            GVM::Core::TaskGraph graph;
            auto graphicsHeavy = graph.add(GpuGraphTestNode(held, false, view), 15u);
            auto values = graph.add(GpuGraphFixture::generate, input);
            auto buffer = graph.add(GpuGraphFixture::Upload{}, values);
            auto copied = graph.add(GVM::Core::ReadbackNode<float>{}, buffer);
            auto unblocker = graph.add([held](eastl::vector<float> result)
            {
                {
                    std::lock_guard lock(held->mutex);
                    held->released = true;
                }
                held->changed.notify_all();
                return GpuGraphFixture::sum(result);
            }, copied);
            return graph.add([](uint32_t, float total) { return total; }, graphicsHeavy, unblocker);
        }, GpuGraphFixture::Input{});

        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_FLOAT_EQ(execution.get(), 127.5f);
    }

    device->freeTexture(target);
    graphics->submit({});
}

/// Fuses sequential conflicting compute dispatches into one GraphPass while keeping independent dispatches parallel.
TEST(GpuTaskGraphTests, ComputePassFusesSequentialHazardsAndSplitsIndependentTasks)
{
    using namespace GVM::RHI;
    struct FusedAndSplitComputeNode final : GVM::Core::IRenderNode<uint32_t, uint32_t>
    {
        eastl::shared_ptr<GpuGraphRecordingProbe> probe;
        explicit FusedAndSplitComputeNode(eastl::shared_ptr<GpuGraphRecordingProbe> p) : probe(eastl::move(p)) {}

        uint32_t build(GVM::Core::RenderNode &node, uint32_t seed) const
        {
            auto bufA = node.createBuffer({.usage = BufferUsage::Storage, .size = 16});
            auto bufB = node.createBuffer({.usage = BufferUsage::Storage, .size = 16});
            auto makeTask = [&](const Buffer &buf, bool isWrite, bool gateRecording)
            {
                GVM::Core::ComputePassTaskDescriptor task;
                task.resources.complete = true;
                task.resources.accesses.push_back({buf.get(), 0u, 16u, isWrite});
                task.dispatchFn = [p = probe, gateRecording](ComputePassEncoder)
                {
                    std::unique_lock lock(p->mutex);
                    ++p->started;
                    if (gateRecording)
                    {
                        ++p->active;
                        p->maximumActive = eastl::max(p->maximumActive, p->active);
                        p->changed.notify_all();
                        p->changed.wait(lock, [&p] { return p->released; });
                        --p->active;
                    }
                };
                return task;
            };
            // Three sequential RAW/WAR/WAW tasks on bufA fuse into Pass 0; independent bufB splits into Pass 1.
            node.computePass("FuseAndSplit", {
                makeTask(bufA, true, true),
                makeTask(bufA, false, false),
                makeTask(bufA, true, false),
                makeTask(bufB, true, true),
            });
            return seed + 7u;
        }
    };

    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    auto *device = instance->createDevice();
    GVM::Core::ThreadExecutor executor(2);
    GVM::Core::TaskRuntime runtime(*device, executor, {.maximumPassesPerExecution = 2});
    auto probe = eastl::make_shared<GpuGraphRecordingProbe>();
    probe->released = false;

    auto execution = runtime.submit([probe](uint32_t seed)
    {
        GVM::Core::TaskGraph graph;
        return graph.add(FusedAndSplitComputeNode(probe), seed);
    }, 35u);

    {
        std::unique_lock lock(probe->mutex);
        EXPECT_TRUE(probe->changed.wait_for(lock, std::chrono::seconds(3), [&probe] { return probe->active == 2u; }));
        EXPECT_EQ(probe->maximumActive, 2u);
        probe->released = true;
    }
    probe->changed.notify_all();

    ASSERT_TRUE(execution.wait(10'000'000'000ull));
    ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
    EXPECT_EQ(probe->started, 4u);
    // Non-GPU-exporting CPU outputs remain repeatable across get() calls.
    EXPECT_EQ(execution.get(), 42u);
    EXPECT_EQ(execution.get(), 42u);
}

/// Verifies opt-in cross-frame GPU pass timestamp feedback harvests non-blocking durations and updates learned pass weights.
TEST(GpuTaskGraphTests, PassTimestampFeedbackHarvestsCrossFrameDurationsWhenEnabled)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    auto *device = instance->createDevice();

    {
        GVM::Core::TaskRuntime disabledRuntime(*device, {.workerCount = 2});
        auto execution = disabledRuntime.submit(GpuGraphFixture::Entries::buildProfiledGraph, 256u);
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_GT(execution.get(), 0.0f);
        EXPECT_TRUE(disabledRuntime.passProfileSnapshot().empty());
    }

    GVM::Core::TaskRuntime feedbackRuntime(*device, {
        .workerCount = 2,
        .enablePassTimestampFeedback = true,
        .queueHandoffThresholdNs = 10'000u,
    });

    constexpr uint32_t kFrames = 4u;
    float referenceSum = 0.0f;
    for (uint32_t frame = 0u; frame < kFrames; ++frame)
    {
        auto execution = feedbackRuntime.submit(GpuGraphFixture::Entries::buildProfiledGraph, 32768u);
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        const float value = execution.get();
        if (frame == 0u)
        {
            referenceSum = value;
        }
        else
        {
            EXPECT_NEAR(value, referenceSum, 1e-2f);
        }
    }

    const auto support = device->getMainQueue()->getTimestampQuerySupport();
    const auto snapshot = feedbackRuntime.passProfileSnapshot();
    if (!support.supported)
    {
        EXPECT_TRUE(snapshot.empty());
        return;
    }

    auto findEntry = [&](const char *label) -> const GVM::Core::PassProfileFeedbackEntry *
    {
        for (const auto &entry : snapshot)
        {
            if (entry.label == label)
            {
                return &entry;
            }
        }
        return nullptr;
    };

    const auto *split0 = findEntry("SplitProfiled#0");
    const auto *split1 = findEntry("SplitProfiled#1");
    const auto *heavy = findEntry("HeavySmallDispatch");
    const auto *preview = findEntry("PreviewProfiled");
    const auto *combine = findEntry("CombineProfiled");
    ASSERT_NE(preview, nullptr);
    EXPECT_EQ(preview->sampleCount, kFrames);
    EXPECT_GT(preview->smoothedDurationNs, 0.0);
    EXPECT_GE(preview->learnedWeight, 1u);
    EXPECT_LE(preview->learnedWeight, 64u);

    // Native Metal always brackets compute encoders via MTLComputePassDescriptor; MoltenVK may collapse
    // compute vkCmdWriteTimestamp2 pairs on stage-boundary-only GPUs, in which case PassWeightProfiler
    // intentionally drops zero-tick samples to preserve static dispatch heuristics.
    if (instance->getBackend() == GVM::RHI::GraphicsBackend::Metal || heavy != nullptr)
    {
        ASSERT_NE(split0, nullptr);
        ASSERT_NE(split1, nullptr);
        ASSERT_NE(heavy, nullptr);
        ASSERT_NE(combine, nullptr);
        EXPECT_EQ(split0->sampleCount, kFrames);
        EXPECT_EQ(split1->sampleCount, kFrames);
        EXPECT_EQ(heavy->sampleCount, kFrames);
        EXPECT_EQ(combine->sampleCount, kFrames);
        EXPECT_GT(heavy->smoothedDurationNs, 0.0);
        EXPECT_GT(heavy->smoothedDurationNs, split0->smoothedDurationNs);
        EXPECT_GE(heavy->learnedWeight, 1u);
        EXPECT_LE(heavy->learnedWeight, 64u);
    }
}

GVM::Core::Execution<float> submitGeneratedGpuGraphForMarlTest(GVM::Core::TaskRuntime &runtime, float scale)
{
    GpuGraphFixture::Input input;
    input.scale = scale;
    return runtime.submit(GpuGraphFixture::Entries::buildGraph, input);
}

/// Executes a nested DSL GPU subgraph on a single worker without blocking, promoting child GPU buffers into the parent graph.
TEST(GpuTaskGraphTests, GeneratedNestedGpuSubgraphPromotesResourcesOnSingleWorker)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();
    {
        GVM::Core::ThreadExecutor executor(1);
        GVM::Core::TaskRuntime runtime(*device, executor);
        auto execution = runtime.submit(GpuGraphFixture::Entries::buildParentNestedGpuGraph, GpuGraphFixture::Input{});
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_FLOAT_EQ(execution.get(), 446.5f);
    }
}

namespace
{
    /// Emits two independent buffer copy passes after a single RenderNode::waitFor call.
    struct MultiPassWaitForNode final : public GVM::Core::IRenderNode<uint32_t, GVM::RHI::Buffer>
    {
        GVM::RHI::Submission dependency;
        GVM::RHI::Buffer externalSrcA;
        GVM::RHI::Buffer externalDstA;
        GVM::RHI::Buffer externalSrcB;

        /// Captures the external dependency and source/destination buffer handles.
        MultiPassWaitForNode(
            GVM::RHI::Submission dep,
            GVM::RHI::Buffer aSrc,
            GVM::RHI::Buffer aDst,
            GVM::RHI::Buffer bSrc)
            : dependency(eastl::move(dep))
            , externalSrcA(eastl::move(aSrc))
            , externalDstA(eastl::move(aDst))
            , externalSrcB(eastl::move(bSrc))
        {
        }

        /// Copies externalSrcA -> externalDstA and externalSrcB -> dstB under one waitFor barrier.
        GVM::RHI::Buffer build(GVM::Core::RenderNode &node, uint32_t) const
        {
            auto dstB = node.createBuffer({
                .label = "MultiWaitForDstB",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
                .size = sizeof(uint32_t),
            });
            node.waitFor(dependency);
            node.copyBuffer(externalSrcA, externalDstA);
            node.copyBuffer(externalSrcB, dstB);
            return dstB;
        }
    };

    /// Synchronizes two concurrent GPU launch groups operating on disjoint byte ranges of one buffer.
    struct SubrangeLaunchGroupGate
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool firstRecordingEntered = false;
        bool secondGroupCompleted = false;
    };

    /// Holds Launch Group 1 inside pass recording while declaring a write to [0, 16) of a shared buffer.
    struct GatedFirstHalfBufferNode final : public GVM::Core::IRenderNode<uint32_t, uint32_t>
    {
        GVM::RHI::BufferRange targetRange;
        eastl::shared_ptr<SubrangeLaunchGroupGate> gate;

        /// Binds the [0, 16) byte subrange and synchronization gate.
        GatedFirstHalfBufferNode(GVM::RHI::BufferRange range, eastl::shared_ptr<SubrangeLaunchGroupGate> syncGate)
            : targetRange(eastl::move(range))
            , gate(eastl::move(syncGate))
        {
        }

        /// Emits one compute pass writing targetRange that waits for Launch Group 2 to complete on a disjoint range.
        uint32_t build(GVM::Core::RenderNode &node, uint32_t value) const
        {
            GVM::Core::ComputePassTaskDescriptor task;
            task.resources.complete = true;
            task.resources.accesses.push_back(GVM::Core::Private::PassResourceAccess::buffer(targetRange, true));
            task.dispatchFn = [syncGate = gate](GVM::RHI::ComputePassEncoder)
            {
                std::unique_lock lock(syncGate->mutex);
                syncGate->firstRecordingEntered = true;
                syncGate->cv.notify_all();
                if (!syncGate->cv.wait_for(lock, std::chrono::seconds(5), [&syncGate] { return syncGate->secondGroupCompleted; }))
                {
                    throw std::runtime_error("Disjoint buffer subrange launch group was blocked by active sibling group.");
                }
            };
            node.computePass("GatedFirstHalfCompute", eastl::move(task));
            return value;
        }
    };

    /// Uploads a uint32_t payload into a specific byte subrange of a shared buffer in Launch Group 2.
    struct SecondHalfBufferUploadNode final : public GVM::Core::IRenderNode<uint32_t, GVM::RHI::BufferRange>
    {
        GVM::RHI::BufferRange targetRange;

        /// Captures the target subrange written by Launch Group 2.
        explicit SecondHalfBufferUploadNode(GVM::RHI::BufferRange range)
            : targetRange(eastl::move(range))
        {
        }

        /// Writes four copies of sentinel into targetRange and returns that subrange for readback.
        GVM::RHI::BufferRange build(GVM::Core::RenderNode &node, uint32_t sentinel) const
        {
            const eastl::vector<uint32_t> values = {sentinel, sentinel + 1u, sentinel + 2u, sentinel + 3u};
            node.writeBuffer(targetRange, values);
            return targetRange;
        }
    };

    /// Attaches an external GPU submission wait without any resource accesses and forwards a WholeSize BufferRange.
    struct UnattachedWaitForwardBufferNode final : public GVM::Core::IRenderNode<uint32_t, GVM::RHI::BufferRange>
    {
        GVM::RHI::Submission externalSubmission;
        GVM::RHI::Buffer sourceBuffer;

        /// Captures the external dependency and external buffer handle.
        UnattachedWaitForwardBufferNode(GVM::RHI::Submission submission, GVM::RHI::Buffer buffer)
            : externalSubmission(eastl::move(submission))
            , sourceBuffer(eastl::move(buffer))
        {
        }

        /// Records only waitFor(externalSubmission) and returns a default WholeSize BufferRange.
        GVM::RHI::BufferRange build(GVM::Core::RenderNode &node, uint32_t) const
        {
            node.waitFor(externalSubmission);
            GVM::RHI::BufferRange wholeRange;
            wholeRange.buffer = sourceBuffer;
            return wholeRange;
        }
    };

    /// Emits many sequential fillBuffer passes inside one RenderNode to exceed a 64-command-buffer queue limit.
    struct ManyFillPassesNode final : public GVM::Core::IRenderNode<uint32_t, GVM::RHI::BufferRange>
    {
        uint32_t passCount = 96u;

        /// Constructs a fill-pass sequence node with the requested pass count.
        explicit ManyFillPassesNode(uint32_t count = 96u) noexcept
            : passCount(count)
        {
        }

        /// Allocates a 4-byte buffer and records passCount sequential fillBuffer passes.
        GVM::RHI::BufferRange build(GVM::Core::RenderNode &node, uint32_t) const
        {
            auto target = node.createBuffer({
                .label = "ManyFillTarget",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
                .size = sizeof(uint32_t),
            });
            for (uint32_t i = 0u; i < passCount; ++i)
            {
                node.fillBuffer({target, 0u, sizeof(uint32_t)}, static_cast<uint8_t>((i + 1u) & 0x7Fu));
            }
            return {target, 0u, sizeof(uint32_t)};
        }
    };

    /// Records a render pass with caller-supplied PassTimestampWrites and resolves the caller query set.
    struct ExplicitTimestampRenderNode final : public GVM::Core::IRenderNode<uint32_t, GVM::RHI::BufferRange>
    {
        GVM::RHI::QuerySet callerQuerySet;

        /// Captures the caller's timestamp QuerySet.
        explicit ExplicitTimestampRenderNode(GVM::RHI::QuerySet querySet)
            : callerQuerySet(eastl::move(querySet))
        {
        }

        /// Clears a transient color target with explicit timestampWrites and returns a dummy output buffer.
        GVM::RHI::BufferRange build(GVM::Core::RenderNode &node, uint32_t) const
        {
            auto color = node.createTexture({
                .label = "ExplicitTsColor",
                .usage = GVM::RHI::TextureUsage::RenderAttachment,
                .dimension = GVM::RHI::TextureDimension::e2D,
                .size = {4u, 4u, 1u},
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            });
            GVM::RHI::RenderPassDescriptor desc;
            desc.label = "CallerTimedRenderPass";
            GVM::RHI::RenderPassColorAttachment att;
            att.view = node.view(color);
            att.loadOp = GVM::RHI::LoadOp::Clear;
            att.storeOp = GVM::RHI::StoreOp::Store;
            desc.colorAttachments = {att};
            desc.timestampWrites = {
                .querySet = callerQuerySet,
                .beginningOfPassWriteIndex = 0u,
                .endOfPassWriteIndex = 1u,
            };
            node.renderPass(desc, {});
            auto dummy = node.createBuffer({
                .label = "ExplicitTsDummy",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
                .size = sizeof(uint32_t),
            });
            node.fillBuffer({dummy, 0u, sizeof(uint32_t)}, 0x2Au);
            return {dummy, 0u, sizeof(uint32_t)};
        }
    };

    /// Uploads a Depth32Float texture, copies through an intermediate depth texture, and outputs a Depth32Float texture for readback.
    struct DepthTransferCopyNode final : public GVM::Core::IRenderNode<float, GVM::RHI::Texture>
    {
        GVM::RHI::TextureFormat intermediateFormat = GVM::RHI::TextureFormat::Depth32Float;

        /// Configures the intermediate depth format used by the texture-to-texture copy stage.
        explicit DepthTransferCopyNode(GVM::RHI::TextureFormat intermediate) noexcept
            : intermediateFormat(intermediate)
        {
        }

        /// Uploads four depth texels, copies across intermediateFormat, and copies back into a host-readable Depth32Float texture.
        GVM::RHI::Texture build(GVM::Core::RenderNode &node, float clearDepth) const
        {
            const float depthPixels[4] = {clearDepth, clearDepth * 0.5f, 0.75f, 1.0f};
            auto srcDepth = node.createTexture({
                .label = "DepthUploadSrc",
                .usage = GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc,
                .dimension = GVM::RHI::TextureDimension::e2D,
                .size = {2u, 2u, 1u},
                .format = GVM::RHI::TextureFormat::Depth32Float,
            });
            auto midDepth = node.createTexture({
                .label = "DepthCopyMid",
                .usage = GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc,
                .dimension = GVM::RHI::TextureDimension::e2D,
                .size = {2u, 2u, 1u},
                .format = intermediateFormat,
            });
            auto outDepth = node.createTexture({
                .label = "DepthReadbackOut",
                .usage = GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc,
                .dimension = GVM::RHI::TextureDimension::e2D,
                .size = {2u, 2u, 1u},
                .format = GVM::RHI::TextureFormat::Depth32Float,
            });
            node.writeTexture(srcDepth, depthPixels, sizeof(depthPixels));
            node.copyTexture(
                {.texture = srcDepth, .aspect = GVM::RHI::TextureAspect::DepthOnly},
                {.texture = midDepth, .aspect = GVM::RHI::TextureAspect::DepthOnly},
                {2u, 2u, 1u});
            node.copyTexture(
                {.texture = midDepth, .aspect = GVM::RHI::TextureAspect::DepthOnly},
                {.texture = outDepth, .aspect = GVM::RHI::TextureAspect::DepthOnly},
                {2u, 2u, 1u});
            return outDepth;
        }
    };

    /// Wraps an exported RHI::Buffer with a copy/move constructor that can be configured to throw before succeeding.
    struct ThrowingCopyableGpuOutput
    {
        GVM::RHI::Buffer buffer;
        std::shared_ptr<std::atomic<bool>> shouldThrow;

        ThrowingCopyableGpuOutput() = default;
        ThrowingCopyableGpuOutput(GVM::RHI::Buffer buf, std::shared_ptr<std::atomic<bool>> flag)
            : buffer(eastl::move(buf))
            , shouldThrow(std::move(flag))
        {
        }
        ThrowingCopyableGpuOutput(const ThrowingCopyableGpuOutput &other)
            : buffer(other.buffer)
            , shouldThrow(other.shouldThrow)
        {
            if (shouldThrow && shouldThrow->load(std::memory_order_acquire))
            {
                throw std::runtime_error("Simulated copy constructor failure during Execution::get.");
            }
        }
        ThrowingCopyableGpuOutput(ThrowingCopyableGpuOutput &&other) noexcept(false)
            : buffer(other.buffer)
            , shouldThrow(other.shouldThrow)
        {
            if (shouldThrow && shouldThrow->load(std::memory_order_acquire))
            {
                throw std::runtime_error("Simulated move constructor failure during Execution::get.");
            }
        }
        ThrowingCopyableGpuOutput &operator=(const ThrowingCopyableGpuOutput &) = default;
        ThrowingCopyableGpuOutput &operator=(ThrowingCopyableGpuOutput &&) = default;
    };

    /// Produces a ThrowingCopyableGpuOutput containing one graph-allocated GPU buffer.
    struct ThrowingGpuOutputProducerNode final : public GVM::Core::IRenderNode<uint32_t, ThrowingCopyableGpuOutput>
    {
        std::shared_ptr<std::atomic<bool>> shouldThrow;

        /// Captures the shared throw control flag.
        explicit ThrowingGpuOutputProducerNode(std::shared_ptr<std::atomic<bool>> flag)
            : shouldThrow(std::move(flag))
        {
        }

        /// Allocates a 16-byte buffer and wraps it in ThrowingCopyableGpuOutput.
        ThrowingCopyableGpuOutput build(GVM::Core::RenderNode &node, uint32_t) const
        {
            auto buf = node.createBuffer({
                .label = "ThrowingExportBuffer",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
                .size = 16u,
            });
            node.fillBuffer({buf, 0u, 16u}, 0x11u);
            return ThrowingCopyableGpuOutput(buf, shouldThrow);
        }
    };
} // namespace

namespace GVM::Core
{
    /// Registers ThrowingCopyableGpuOutput as an exported GPU resource carrier for single-claim ownership testing.
    template <>
    struct GraphResourceTraits<ThrowingCopyableGpuOutput>
    {
        static constexpr bool containsResources = true;

        /// Visits the wrapped RHI::Buffer handle.
        template <class Visitor>
        static void visit(const ThrowingCopyableGpuOutput &value, const Visitor &visitor)
        {
            visitor(value.buffer.get());
        }
    };
} // namespace GVM::Core

/// Preserves RenderNode::waitFor dependencies across all independent passes emitted inside the same RenderNode.
TEST(GpuTaskGraphTests, RenderNodeWaitForAppliesToAllIndependentPassesInSameNode)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();
    auto *mainQueue = device->getMainQueue();
    auto *auxiliaryQueue = device->getQueue({.prefer = QueuePreference::Compute});

    auto srcA = device->createBuffer({
        .label = "MultiWaitForSrcA",
        .usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
        .size = sizeof(uint32_t),
    });
    auto srcB = device->createBuffer({
        .label = "MultiWaitForSrcB",
        .usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
        .size = sizeof(uint32_t),
    });
    auto dstA = device->createBuffer({
        .label = "MultiWaitForExternalDstA",
        .usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
        .size = sizeof(uint32_t),
    });

    const uint32_t zero = 0u;
    mainQueue->writeBuffer(srcA, &zero, sizeof(zero));
    mainQueue->writeBuffer(srcB, &zero, sizeof(zero));
    mainQueue->writeBuffer(dstA, &zero, sizeof(zero));
    ASSERT_TRUE(mainQueue->submit({}, {}).wait(5'000'000'000ull));

    const uint32_t sentinelA = 0x11223344u;
    const uint32_t sentinelB = 0x99aabbccu;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2, .maximumPassesPerBatch = 1});
        auxiliaryQueue->writeBuffer(srcA, &sentinelA, sizeof(sentinelA));
        auxiliaryQueue->writeBuffer(srcB, &sentinelB, sizeof(sentinelB));
        auto externalDep = auxiliaryQueue->submit({}, {});

        auto execution = runtime.submit([externalDep, srcA, dstA, srcB](uint32_t seed)
        {
            GVM::Core::TaskGraph graph;
            auto secondCopy = graph.add(MultiPassWaitForNode(externalDep, srcA, dstA, srcB), seed);
            return graph.add(GVM::Core::ReadbackNode<uint32_t>{}, secondCopy);
        }, 1u);

        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        const auto observedB = execution.get();
        ASSERT_EQ(observedB.size(), 1u);
        EXPECT_EQ(observedB[0], sentinelB);
    }

    auto readA = mainQueue->readBufferAsync(dstA);
    ASSERT_TRUE(mainQueue->submit({}, {}).wait(5'000'000'000ull));
    readA->wait();
    uint32_t observedA = 0u;
    ASSERT_EQ(readA->getData().size(), sizeof(uint32_t));
    std::memcpy(&observedA, readA->getData().data(), sizeof(uint32_t));
    EXPECT_EQ(observedA, sentinelA);

    device->freeBuffer(srcA);
    device->freeBuffer(srcB);
    device->freeBuffer(dstA);
}

/// Allows a second GPU launch group to execute and read back a disjoint byte range of a buffer
/// while an earlier launch group writing another range of the same buffer is still active.
TEST(GpuTaskGraphTests, DisjointBufferRangesLaunchConcurrentlyAcrossGpuLaunchGroups)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();

    auto sharedBuffer = device->createBuffer({
        .label = "SharedMegaBuffer",
        .usage = BufferUsage::Storage | BufferUsage::CopyDst | BufferUsage::CopySrc,
        .size = 32u,
    });
    auto gate = eastl::make_shared<SubrangeLaunchGroupGate>();

    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 3});
        auto execution = runtime.submit([sharedBuffer, gate](uint32_t sentinel)
        {
            GVM::Core::TaskGraph graph;
            auto group1 = graph.add(GatedFirstHalfBufferNode({sharedBuffer, 0u, 16u}, gate), 1u);
            auto waitGroup1Recording = graph.add([gate, sentinel]()
            {
                std::unique_lock lock(gate->mutex);
                if (!gate->cv.wait_for(lock, std::chrono::seconds(5), [&gate] { return gate->firstRecordingEntered; }))
                {
                    throw std::runtime_error("First GPU launch group never entered recording.");
                }
                return sentinel;
            });
            auto secondRange = graph.add(SecondHalfBufferUploadNode({sharedBuffer, 16u, 16u}), waitGroup1Recording);
            auto readSecond = graph.add(GVM::Core::ReadbackNode<uint32_t>{}, secondRange);
            auto unblocked = graph.add([gate](eastl::vector<uint32_t> values)
            {
                {
                    std::lock_guard lock(gate->mutex);
                    gate->secondGroupCompleted = true;
                }
                gate->cv.notify_all();
                return values.empty() ? 0u : values[0];
            }, readSecond);
            return graph.add([](uint32_t a, uint32_t b) { return a + b; }, group1, unblocked);
        }, 0x4000'0000u);

        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_EQ(execution.get(), 0x4000'0001u);
    }

    device->freeBuffer(sharedBuffer);
}

/// Drains asynchronous GPU batch completion closures before TaskRuntime teardown releases GraphDevice.
TEST(GpuTaskGraphTests, RepeatedRuntimeTeardownDrainsCompletionCallbacksBeforeDeviceRelease)
{
    for (uint32_t iter = 0; iter < 12; ++iter)
    {
        eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
            GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
        ASSERT_NE(instance, nullptr);
        auto *device = instance->createDevice();
        {
            GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
            auto execution = runtime.submit(GpuGraphFixture::Entries::buildGraph, GpuGraphFixture::Input{});
            ASSERT_TRUE(execution.wait(10'000'000'000ull));
            ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
            EXPECT_FLOAT_EQ(execution.get(), 446.5f);
        }
    }
}

/// Orders an unattached external wait pass strictly before a downstream WholeSize ReadbackNode in the same launch group.
TEST(GpuTaskGraphTests, UnattachedExternalWaitOrdersBeforeDownstreamWholeSizeReadback)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();
    auto *mainQueue = device->getMainQueue();
    auto *auxQueue = device->getQueue({.prefer = QueuePreference::Compute});

    auto externalBuf = device->createBuffer({
        .label = "UnattachedWaitDownstreamBuf",
        .usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
        .size = sizeof(uint32_t) * 2u,
    });
    const uint32_t zeros[2] = {0u, 0u};
    mainQueue->writeBuffer(externalBuf, zeros, sizeof(zeros));
    ASSERT_TRUE(mainQueue->submit({}, {}).wait(5'000'000'000ull));

    const uint32_t expected[2] = {0xCAFEBABEu, 0x12345678u};
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        auxQueue->writeBuffer(externalBuf, expected, sizeof(expected));
        auto externalSub = auxQueue->submit({}, {});

        auto execution = runtime.submit([externalSub, externalBuf](uint32_t seed)
        {
            GVM::Core::TaskGraph graph;
            auto waited = graph.add(UnattachedWaitForwardBufferNode(externalSub, externalBuf), seed);
            return graph.add(GVM::Core::ReadbackNode<uint32_t>{}, waited);
        }, 1u);

        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        const auto observed = execution.get();
        ASSERT_EQ(observed.size(), 2u);
        EXPECT_EQ(observed[0], expected[0]);
        EXPECT_EQ(observed[1], expected[1]);
    }

    device->freeBuffer(externalBuf);
}

/// Serializes conflicting GPU launch groups across parallel sibling nested subgraphs sharing an external buffer.
TEST(GpuTaskGraphTests, SiblingNestedGpuSubgraphsSerializeConflictingBufferAccesses)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();

    auto sharedBuf = device->createBuffer({
        .label = "SiblingSharedConflictBuf",
        .usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
        .size = 16u,
    });

    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 4});
        auto execution = runtime.submit([sharedBuf](uint32_t base)
        {
            GVM::Core::TaskGraph root;
            auto subA = root.add([sharedBuf](GVM::Core::TaskGraph &sub, uint32_t v)
            {
                auto written = sub.add(SecondHalfBufferUploadNode({sharedBuf, 0u, 16u}), v);
                auto read = sub.add(GVM::Core::ReadbackNode<uint32_t>{}, written);
                return sub.add([](eastl::vector<uint32_t> vals) { return vals[0]; }, read);
            }, base);
            auto subB = root.add([sharedBuf](GVM::Core::TaskGraph &sub, uint32_t v)
            {
                auto written = sub.add(SecondHalfBufferUploadNode({sharedBuf, 0u, 16u}), v);
                auto read = sub.add(GVM::Core::ReadbackNode<uint32_t>{}, written);
                return sub.add([](eastl::vector<uint32_t> vals) { return vals[0]; }, read);
            }, base + 100u);
            return root.add([](uint32_t a, uint32_t b) { return a + b; }, subA, subB);
        }, 10u);

        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        EXPECT_EQ(execution.get(), 120u);
    }

    device->freeBuffer(sharedBuf);
}

/// Allows a completed CPU-scalar Execution handle to outlive both TaskRuntime and RHI Device when profiler feedback is enabled.
TEST(GpuTaskGraphTests, CompletedScalarExecutionOutlivesRuntimeAndDeviceWithTimestampFeedback)
{
    eastl::optional<GVM::Core::Execution<float>> retainedExecution;
    {
        eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
            GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
        ASSERT_NE(instance, nullptr);
        auto *device = instance->createDevice();
        {
            GVM::Core::TaskRuntime runtime(*device, {
                .workerCount = 2,
                .enablePassTimestampFeedback = true,
            });
            retainedExecution.emplace(runtime.submit(GpuGraphFixture::Entries::buildProfiledGraph, 256u));
            ASSERT_TRUE(retainedExecution->wait(10'000'000'000ull));
            ASSERT_EQ(retainedExecution->status(), GVM::Core::ExecutionStatus::Succeeded)
                << retainedExecution->error().c_str();
        }
    }
    ASSERT_TRUE(retainedExecution.has_value());
    EXPECT_GT(retainedExecution->get(), 0.0f);
    retainedExecution.reset();
}

/// Verifies templated record GPU export, single-evaluation 2D createTexture extent, and DSL runtime.submit with GPU input.
TEST(GpuTaskGraphTests, TemplatedBoxExportSingleExtentEvalAndDslRuntimeSubmitSucceed)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();

    GpuGraphFixture::ResourceBox<GVM::RHI::Buffer> exportedBox;
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        GpuGraphFixture::Input input;
        input.count = 64u;
        auto boxExec = runtime.submit(GpuGraphFixture::Entries::buildTemplatedBoxExport, input);
        ASSERT_TRUE(boxExec.wait(10'000'000'000ull));
        ASSERT_EQ(boxExec.status(), GVM::Core::ExecutionStatus::Succeeded) << boxExec.error().c_str();
        exportedBox = boxExec.take();

        auto extentExec = runtime.submit(GpuGraphFixture::Entries::buildSingleEvalTextureExtentGraph, 0u);
        ASSERT_TRUE(extentExec.wait(10'000'000'000ull));
        ASSERT_EQ(extentExec.status(), GVM::Core::ExecutionStatus::Succeeded) << extentExec.error().c_str();
        EXPECT_EQ(extentExec.get(), 1u);
    }

    ASSERT_FALSE(exportedBox.payload.isNull());
    EXPECT_EQ(exportedBox.count, 64u);
    const float observedSum = GpuGraphFixture::submitBorrowedBufferInDsl(*device, {exportedBox.payload});
    EXPECT_FLOAT_EQ(observedSum, (63.0f * 64.0f / 2.0f) / 256.0f);
    device->freeBuffer(exportedBox.payload);
}

/// Retains internal GPU allocation cleanup if constructing the typed Execution::get() return value throws.
TEST(GpuTaskGraphTests, ThrowingExportedResultConstructorRetainsAllocationOwnership)
{
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> instance(
        GVM::Tests::createTestInstance(), GVM::RHI::destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();

    auto shouldThrow = std::make_shared<std::atomic<bool>>(false);
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        auto execution = runtime.submit([shouldThrow](uint32_t seed)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(ThrowingGpuOutputProducerNode(shouldThrow), seed);
        }, 1u);
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();

        shouldThrow->store(true, std::memory_order_release);
        EXPECT_THROW((void)execution.get(), std::runtime_error);
        shouldThrow->store(false, std::memory_order_release);
        auto claimed = execution.take();
        ASSERT_FALSE(claimed.buffer.isNull());
        device->freeBuffer(claimed.buffer);
    }

    auto alwaysThrow = std::make_shared<std::atomic<bool>>(false);
    {
        GVM::Core::TaskRuntime runtime(*device, {.workerCount = 2});
        auto execution = runtime.submit([alwaysThrow](uint32_t seed)
        {
            GVM::Core::TaskGraph graph;
            return graph.add(ThrowingGpuOutputProducerNode(alwaysThrow), seed);
        }, 2u);
        ASSERT_TRUE(execution.wait(10'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        alwaysThrow->store(true, std::memory_order_release);
        EXPECT_THROW((void)execution.get(), std::runtime_error);
    }
}

/// Completes batches exceeding Metal's 64-slot default command-buffer limit and preserves caller timestampWrites.
TEST(GpuTaskGraphTests, LargePassBatchAndCallerTimestampWritesSucceedUnderProfilerFeedback)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();

    {
        GVM::Core::TaskRuntime runtime(*device, {
            .workerCount = 2,
            .maximumPassesPerBatch = 96,
            .enablePassTimestampFeedback = true,
        });
        auto execution = runtime.submit([](uint32_t seed)
        {
            GVM::Core::TaskGraph graph;
            auto filled = graph.add(ManyFillPassesNode(96u), seed);
            return graph.add(GVM::Core::ReadbackNode<uint32_t>{}, filled);
        }, 1u);
        ASSERT_TRUE(execution.wait(15'000'000'000ull));
        ASSERT_EQ(execution.status(), GVM::Core::ExecutionStatus::Succeeded) << execution.error().c_str();
        const auto values = execution.get();
        ASSERT_EQ(values.size(), 1u);
        EXPECT_EQ(values[0] & 0xFFu, 96u & 0x7Fu);

        auto *mainQueue = device->getMainQueue();
        if (mainQueue->getTimestampQuerySupport().supported != False)
        {
            auto callerQuerySet = device->createQuerySet({
                .label = "CallerExplicitTimestamps",
                .type = QueryType::Timestamp,
                .count = 2u,
            });
            auto timedExec = runtime.submit([callerQuerySet](uint32_t seed)
            {
                GVM::Core::TaskGraph graph;
                auto dummy = graph.add(ExplicitTimestampRenderNode(callerQuerySet), seed);
                return graph.add(GVM::Core::ReadbackNode<uint8_t>{}, dummy);
            }, 1u);
            ASSERT_TRUE(timedExec.wait(10'000'000'000ull));
            ASSERT_EQ(timedExec.status(), GVM::Core::ExecutionStatus::Succeeded) << timedExec.error().c_str();

            auto resolveBuf = device->createBuffer({
                .label = "CallerExplicitResolve",
                .usage = BufferUsage::QueryResolve | BufferUsage::CopySrc,
                .size = sizeof(uint64_t) * 2u,
            });
            auto enc = mainQueue->createCommandEncoder();
            enc->resolveQuerySet(callerQuerySet, 0u, 2u, {resolveBuf, 0u, sizeof(uint64_t) * 2u});
            enc->end();
            auto readTs = mainQueue->readBufferAsync({resolveBuf, 0u, sizeof(uint64_t) * 2u});
            ASSERT_TRUE(mainQueue->submit({enc}, {}).wait(5'000'000'000ull));
            readTs->wait();
            uint64_t raw[2] = {};
            ASSERT_EQ(readTs->getData().size(), sizeof(raw));
            std::memcpy(raw, readTs->getData().data(), sizeof(raw));
            EXPECT_GT(raw[1], 0u);
            device->freeBuffer(resolveBuf);
        }
    }
}

/// Completes concurrent 192-pass executions sharing one device without exhausting Metal's 512 command-buffer slots,
/// and validates depth texture upload, Depth24Plus copy granularity, and depth readback routing.
TEST(GpuTaskGraphTests, ConcurrentLargeBatchesAndDepthTextureTransfersSucceed)
{
    using namespace GVM::RHI;
    eastl::unique_ptr<InstanceImpl, void (*)(Instance)> instance(GVM::Tests::createTestInstance(), destroyInstance);
    ASSERT_NE(instance, nullptr);
    auto *device = instance->createDevice();

    {
        GVM::Core::TaskRuntime runtime(*device, {
            .workerCount = 4,
            .maximumPassesPerExecution = 256,
            .maximumPassesPerBatch = 192,
        });
        eastl::vector<GVM::Core::Execution<eastl::vector<uint32_t>>> executions;
        for (uint32_t idx = 0u; idx < 4u; ++idx)
        {
            executions.push_back(runtime.submit([](uint32_t seed)
            {
                GVM::Core::TaskGraph graph;
                auto filled = graph.add(ManyFillPassesNode(192u), seed);
                return graph.add(GVM::Core::ReadbackNode<uint32_t>{}, filled);
            }, idx + 1u));
        }
        for (auto &exec : executions)
        {
            ASSERT_TRUE(exec.wait(20'000'000'000ull));
            ASSERT_EQ(exec.status(), GVM::Core::ExecutionStatus::Succeeded) << exec.error().c_str();
            const auto values = exec.get();
            ASSERT_EQ(values.size(), 1u);
            EXPECT_EQ(values[0] & 0xFFu, 192u & 0x7Fu);
        }

        const TextureFormat midFormat = instance->getBackend() == GraphicsBackend::Vulkan
            ? TextureFormat::Depth24Plus
            : TextureFormat::Depth32Float;
        auto depthExec = runtime.submit([midFormat](float clearDepth)
        {
            GVM::Core::TaskGraph graph;
            auto copied = graph.add(DepthTransferCopyNode(midFormat), clearDepth);
            return graph.add(GVM::Core::ReadbackTextureNode<float>{}, copied);
        }, 0.5f);
        ASSERT_TRUE(depthExec.wait(10'000'000'000ull));
        ASSERT_EQ(depthExec.status(), GVM::Core::ExecutionStatus::Succeeded) << depthExec.error().c_str();
        const auto readDepth = depthExec.get();
        ASSERT_EQ(readDepth.size(), 4u);
        EXPECT_NEAR(readDepth[0], 0.5f, 1e-4f);
        EXPECT_NEAR(readDepth[1], 0.25f, 1e-4f);
        EXPECT_NEAR(readDepth[2], 0.75f, 1e-4f);
        EXPECT_NEAR(readDepth[3], 1.0f, 1e-4f);
    }
}
