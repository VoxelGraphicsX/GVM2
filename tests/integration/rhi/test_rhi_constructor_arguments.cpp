#define GLM_ENABLE_EXPERIMENTAL
#include <gtest/gtest.h>
#include <EASTL/array.h>
#include <EASTL/unique_ptr.h>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMShaderReadbackTest.hpp"
#include <GVMRHI/GVMRHI.Vulkan/VKDevice.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKArguments.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKBuffer.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKQueue.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKTexture.hpp>
#if defined(__APPLE__)
#include <GVMRHI/GVMRHI.Metal/MComputePipeline.hpp>
#endif
#include "generate_result.hpp"

namespace
{
    /** Owns a headless device for constructor Args execution and native allocation lifetime checks. */
    class ConstructorArgumentsTest : public GVM::Tests::ShaderReadbackTest
    {
    };

    /** Produces distinct scalar, vector, matrix, half, bool, and array values for exact GPU observations. */
    ArgumentData makeArgumentData(uint32_t base)
    {
        ArgumentData data{};
        data.base = base;
        data.enabled = true;
        data.weight = GVM::Core::Math::ShaderHalf(0.5f);
        data.direction = float3(1, 2, 3);
        data.matrix = float4x4(0);
        data.matrix[2][1] = 11;
        data.fixed[0] = 7;
        data.fixed[1] = 9;
        data.sequence = {12, 13};
        return data;
    }

    /** Creates a shared storage target large enough to observe nonzero argument ranges independently. */
    GVM::RHI::Buffer createArgumentOutput(GVM::Core::DeviceProxy &proxy)
    {
        return proxy->createBuffer({.label = "ConstructorArgsOutput", .usage = GVM::RHI::BufferUsage::Storage |
            GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst, .size = 128});
    }
    /** Creates a small texture whose explicit views can serve both sampled and storage Args. */
    GVM::RHI::Texture createArgumentTexture(GVM::RHI::DeviceImpl &device, uint32_t mipCount = 1)
    {
        return device.createTexture({.label = "ArgsTexture", .usage = GVM::RHI::TextureUsage::TextureBinding |
            GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc,
            .size = {2, 2, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm, .mipLevelCount = mipCount});
    }

    /** Creates an Args-compatible sampler with an explicit nearest or linear filtering mode. */
    GVM::RHI::Sampler createArgumentSampler(GVM::RHI::DeviceImpl &device, GVM::RHI::FilterMode filter = GVM::RHI::FilterMode::Nearest)
    {
        return device.createSampler({.addressModeU = GVM::RHI::AddressMode::ClampToEdge,
            .addressModeV = GVM::RHI::AddressMode::ClampToEdge, .addressModeW = GVM::RHI::AddressMode::ClampToEdge,
            .magFilter = filter, .minFilter = filter, .mipmapFilter = GVM::RHI::MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f, .lodMaxClamp = 32.0f, .compare = GVM::RHI::CompareFunction::Undefined});
    }

    /** Records one immutable task without submitting it, allowing tests to choose submission order independently. */
    GVM::RHI::CommandEncoder recordArgumentDispatch(GVM::RHI::DeviceImpl &device, const GVM::Core::ComputePassTaskDescriptor &task)
    {
        auto encoder = device.getMainQueue()->createCommandEncoder();
        auto pass = encoder->beginComputePass({});
        task.dispatchFn(pass);
        pass->end();
        encoder->end();
        return encoder;
    }
}

TEST_F(ConstructorArgumentsTest, CapturesReplacementValuesAndRangesBeforeDeferredRecording)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto queue = proxy->graphicsQueue(0);
    eastl::array<uint32_t, 32> words{};
    queue->writeBuffer(output, words.data(), sizeof(words))->submit();
    auto data = makeArgumentData(10);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), data);
    ASSERT_EQ(pipeline->creations, 1u);
    auto first = pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 16, 8), data, 2, 1, 1);
    data.base = 20;
    auto second = pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 48, 8), data, 2, 1, 1);
    auto copy = first;
    data = makeArgumentData(900);
    ASSERT_EQ(pipeline->creations, 1u);
    pipeline.reset();
    queue->computePass("ArgsInterleaving", eastl::move(second), eastl::move(first), eastl::move(copy))->submit();
    queue->readBuffer(output, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[4], 151u);
    EXPECT_EQ(words[5], 152u);
    EXPECT_EQ(words[12], 161u);
    EXPECT_EQ(words[13], 162u);
    EXPECT_EQ(words[0], 0u);
}

/** Keeps one dispatch's buffer writes pending even when another Args field reads the same allocation. */
TEST_F(ConstructorArgumentsTest, PreservesWriteDependenciesForIdenticalAndOverlappingBufferArguments)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto shared = createArgumentOutput(proxy);
    auto output = createArgumentOutput(proxy);
    auto writer = proxy->createComputeClass<ArgumentAliasedBufferCompute>(GVM::RHI::BufferRange(shared), GVM::RHI::BufferRange(shared));
    auto reader = proxy->createComputeClass<ArgumentReadCompute>(GVM::RHI::BufferRange(shared), GVM::RHI::BufferRange(output), 0u);
    const eastl::array<uint32_t, 5> initial{10, 20, 30, 40, 50};
    const eastl::array<eastl::array<uint64_t, 4>, 5> ranges{{{0, 128, 0, 128}, {0, 16, 4, 16},
        {4, 16, 0, 16}, {0, 32, 4, 8}, {4, 8, 0, 32}}};
    for (const auto &range : ranges)
    {
        queue->writeBuffer(shared, initial.data(), sizeof(initial))->submit();
        const GVM::RHI::BufferRange destination(shared, range[0], range[1]);
        const GVM::RHI::BufferRange source(shared, range[2], range[3]);
        auto produce = writer->dispatchThreads(destination, source, 1, 1, 1);
        auto consume = reader->dispatchThreads(GVM::RHI::BufferRange(shared, range[0] + sizeof(uint32_t), sizeof(uint32_t)),
            GVM::RHI::BufferRange(output, 0, sizeof(uint32_t)), 0u, 1, 1, 1);
        queue->computePass("ArgsAliasedBufferDependencies", eastl::move(produce), eastl::move(consume))->submit();
        uint32_t observed = 0;
        queue->readBuffer(output, &observed, sizeof(observed))->submit();
        EXPECT_EQ(observed, initial[range[2] / sizeof(uint32_t)] + 1u);
    }
}

/** Rejects indirect inputs retired before recording while leaving the queue usable for later passes. */
TEST_F(ConstructorArgumentsTest, RejectsIndirectInputsFreedBeforeRecording)
{
    if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan &&
        !static_cast<GVM::RHI::Vulkan::VKDevice *>(device)->getCapabilities().supportsDrawIndirectCount())
    {
        GTEST_SKIP() << "The native Vulkan device does not expose drawIndirectCount.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst,
        .size = 36});
    const eastl::array<uint32_t, 9> records{0, 1, 0, 0, 3, 1, 0, 0, 0};
    queue->writeBuffer(commands, records.data(), sizeof(records))->submit();
    auto raster = proxy->createRenderClass<ArgumentCountRaster>(GVM::RHI::BufferRange(output), 1u);
    auto group = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(output));
    auto slot = proxy->createRenderClass<ArgumentSlotWriteRaster>(group);
    const GVM::RHI::BufferRange commandRange(commands, 16, 20);
    const GVM::RHI::BufferRange countRange(commands, 4, 4);
    const eastl::array<GVM::Core::RenderPassTaskDescriptor, 2> tasks{
        raster->drawIndirect(GVM::RHI::BufferRange(output), 7u, commandRange, 1, 0),
        slot->drawIndirectCount(commandRange, countRange, 1)};
    device->freeBuffer(commands);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    EXPECT_THROW(queue->renderPass("RetiredIndirectInputs", target, tasks), std::invalid_argument);
    auto mainQueue = device->getMainQueue();
    auto encoder = mainQueue->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    slot->run(3u, 1u, 0u, 0u).drawFn(render);
    render->end();
    encoder->end();
    mainQueue->submit({encoder});
    eastl::array<uint32_t, 2> result{};
    mainQueue->readBuffer(GVM::RHI::BufferRange(output), result.data(), sizeof(result));
    mainQueue->submit({});
    EXPECT_EQ(result[1], 99u);
}

/** Retains one native buffer per snapshot and preserves its writer state after an aliased dispatch. */
TEST_F(ConstructorArgumentsTest, CapturesAliasedBufferOwnershipAndAccessOnlyOnce)
{
    if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
    {
        GTEST_SKIP() << "Native reference and resource-state observations are specific to the Vulkan backend.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto buffer = createArgumentOutput(proxy);
    const auto baselineReferences = GVM::RHI::Vulkan::Testing::getBufferTotalReferenceCount(device, buffer);
    auto pipeline = proxy->createComputeClass<ArgumentAliasedBufferCompute>(GVM::RHI::BufferRange(buffer), GVM::RHI::BufferRange(buffer));
    auto task = pipeline->dispatchThreads(GVM::RHI::BufferRange(buffer), GVM::RHI::BufferRange(buffer), 1, 1, 1);
    EXPECT_EQ(GVM::RHI::Vulkan::Testing::getBufferTotalReferenceCount(device, buffer), baselineReferences + 1u);
    const eastl::array<uint32_t, 2> initial{17, 0};
    auto queue = proxy->graphicsQueue(0);
    queue->writeBuffer(buffer, initial.data(), sizeof(initial))->submit();
    queue->computePass("ArgsAliasedBufferWriterState", eastl::move(task))->submit();
    const auto state = GVM::RHI::Vulkan::Testing::getCurrentBufferState(device->getMainQueue(), buffer);
    EXPECT_TRUE(state.lastWriteInitialized);
    EXPECT_NE(state.lastWriteAccessMaskBits, 0u);
    EXPECT_NE(state.accessMaskBits & state.lastWriteAccessMaskBits, 0u);
}

TEST_F(ConstructorArgumentsTest, RetainsNativeUniformAllocationAfterHandleRetirement)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto uniform = proxy->createBuffer({.label = "ArgsRetiredUniform", .usage = GVM::RHI::BufferUsage::Uniform |
        GVM::RHI::BufferUsage::CopyDst, .size = 16});
    auto queue = proxy->graphicsQueue(0);
    uint32_t value = 17;
    queue->writeBuffer(uniform, &value, sizeof(value))->submit();
    ArgumentMaterial material{23, GVM::RHI::BufferRange(output), GVM::RHI::BufferRange(uniform)};
    auto pipeline = proxy->createComputeClass<ArgumentMaterialCompute>(material);
    auto task = pipeline->dispatchThreads(material, 2, 1, 1);
    device->freeBuffer(uniform);
    pipeline.reset();
    material = {};
    queue->computePass("ArgsRetainedUniform", eastl::move(task))->submit();
    eastl::array<uint32_t, 2> result{};
    queue->readBuffer(output, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 41u);
    EXPECT_EQ(result[1], 42u);
}

/** Verifies that pipeline construction performs no invocation allocation or argument-budget reservation. */
TEST_F(ConstructorArgumentsTest, ConstructionDoesNotConsumeArgumentBudget)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto data = makeArgumentData(1);
    device->setArgumentMemoryBudget(1);
    EXPECT_NO_THROW(proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), data));
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(128 * 1024));
}

TEST_F(ConstructorArgumentsTest, DiscardedTasksAndEncodersReleaseFiniteArgumentBudget)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto data = makeArgumentData(1);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), data);
    device->setArgumentMemoryBudget(8 * 1024);
    {
        auto task = pipeline->dispatchThreads(GVM::RHI::BufferRange(output), data, 1, 1, 1);
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto pass = encoder->beginComputePass({});
        EXPECT_THROW(task.dispatchFn(pass), GVM::RHI::ArgumentMemoryExhausted);
        pass->end();
        encoder->end();
    }
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(128 * 1024));
    {
        auto task = pipeline->dispatchThreads(GVM::RHI::BufferRange(output), data, 1, 1, 1);
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto pass = encoder->beginComputePass({});
        task.dispatchFn(pass);
        pass->end();
        encoder->end();
    }
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(32u * 1024u * 1024u));
}

/** Counts CPU snapshot bytes for diagnostics while the page budget never rejects snapshot creation. */
TEST_F(ConstructorArgumentsTest, ReportsArgumentSnapshotBytesWithoutLimiting)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), makeArgumentData(1));
    device->setArgumentMemoryBudget(64 * 1024);
    const uint64_t baseline = device->getArgumentMemoryStatistics().snapshotBytes;
    eastl::vector<GVM::Core::ComputePassTaskDescriptor> tasks;
    for (uint32_t index = 0; index < 32; ++index)
    {
        EXPECT_NO_THROW(tasks.push_back(pipeline->dispatchThreads(GVM::RHI::BufferRange(output), makeArgumentData(index), 1, 1, 1)));
    }
    EXPECT_GT(device->getArgumentMemoryStatistics().snapshotBytes, baseline);
    tasks.clear();
    EXPECT_EQ(device->getArgumentMemoryStatistics().snapshotBytes, baseline);
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(32u * 1024u * 1024u));
}

/** Returns completed parameter pages to the pool so a later encoder reuses them without a native allocation. */
TEST_F(ConstructorArgumentsTest, ReusesCompletedArgumentPages)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), makeArgumentData(5));
    device->setArgumentMemoryBudget(64 * 1024);
    {
        auto encoder = recordArgumentDispatch(*device,
            pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 0, 4), makeArgumentData(5), 1, 1, 1));
        device->getMainQueue()->submit({encoder});
    }
    eastl::array<uint32_t, 8> words{};
    proxy->graphicsQueue(0)->readBuffer(output, words.data(), sizeof(words))->submit();
    {
        const auto stats = device->getArgumentMemoryStatistics();
        EXPECT_EQ(stats.pageBytesFree, 64u * 1024u);
        EXPECT_EQ(stats.pageBytesInFlight, 0u);
        EXPECT_EQ(stats.pageBytesRecording, 0u);
        const uint64_t allocationCount = stats.pageAllocationCount;
        {
            auto encoder = recordArgumentDispatch(*device,
                pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 16, 4), makeArgumentData(7), 1, 1, 1));
            device->getMainQueue()->submit({encoder});
        }
        EXPECT_EQ(device->getArgumentMemoryStatistics().pageAllocationCount, allocationCount);
    }
    words = {};
    proxy->graphicsQueue(0)->readBuffer(output, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[0], 146u);
    EXPECT_EQ(words[4], 148u);
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(32u * 1024u * 1024u));
}

/** Blocks page acquisition only until the oldest submitted work releases its pages. */
TEST_F(ConstructorArgumentsTest, WaitsForSubmittedWorkWhenPagesAreExhausted)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), makeArgumentData(1));
    device->setArgumentMemoryBudget(64 * 1024);
    auto encoderA = recordArgumentDispatch(*device,
        pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 0, 4), makeArgumentData(3), 1, 1, 1));
    device->getMainQueue()->submit({encoderA});
    auto encoderB = recordArgumentDispatch(*device,
        pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 16, 4), makeArgumentData(9), 1, 1, 1));
    device->getMainQueue()->submit({encoderB});
    eastl::array<uint32_t, 8> words{};
    proxy->graphicsQueue(0)->readBuffer(output, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[0], 144u);
    EXPECT_EQ(words[4], 150u);
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(32u * 1024u * 1024u));
}

/** Throws ArgumentMemoryExhausted only when unsubmitted recording work holds every available page. */
TEST_F(ConstructorArgumentsTest, ThrowsArgumentMemoryExhaustedWhenOnlyRecordingWorkHoldsPages)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), makeArgumentData(1));
    eastl::array<uint32_t, 8> words{};
    proxy->graphicsQueue(0)->writeBuffer(output, words.data(), sizeof(words))->submit();
    device->setArgumentMemoryBudget(64 * 1024);
    auto encoderA = recordArgumentDispatch(*device,
        pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 0, 4), makeArgumentData(3), 1, 1, 1));
    {
        auto encoderB = device->getMainQueue()->createCommandEncoder();
        auto pass = encoderB->beginComputePass({});
        EXPECT_THROW(pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 16, 4), makeArgumentData(9), 1, 1, 1).dispatchFn(pass),
            GVM::RHI::ArgumentMemoryExhausted);
        pass->end();
        encoderB->end();
    }
    encoderA = nullptr;
    auto encoderB = recordArgumentDispatch(*device,
        pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 16, 4), makeArgumentData(9), 1, 1, 1));
    device->getMainQueue()->submit({encoderB});
    proxy->graphicsQueue(0)->readBuffer(output, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[0], 0u);
    EXPECT_EQ(words[4], 150u);
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(32u * 1024u * 1024u));
}

/** Rejects a zero page budget while allowing budget changes with live snapshots. */
TEST_F(ConstructorArgumentsTest, RejectsZeroArgumentBudget)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), makeArgumentData(1));
    auto task = pipeline->dispatchThreads(GVM::RHI::BufferRange(output), makeArgumentData(1), 1, 1, 1);
    EXPECT_THROW(device->setArgumentMemoryBudget(0), std::invalid_argument);
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(128 * 1024));
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(32u * 1024u * 1024u));
}

TEST_F(ConstructorArgumentsTest, DrawsReplacementArgsAndRestoresVertexBuffersAcrossPipelineChanges)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto compatibilityOutput = createArgumentOutput(proxy);
    auto vertices = proxy->createBuffer({.label = "ArgsVertexInputs", .usage = GVM::RHI::BufferUsage::Vertex |
        GVM::RHI::BufferUsage::CopyDst, .size = sizeof(ArgumentVertexInput) * 3});
    const eastl::array<ArgumentVertexInput, 3> inputs = {{{float2(-1, -1), 7}, {float2(3, -1), 8}, {float2(-1, 3), 9}}};
    auto texture = proxy->createTexture({.label = "ArgsRasterReadback", .usage = GVM::RHI::TextureUsage::RenderAttachment |
        GVM::RHI::TextureUsage::CopySrc, .size = {8, 8, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = texture->createView();
    auto groups = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(compatibilityOutput));
    auto procedural = proxy->createRenderClass<ArgumentRaster>(float4(0), GVM::RHI::BufferRange(output), 0u);
    auto buffered = proxy->createRenderClass<ArgumentVertexRaster>(float4(0), GVM::RHI::BufferRange(output), 0u);
    auto compatibility = proxy->createRenderClass<ArgumentSlotRaster>(groups);
    auto first = procedural->draw(float4(1, 0, 0, 1), GVM::RHI::BufferRange(output, 0, 12), 10u, 3, 1, 0, 0);
    auto second = buffered->draw(float4(0, 1, 0, 1), GVM::RHI::BufferRange(output, 16, 12), 20u, 3, 1, 0, 0);
    auto last = procedural->draw(float4(0, 0, 1, 1), GVM::RHI::BufferRange(output, 32, 12), 30u, 3, 1, 0, 0);
    GVM::Core::RenderPassTaskDescriptor vertexBinding;
    vertexBinding.drawFn = [vertices](GVM::RHI::RenderPassEncoder pass) { pass->setVertexBuffer(GVM::RHI::BufferRange(vertices), 0); };
    const eastl::array<GVM::Core::RenderPassTaskDescriptor, 5> tasks = {vertexBinding, first, compatibility->run(3u, 1u, 0u, 0u), second, last};
    procedural.reset();
    buffered.reset();
    auto queue = proxy->graphicsQueue(0);
    queue->writeBuffer(vertices, inputs.data(), sizeof(inputs))->submit();
    queue->renderPass("ArgsPipelineSwitches", target, tasks)->submit();
    eastl::array<uint32_t, 32> words{};
    queue->readBuffer(output, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[0], 10u); EXPECT_EQ(words[1], 11u); EXPECT_EQ(words[2], 12u);
    EXPECT_EQ(words[4], 27u); EXPECT_EQ(words[5], 28u); EXPECT_EQ(words[6], 29u);
    EXPECT_EQ(words[8], 30u); EXPECT_EQ(words[9], 31u); EXPECT_EQ(words[10], 32u);
    queue->readBuffer(compatibilityOutput, words.data(), 12)->submit();
    EXPECT_EQ(words[0], 7u); EXPECT_EQ(words[1], 8u); EXPECT_EQ(words[2], 9u);
    eastl::array<uint8_t, 8 * 8 * 4> pixels{};
    queue->readTexture(texture, pixels.data(), pixels.size())->submit();
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        EXPECT_EQ(pixels[offset], 0u); EXPECT_EQ(pixels[offset + 1], 0u);
        EXPECT_EQ(pixels[offset + 2], 255u); EXPECT_EQ(pixels[offset + 3], 255u);
    }
}

/** Keeps index allocations and all draw controls independent after their public handles are freed. */
TEST_F(ConstructorArgumentsTest, CapturesIndexedDrawResourcesFormatsAndControlsBeforeRecording)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    auto firstIndices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst, .size = 12});
    auto secondIndices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst, .size = 24});
    const eastl::array<uint16_t, 6> firstData{99, 99, 3, 4, 5, 99};
    const eastl::array<uint32_t, 6> secondData{99, 99, 0, 1, 2, 99};
    queue->writeBuffer(firstIndices, firstData.data(), sizeof(firstData));
    queue->writeBuffer(secondIndices, secondData.data(), sizeof(secondData))->submit();
    auto raster = proxy->createRenderClass<ArgumentIndexedRaster>(float4(0), GVM::RHI::BufferRange(output), 0u);
    auto first = raster->drawIndexed(float4(1, 0, 0, 1), GVM::RHI::BufferRange(output), 10u,
        GVM::RHI::BufferRange(firstIndices, 2, 8), GVM::RHI::IndexFormat::Uint16, 3, 1, 1, -3, 2);
    auto second = raster->drawIndexed(float4(0, 1, 0, 1), GVM::RHI::BufferRange(output, 32, 32), 20u,
        GVM::RHI::BufferRange(secondIndices, 4, 16), GVM::RHI::IndexFormat::Uint32, 3, 1, 1, 3, 3);
    auto copy = first;
    raster.reset();
    auto texture = proxy->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = texture->createView();
    const eastl::array<GVM::Core::RenderPassTaskDescriptor, 3> tasks{first, second, copy};
    auto *recorded = queue->renderPass("CapturedIndexedArgs", target, tasks);
    device->freeBuffer(firstIndices);
    device->freeBuffer(secondIndices);
    recorded->submit();
    eastl::array<uint32_t, 32> words{};
    queue->readBuffer(output, words.data(), sizeof(words))->submit();
    for (uint32_t index = 0; index < 3; ++index)
    {
        EXPECT_EQ(words[index], 42u + index);
        EXPECT_EQ(words[11 + index], 71u + index);
    }
    eastl::array<uint8_t, 4> pixel{};
    queue->readTexture(texture, pixel.data(), pixel.size())->submit();
    EXPECT_EQ(pixel[0], 255u);
    EXPECT_EQ(pixel[1], 0u);
    EXPECT_EQ(pixel[3], 255u);
}

/** Reads GPU-written native workgroup counts after the command buffer's public handle is freed. */
TEST_F(ConstructorArgumentsTest, DispatchesCapturedGpuWrittenIndirectWorkgroupCounts)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::Indirect,
        .size = 32});
    auto writer = proxy->createComputeClass<ArgumentDispatchWriter>(GVM::RHI::BufferRange(commands, 16, 12), uint3(2, 1, 1));
    auto consumer = proxy->createComputeClass<ArgumentWorkgroupCompute>(GVM::RHI::BufferRange(output), 0u);
    auto consume = consumer->dispatchWorkgroupsIndirect(GVM::RHI::BufferRange(output), 31u, GVM::RHI::BufferRange(commands, 16, 12));
    auto produce = writer->dispatchThreads(GVM::RHI::BufferRange(commands, 16, 12), uint3(2, 1, 1), 1, 1, 1);
    consumer.reset();
    writer.reset();
    auto *recorded = queue->computePass("CapturedGpuIndirectDispatch", eastl::move(produce), eastl::move(consume));
    device->freeBuffer(commands);
    recorded->submit();
    eastl::array<uint32_t, 8> words{};
    queue->readBuffer(output, words.data(), sizeof(words))->submit();
    for (uint32_t index = 0; index < words.size(); ++index)
    {
        EXPECT_EQ(words[index], 31u + index);
    }
}

/** Uses the explicit indirect range after a shader Args task selected another command range. */
TEST_F(ConstructorArgumentsTest, DispatchesTheExplicitIndirectRangeAfterBindingArguments)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst, .size = 64});
    const eastl::array<uint32_t, 16> counts{0, 1, 1, 0, 0, 0, 0, 0, 2, 1, 1};
    queue->writeBuffer(commands, counts.data(), sizeof(counts))->submit();
    eastl::array<uint32_t, 8> observed{};
    queue->writeBuffer(output, observed.data(), sizeof(observed))->submit();
    auto pipeline = proxy->createComputeClass<ArgumentWorkgroupCompute>(GVM::RHI::BufferRange(output), 0u);
    auto initial = pipeline->dispatchWorkgroupsIndirect(GVM::RHI::BufferRange(output), 31u, GVM::RHI::BufferRange(commands, 0, 12));
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto pass = encoder->beginComputePass({});
    initial.dispatchFn(pass);
    pass->dispatchWorkgroupsIndirect(GVM::RHI::BufferRange(commands, 32, 12));
    pass->end();
    encoder->end();
    device->getMainQueue()->submit({encoder});
    queue->readBuffer(output, observed.data(), sizeof(observed))->submit();
    for (uint32_t index = 0; index < observed.size(); ++index)
    {
        EXPECT_EQ(observed[index], 31u + index);
    }
}

/** Uses explicit command ranges for both draw kinds while sharing immutable shader and index inputs. */
TEST_F(ConstructorArgumentsTest, DrawsTheExplicitIndirectRangesAfterBindingArguments)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst, .size = 96});
    auto indices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst, .size = 12});
    eastl::array<uint32_t, 24> records{};
    records[8] = 3;
    records[9] = 1;
    records[16] = 3;
    records[17] = 1;
    const eastl::array<uint32_t, 3> elements{0, 1, 2};
    uint32_t observed = 0;
    queue->writeBuffer(output, &observed, sizeof(observed))->submit();
    queue->writeBuffer(commands, records.data(), sizeof(records))->submit();
    queue->writeBuffer(indices, elements.data(), sizeof(elements))->submit();
    auto counter = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst, .size = 4});
    const uint32_t count = 1;
    queue->writeBuffer(counter, &count, sizeof(count))->submit();
    const bool supportsCount = instance->getBackend() == GVM::RHI::GraphicsBackend::Metal ||
        static_cast<GVM::RHI::Vulkan::VKDevice *>(device)->getCapabilities().supportsDrawIndirectCount();
    auto raster = proxy->createRenderClass<ArgumentCountRaster>(GVM::RHI::BufferRange(output), 5u);
    auto initial = raster->drawIndexedIndirect(GVM::RHI::BufferRange(output), 5u,
        GVM::RHI::BufferRange(indices), GVM::RHI::IndexFormat::Uint32, GVM::RHI::BufferRange(commands, 0, 20), 0u, 0u);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto pass = encoder->beginRenderPass(target.getRenderPassDescriptor());
    initial.drawFn(pass);
    pass->drawIndirect(GVM::RHI::BufferRange(commands, 32, 16), 1u, 0u);
    pass->drawIndexedIndirect(GVM::RHI::BufferRange(commands, 64, 20), 1u, 0u);
    if (supportsCount)
    {
        pass->drawIndirectCount(GVM::RHI::BufferRange(commands, 32, 16), GVM::RHI::BufferRange(counter), 1u);
        pass->drawIndexedIndirectCount(GVM::RHI::BufferRange(commands, 64, 20), GVM::RHI::BufferRange(counter), 1u);
    }
    pass->end();
    encoder->end();
    device->getMainQueue()->submit({encoder});
    queue->readBuffer(output, &observed, sizeof(observed))->submit();
    EXPECT_EQ(observed, supportsCount ? 20u : 10u);
}

/** Preserves the explicit index binding when shader Args are rebound independently of draw controls. */
TEST_F(ConstructorArgumentsTest, RebindsShaderArgumentsWithoutReplacingExplicitIndexState)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    uint32_t observed = 0;
    queue->writeBuffer(output, &observed, sizeof(observed))->submit();
    auto firstIndices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst, .size = 6});
    auto secondIndices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst, .size = 24});
    const eastl::array<uint16_t, 3> first{0, 1, 2};
    const eastl::array<uint32_t, 6> second{0, 1, 2, 0, 1, 2};
    queue->writeBuffer(firstIndices, first.data(), sizeof(first))->submit();
    queue->writeBuffer(secondIndices, second.data(), sizeof(second))->submit();
    auto raster = proxy->createRenderClass<ArgumentCountRaster>(GVM::RHI::BufferRange(output), 1u);
    auto initial = raster->drawIndexed(GVM::RHI::BufferRange(output), 1u,
        GVM::RHI::BufferRange(firstIndices), GVM::RHI::IndexFormat::Uint16, 3, 1, 0, 0, 0);
    auto shaderOnly = raster->draw(GVM::RHI::BufferRange(output), 7u, 3, 1, 0, 0);
    auto replacement = raster->drawIndexed(GVM::RHI::BufferRange(output), 1u,
        GVM::RHI::BufferRange(secondIndices), GVM::RHI::IndexFormat::Uint32, 6, 1, 0, 0, 0);
    raster.reset();
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto pass = encoder->beginRenderPass(target.getRenderPassDescriptor());
    initial.drawFn(pass);
    replacement.drawFn(pass);
    shaderOnly.drawFn(pass);
    pass->drawIndexed(6, 1, 0, 0, 0);
    pass->end();
    encoder->end();
    device->freeBuffer(firstIndices);
    device->freeBuffer(secondIndices);
    device->getMainQueue()->submit({encoder});
    queue->readBuffer(output, &observed, sizeof(observed))->submit();
    EXPECT_EQ(observed, 24u);
}

/** Preserves indirect source ranges and shared ICB storage across different captured index bindings. */
TEST_F(ConstructorArgumentsTest, DrawsCapturedIndirectArgsAfterFreeingBothControlBuffers)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst,
        .size = 192});
    auto indices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst,
        .size = 32});
    eastl::array<uint32_t, 48> records{};
    // Packed draws at 16 and 32, a single draw at 64, and padded indexed draws at 96 and 120.
    records[4] = 3; records[5] = 1;
    records[8] = 3; records[9] = 1;
    records[16] = 3; records[17] = 1;
    records[24] = 3; records[25] = 1; records[26] = 1;
    records[30] = 3; records[31] = 1; records[32] = 1;
    const eastl::array<uint32_t, 8> indexData{99, 99, 0, 1, 2, 3, 4, 5};
    queue->writeBuffer(commands, records.data(), sizeof(records));
    queue->writeBuffer(indices, indexData.data(), sizeof(indexData))->submit();
    auto raster = proxy->createRenderClass<ArgumentIndexedRaster>(float4(0), GVM::RHI::BufferRange(output), 0u);
    auto multi = raster->drawIndirect(float4(1, 0, 0, 1), GVM::RHI::BufferRange(output), 10u,
        GVM::RHI::BufferRange(commands, 16, 32), 2, 0);
    auto single = raster->drawIndirect(float4(1, 0, 0, 1), GVM::RHI::BufferRange(output, 16, 16), 20u,
        GVM::RHI::BufferRange(commands, 64, 16), 1, 0);
    auto firstIndexed = raster->drawIndexedIndirect(float4(0, 1, 0, 1), GVM::RHI::BufferRange(output, 32, 32), 30u,
        GVM::RHI::BufferRange(indices, 4, 16), GVM::RHI::IndexFormat::Uint32,
        GVM::RHI::BufferRange(commands, 96, 44), 2, 24);
    auto secondIndexed = raster->drawIndexedIndirect(float4(0, 0, 1, 1), GVM::RHI::BufferRange(output, 64, 32), 40u,
        GVM::RHI::BufferRange(indices, 16, 16), GVM::RHI::IndexFormat::Uint32,
        GVM::RHI::BufferRange(commands, 96, 44), 2, 24);
    auto zero = raster->drawIndirect(float4(0), GVM::RHI::BufferRange(output), 999u,
        GVM::RHI::BufferRange(commands, 16, 16), 0, 0);
    raster.reset();
    auto texture = proxy->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = texture->createView();
    const eastl::array<GVM::Core::RenderPassTaskDescriptor, 5> tasks{multi, single, firstIndexed, secondIndexed, zero};
    auto *recorded = queue->renderPass("CapturedIndirectDraws", target, tasks);
    device->freeBuffer(commands);
    device->freeBuffer(indices);
    recorded->submit();
    eastl::array<uint32_t, 32> words{};
    queue->readBuffer(output, words.data(), sizeof(words))->submit();
    for (uint32_t index = 0; index < 3; ++index)
    {
        EXPECT_EQ(words[index], 10u + index);
        EXPECT_EQ(words[4 + index], 20u + index);
        EXPECT_EQ(words[8 + index], 30u + index);
        EXPECT_EQ(words[19 + index], 43u + index);
    }
    eastl::array<uint8_t, 4> pixel{};
    queue->readTexture(texture, pixel.data(), pixel.size())->submit();
    EXPECT_EQ(pixel[0], 0u);
    EXPECT_EQ(pixel[1], 0u);
    EXPECT_EQ(pixel[2], 255u);
}

/** Reads padded records ending exactly at the native allocation boundary for both indirect draw kinds. */
TEST_F(ConstructorArgumentsTest, ReadsOnlyNativeFieldsOfTheFinalPaddedIndirectRecord)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto source = createArgumentTexture(*device);
    const eastl::array<uint8_t, 16> sourcePixels{37, 0, 0, 255, 37, 0, 0, 255, 37, 0, 0, 255, 37, 0, 0, 255};
    queue->writeTexture(source, sourcePixels.data(), sizeof(sourcePixels))->submit();
    auto view = source->createView();
    auto sampler = createArgumentSampler(*device);
    auto raster = proxy->createRenderClass<ArgumentTextureRaster>(view, sampler);
    auto targetImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = targetImage->createView();
    for (bool indexed : {false, true})
    {
        SCOPED_TRACE(indexed);
        constexpr uint32_t offset = 16;
        constexpr uint32_t stride = 24;
        const uint32_t commandSize = indexed ? sizeof(GVM::RHI::IndirectIndexedRenderCommand) : sizeof(GVM::RHI::IndirectRenderCommand);
        const uint32_t bytes = offset + stride + commandSize;
        auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst, .size = bytes});
        eastl::array<uint32_t, 16> words{};
        words[offset / 4] = words[(offset + stride) / 4] = 3;
        words[offset / 4 + 1] = words[(offset + stride) / 4 + 1] = 1;
        queue->writeBuffer(commands, words.data(), bytes)->submit();
        GVM::Core::RenderPassTaskDescriptor task;
        GVM::RHI::Buffer indices;
        if (indexed)
        {
            indices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst, .size = 12});
            const eastl::array<uint32_t, 3> indexData{0, 1, 2};
            queue->writeBuffer(indices, indexData.data(), sizeof(indexData))->submit();
            task = raster->drawIndexedIndirect(view, sampler, GVM::RHI::BufferRange(indices), GVM::RHI::IndexFormat::Uint32,
                GVM::RHI::BufferRange(commands, offset, bytes - offset), 2, stride);
        }
        else
        {
            task = raster->drawIndirect(view, sampler, GVM::RHI::BufferRange(commands, offset, bytes - offset), 2, stride);
        }
        auto *recorded = queue->renderPass("ExactPaddedIndirectAllocation", target, task);
        if (!indices.isNull())
        {
            device->freeBuffer(indices);
        }
        device->freeBuffer(commands);
        recorded->submit();
        eastl::array<uint8_t, 4> pixel{};
        queue->readTexture(targetImage, pixel.data(), sizeof(pixel))->submit();
        EXPECT_EQ(pixel[0], 37u);
        EXPECT_EQ(pixel[3], 255u);
    }
}

/** Observes GPU-generated counts, aliased inputs, clamping, reuse, and native lifetimes in one submission. */
TEST_F(ConstructorArgumentsTest, DrawsGpuGeneratedIndirectCountsAfterFreeingControlBuffers)
{
    if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan &&
        !static_cast<GVM::RHI::Vulkan::VKDevice *>(device)->getCapabilities().supportsDrawIndirectCount())
    {
        GTEST_SKIP() << "The native Vulkan device does not expose drawIndirectCount.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    const eastl::array<uint32_t, 5> counts{0, 1, 2, 7, 0};
    for (uint32_t mode = 0; mode < 3; ++mode)
    {
        SCOPED_TRACE(mode);
        const bool indexed = mode != 0;
        const uint32_t commandWords = indexed ? 5u : 4u;
        constexpr uint32_t capacity = 3;
        constexpr uint32_t stride = 24;
        const uint32_t bytes = 16 + (capacity - 1) * stride + commandWords * 4;
        auto source = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::Indirect,
            .size = bytes});
        auto indices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst,
            .size = 16});
        const auto format = mode == 1 ? GVM::RHI::IndexFormat::Uint16 : GVM::RHI::IndexFormat::Uint32;
        if (mode == 1)
        {
            const eastl::array<uint16_t, 5> values{0, 0, 0, 1, 2};
            queue->writeBuffer(indices, values.data(), sizeof(values))->submit();
        }
        else
        {
            const eastl::array<uint32_t, 4> values{0, 0, 1, 2};
            queue->writeBuffer(indices, values.data(), sizeof(values))->submit();
        }
        auto writer = proxy->createComputeClass<ArgumentIndirectCountWriter>(GVM::RHI::BufferRange(source),
            0u, capacity, stride / 4, commandWords);
        auto raster = proxy->createRenderClass<ArgumentCountRaster>(GVM::RHI::BufferRange(output), 1u);
        eastl::vector<GVM::Core::ComputePassTaskDescriptor> producers;
        for (uint32_t count : counts)
        {
            producers.push_back(writer->dispatchThreads(GVM::RHI::BufferRange(source), count, capacity,
                stride / 4, commandWords, 1, 1, 1));
        }
        eastl::vector<GVM::Core::RenderPassTaskDescriptor> draws;
        for (uint32_t maximum : {0u, 1u, capacity})
        {
            const GVM::RHI::BufferRange commands(source, 16, bytes - 16);
            const GVM::RHI::BufferRange count(source, 4, sizeof(uint32_t));
            draws.push_back(indexed ? raster->drawIndexedIndirectCount(GVM::RHI::BufferRange(output), 5u,
                GVM::RHI::BufferRange(indices, 4, mode == 1 ? 6 : 12), format, commands, count, maximum, stride) :
                raster->drawIndirectCount(GVM::RHI::BufferRange(output), 5u, commands, count, maximum, stride));
        }
        writer.reset();
        raster.reset();
        const eastl::array<uint32_t, 3> maxima{0, 1, capacity};
        for (size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex)
        {
            for (size_t countIndex = 0; countIndex < counts.size(); ++countIndex)
            {
                SCOPED_TRACE(maxima[drawIndex]);
                SCOPED_TRACE(counts[countIndex]);
                uint32_t observed = 0;
                queue->writeBuffer(output, &observed, sizeof(observed))->submit();
                auto encoder = device->getMainQueue()->createCommandEncoder();
                auto compute = encoder->beginComputePass({});
                producers[countIndex].dispatchFn(compute);
                compute->end();
                compute.reset();
                auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
                auto copied = draws[drawIndex];
                copied.drawFn(render);
                render->end();
                render.reset();
                encoder->end();
                device->getMainQueue()->submit({encoder});
                queue->readBuffer(output, &observed, sizeof(observed))->submit();
                EXPECT_EQ(observed, eastl::min(counts[countIndex], maxima[drawIndex]) * 5u);
            }
        }
        device->freeBuffer(source);
        device->freeBuffer(indices);
    }
}

/** Keeps per-draw count ranges independent when count and fixed draws share a source and a render pass. */
TEST_F(ConstructorArgumentsTest, MixesFixedAndGpuCountsWithIndependentConversionSlots)
{
    if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan &&
        !static_cast<GVM::RHI::Vulkan::VKDevice *>(device)->getCapabilities().supportsDrawIndirectCount())
    {
        GTEST_SKIP() << "The native Vulkan device does not expose drawIndirectCount.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    auto source = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst,
        .size = 48});
    const eastl::array<uint32_t, 12> words{0, 2, 0, 0, 3, 1, 0, 0, 3, 1, 0, 0};
    queue->writeBuffer(source, words.data(), sizeof(words))->submit();
    auto raster = proxy->createRenderClass<ArgumentCountRaster>(GVM::RHI::BufferRange(output), 1u);
    const GVM::RHI::BufferRange commands(source, 16, 32);
    auto zero = raster->drawIndirectCount(GVM::RHI::BufferRange(output), 11u, commands,
        GVM::RHI::BufferRange(source, 0, 4), 1);
    auto counted = raster->drawIndirectCount(GVM::RHI::BufferRange(output), 3u, commands,
        GVM::RHI::BufferRange(source, 4, 4), 2);
    auto fixed = raster->drawIndirect(GVM::RHI::BufferRange(output), 7u, commands, 1, 0);
    raster.reset();
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    uint32_t observed = 0;
    queue->writeBuffer(output, &observed, sizeof(observed))->submit();
    auto *recorded = queue->renderPass("MixedGpuAndFixedCounts", target, zero, counted, fixed);
    device->freeBuffer(source);
    recorded->submit();
    queue->readBuffer(output, &observed, sizeof(observed))->submit();
    EXPECT_EQ(observed, 13u);
}

/** Rejects malformed count inputs while keeping feature and maximum checks in the recording boundary. */
TEST_F(ConstructorArgumentsTest, RejectsInvalidIndirectCountResourcesAndControls)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect, .size = 64});
    auto counter = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect, .size = 16});
    auto wrongUsage = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Storage, .size = 16});
    auto raster = proxy->createRenderClass<ArgumentCountRaster>(GVM::RHI::BufferRange(output), 1u);
    auto wrongUsageTask = raster->drawIndirectCount(GVM::RHI::BufferRange(output), 1u,
        GVM::RHI::BufferRange(commands), GVM::RHI::BufferRange(wrongUsage), 1);
    auto misalignedCounter = raster->drawIndirectCount(GVM::RHI::BufferRange(output), 1u,
        GVM::RHI::BufferRange(commands), GVM::RHI::BufferRange(counter, 2, 4), 1);
    auto shortCounter = raster->drawIndirectCount(GVM::RHI::BufferRange(output), 1u,
        GVM::RHI::BufferRange(commands), GVM::RHI::BufferRange(counter, 0, 3), 1);
    GVM::RHI::BufferRange outOfBoundsCounterRange(counter);
    outOfBoundsCounterRange.offset = 16; outOfBoundsCounterRange.size = 4;
    auto outOfBoundsCounter = raster->drawIndirectCount(GVM::RHI::BufferRange(output), 1u,
        GVM::RHI::BufferRange(commands), outOfBoundsCounterRange, 1);
    auto task = raster->drawIndirectCount(GVM::RHI::BufferRange(output), 1u,
        GVM::RHI::BufferRange(commands), GVM::RHI::BufferRange(counter), 5);
    auto invalidStride = raster->drawIndirectCount(GVM::RHI::BufferRange(output), 1u,
        GVM::RHI::BufferRange(commands), GVM::RHI::BufferRange(counter), 1, 12);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    auto *nativeCommands = instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan ?
        static_cast<GVM::RHI::Vulkan::VKBuffer *>(commands.get()) : nullptr;
    const auto referencesBeforeFailure = nativeCommands != nullptr ? nativeCommands->getReferenceCount() : 0u;
    EXPECT_THROW(render->drawIndirectCount(GVM::RHI::BufferRange(commands), GVM::RHI::BufferRange(counter), 1u), std::logic_error);
    EXPECT_THROW(render->drawIndexedIndirectCount(GVM::RHI::BufferRange(commands), GVM::RHI::BufferRange(counter), 1u), std::logic_error);
    EXPECT_THROW(wrongUsageTask.drawFn(render), std::invalid_argument);
    EXPECT_THROW(misalignedCounter.drawFn(render), std::out_of_range);
    EXPECT_THROW(shortCounter.drawFn(render), std::out_of_range);
    EXPECT_THROW(outOfBoundsCounter.drawFn(render), std::out_of_range);
    if (nativeCommands != nullptr)
    {
        EXPECT_EQ(nativeCommands->getReferenceCount(), referencesBeforeFailure);
    }
    if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan &&
        !static_cast<GVM::RHI::Vulkan::VKDevice *>(device)->getCapabilities().supportsDrawIndirectCount())
    {
        EXPECT_THROW(task.drawFn(render), std::logic_error);
    }
    else
    {
        EXPECT_THROW(task.drawFn(render), std::out_of_range);
        EXPECT_THROW(invalidStride.drawFn(render), std::invalid_argument);
    }
    task.drawFn = {};
    invalidStride.drawFn = {};
    if (nativeCommands != nullptr)
    {
        EXPECT_EQ(nativeCommands->getReferenceCount(), 1u);
    }
}

/** Preserves ordinary vertex inputs and Slot bindings without creating shader Args for GPU counts. */
TEST_F(ConstructorArgumentsTest, DrawsGpuCountsWithZeroArgsAndSlotBindings)
{
    if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan &&
        !static_cast<GVM::RHI::Vulkan::VKDevice *>(device)->getCapabilities().supportsDrawIndirectCount())
    {
        GTEST_SKIP() << "The native Vulkan device does not expose drawIndirectCount.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto output = createArgumentOutput(proxy);
    auto source = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst,
        .size = 36});
    const eastl::array<uint32_t, 9> commands{0, 1, 0, 0, 3, 1, 0, 0, 0};
    queue->writeBuffer(source, commands.data(), sizeof(commands))->submit();
    auto vertices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::CopyDst,
        .size = sizeof(ArgumentVertexInput) * 3});
    const eastl::array<ArgumentVertexInput, 3> points{{{float2(-1, -1), 0}, {float2(3, -1), 0}, {float2(-1, 3), 0}}};
    queue->writeBuffer(vertices, points.data(), sizeof(points))->submit();
    auto indices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst,
        .size = 12});
    const eastl::array<uint32_t, 3> indexValues{0, 1, 2};
    queue->writeBuffer(indices, indexValues.data(), sizeof(indexValues))->submit();
    auto zero = proxy->createRenderClass<ArgumentZeroRaster>();
    auto group = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(output));
    auto slot = proxy->createRenderClass<ArgumentSlotWriteRaster>(group);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    auto passDescriptor = target.getRenderPassDescriptor();
    passDescriptor.colorAttachments[0].clearValue = {0, 0, 0, 1};
    for (uint32_t count : {0u, 1u})
    {
        SCOPED_TRACE(count);
        const GVM::RHI::BufferRange commandRange(source, 16, 20);
        const GVM::RHI::BufferRange countRange(source, count * sizeof(uint32_t), sizeof(uint32_t));
        auto indexed = zero->drawIndexedIndirectCount(GVM::RHI::BufferRange(indices), GVM::RHI::IndexFormat::Uint32,
            commandRange, countRange, 1);
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass(passDescriptor);
        render->setVertexBuffer(GVM::RHI::BufferRange(vertices), 0);
        indexed.drawFn(render);
        render->end();
        render.reset();
        encoder->end();
        device->getMainQueue()->submit({encoder});
        eastl::array<uint8_t, 4> pixel{};
        queue->readTexture(image, pixel.data(), sizeof(pixel))->submit();
        EXPECT_EQ(pixel[2], count == 0 ? 0u : 255u);
        eastl::array<uint32_t, 2> result{};
        queue->writeBuffer(output, result.data(), sizeof(result))->submit();
        queue->renderPass("SlotGpuIndirectCount", target, slot->drawIndirectCount(commandRange, countRange, 1))->submit();
        queue->readBuffer(output, result.data(), sizeof(result))->submit();
        EXPECT_EQ(result[1], count == 0 ? 0u : 99u);
    }
}

/** Applies deferred Slot changes in order for copied and reused Count tasks after their render class is released. */
TEST_F(ConstructorArgumentsTest, AppliesSlotChangesInRecordingOrderForGpuCountTasks)
{
    if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan &&
        !static_cast<GVM::RHI::Vulkan::VKDevice *>(device)->getCapabilities().supportsDrawIndirectCount())
    {
        GTEST_SKIP() << "The native Vulkan device does not expose drawIndirectCount.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    for (bool indexed : {false, true})
    {
        SCOPED_TRACE(indexed);
        const eastl::array<GVM::RHI::Buffer, 3> outputs{
            createArgumentOutput(proxy), createArgumentOutput(proxy), createArgumentOutput(proxy)};
        const eastl::array<uint32_t, 2> initial{};
        for (const auto &output : outputs)
        {
            queue->writeBuffer(output, initial.data(), sizeof(initial))->submit();
        }
        auto source = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst,
            .size = 36});
        const eastl::array<uint32_t, 9> commands{0, 1, 0, 0, 3, 1, 0, 0, 0};
        queue->writeBuffer(source, commands.data(), sizeof(commands))->submit();
        auto indices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst,
            .size = 12});
        const eastl::array<uint32_t, 3> indexValues{0, 1, 2};
        queue->writeBuffer(indices, indexValues.data(), sizeof(indexValues))->submit();
        auto firstGroup = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(outputs[0]));
        auto secondGroup = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(outputs[1]));
        auto thirdGroup = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(outputs[2]));
        auto slot = proxy->createRenderClass<ArgumentSlotWriteRaster>(firstGroup);
        const GVM::RHI::BufferRange commandRange(source, 16, 20);
        const GVM::RHI::BufferRange countRange(source, 4, 4);
        auto draw = indexed ?
            slot->drawIndexedIndirectCount(GVM::RHI::BufferRange(indices), GVM::RHI::IndexFormat::Uint32,
                commandRange, countRange, 1) :
            slot->drawIndirectCount(commandRange, countRange, 1);
        auto copy = draw;
        auto changeSecond = slot->setBindGroup(0, secondGroup);
        auto changeThird = slot->setBindGroup(0, thirdGroup);
        slot.reset();
        firstGroup.reset();
        secondGroup.reset();
        thirdGroup.reset();
        const eastl::array<GVM::Core::RenderPassTaskDescriptor, 5> tasks{
            draw, changeSecond, copy, changeThird, draw};
        auto *recorded = queue->renderPass("DeferredSlotGpuCountChanges", target, tasks);
        device->freeBuffer(source);
        device->freeBuffer(indices);
        recorded->submit();
        for (const auto &output : outputs)
        {
            eastl::array<uint32_t, 2> result{};
            queue->readBuffer(output, result.data(), sizeof(result))->submit();
            EXPECT_EQ(result[0], 0u);
            EXPECT_EQ(result[1], 99u);
        }
    }
}

/** Passes a Slot bind group's nonzero buffer offset through to the bound resource range. */
TEST_F(ConstructorArgumentsTest, AppliesSlotBindGroupBufferRangeOffset)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto storage = proxy->createBuffer({.label = "SlotRangeOffsetStorage",
        .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
        .size = 384});
    eastl::array<uint32_t, 96> sentinel{};
    sentinel.fill(0xABu);
    queue->writeBuffer(storage, sentinel.data(), sizeof(sentinel))->submit();
    auto bindings = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(storage, 256, 64));
    auto pass = proxy->createComputeClass<ArgumentSlotCompute>(bindings);
    queue->computePass("SlotBindGroupRangeOffset", pass->run(16u, 1u, 1u))->submit();
    eastl::array<uint32_t, 96> words{};
    queue->readBuffer(storage, words.data(), sizeof(words))->submit();
    for (uint32_t index = 0; index < 64u; ++index)
    {
        EXPECT_EQ(words[index], 0xABu) << index;
    }
    for (uint32_t index = 0; index < 16u; ++index)
    {
        EXPECT_EQ(words[64u + index], 77u + index) << index;
    }
    for (uint32_t index = 80u; index < 96u; ++index)
    {
        EXPECT_EQ(words[index], 0xABu) << index;
    }
    device->freeBuffer(storage);
}

/** Executes deferred direct, rebinding, and indirect Slot compute tasks after the shader class is released. */
TEST_F(ConstructorArgumentsTest, KeepsComputeSlotTasksAliveAfterClassRelease)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto first = createArgumentOutput(proxy);
    auto second = createArgumentOutput(proxy);
    eastl::array<uint32_t, 8> sentinel{};
    sentinel.fill(0xCDu);
    queue->writeBuffer(second, sentinel.data(), sizeof(sentinel))->submit();
    auto commands = proxy->createBuffer({.label = "ReleasedSlotTaskCounts",
        .usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst, .size = 12});
    const eastl::array<uint32_t, 3> counts{4u, 1u, 1u};
    queue->writeBuffer(commands, counts.data(), sizeof(counts))->submit();
    auto firstGroup = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(first));
    auto secondGroup = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(second));
    auto pass = proxy->createComputeClass<ArgumentSlotCompute>(firstGroup);
    auto direct = pass->run(8u, 1u, 1u);
    auto change = pass->setBindGroup(0, secondGroup);
    auto indirect = pass->run(GVM::RHI::BufferRange(commands));
    pass.reset();
    firstGroup.reset();
    secondGroup.reset();
    const eastl::array<GVM::Core::ComputePassTaskDescriptor, 3> tasks{direct, change, indirect};
    queue->computePass("ReleasedComputeSlotTasks", tasks)->submit();
    eastl::array<uint32_t, 8> words{};
    queue->readBuffer(first, words.data(), sizeof(words))->submit();
    for (uint32_t index = 0; index < 8u; ++index)
    {
        EXPECT_EQ(words[index], 77u + index) << index;
    }
    queue->readBuffer(second, words.data(), sizeof(words))->submit();
    for (uint32_t index = 0; index < 4u; ++index)
    {
        EXPECT_EQ(words[index], 77u + index) << index;
    }
    for (uint32_t index = 4u; index < 8u; ++index)
    {
        EXPECT_EQ(words[index], 0xCDu) << index;
    }
    device->freeBuffer(commands);
}

/** Executes deferred draw and rebinding Slot render tasks after the shader class is released. */
TEST_F(ConstructorArgumentsTest, KeepsRenderSlotTasksAliveAfterClassRelease)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto first = createArgumentOutput(proxy);
    auto second = createArgumentOutput(proxy);
    eastl::array<uint32_t, 4> sentinel{};
    sentinel.fill(0xCDu);
    queue->writeBuffer(first, sentinel.data(), sizeof(sentinel));
    queue->writeBuffer(second, sentinel.data(), sizeof(sentinel))->submit();
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    auto firstGroup = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(first));
    auto secondGroup = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(second));
    auto raster = proxy->createRenderClass<ArgumentSlotWriteRaster>(firstGroup);
    auto draw = raster->run(3u, 1u, 0u, 0u);
    auto change = raster->setBindGroup(0, secondGroup);
    raster.reset();
    firstGroup.reset();
    secondGroup.reset();
    const eastl::array<GVM::Core::RenderPassTaskDescriptor, 3> tasks{draw, change, draw};
    queue->renderPass("ReleasedRenderSlotTasks", target, tasks)->submit();
    eastl::array<uint32_t, 4> firstWords{};
    queue->readBuffer(first, firstWords.data(), sizeof(firstWords))->submit();
    EXPECT_EQ(firstWords[0], 0xCDu);
    EXPECT_EQ(firstWords[1], 99u);
    eastl::array<uint32_t, 4> secondWords{};
    queue->readBuffer(second, secondWords.data(), sizeof(secondWords))->submit();
    EXPECT_EQ(secondWords[0], 0xCDu);
    EXPECT_EQ(secondWords[1], 99u);
    device->freeBuffer(first);
    device->freeBuffer(second);
}

/** Exercises input-attachment descriptor set zero with a constant-only Args invocation. */
TEST_F(ConstructorArgumentsTest, DrawsPixelLocalArgsWithAConstantOnlyRoot)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto texture = proxy->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {2, 2, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto albedo = proxy->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::PixelLocalAttachment,
        .size = {2, 2, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        .storageMode = GVM::RHI::TextureStorageMode::TransientAttachment});
    ArgumentPixelFrame frame;
    frame.albedo = albedo->createView();
    frame.present = texture->createView();
    auto raster = proxy->createRenderClass<ArgumentPixelRaster>(float4(0));
    auto resolve = proxy->createRenderClass<ArgumentPixelResolve>(float4(0));
    auto pixel = resolve->draw(float4(0, 0.5f, 0, 0));
    EXPECT_EQ(pixel.phaseRequirement, GVM::Core::RenderPassPhaseRequirement::PixelLocalOnly);
    EXPECT_THROW(queue->renderPass("InvalidOrdinaryPixelArgs", frame, pixel), std::invalid_argument);
    auto draw = raster->draw(float4(1, 0, 0, 1), 3, 1, 0, 0);
    raster.reset();
    resolve.reset();
    queue->renderPass("ConstantPixelLocalArgs", frame, GVM::Core::pixelLocalPass(
        eastl::move(draw), GVM::Core::nextPixelLocalPass(), eastl::move(pixel)))->submit();
    eastl::array<uint8_t, 16> pixels{};
    queue->readTexture(texture, pixels.data(), pixels.size())->submit();
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        EXPECT_EQ(pixels[offset], 255u);
        EXPECT_NEAR(pixels[offset + 1], 128u, 1u);
        EXPECT_EQ(pixels[offset + 2], 0u);
        EXPECT_EQ(pixels[offset + 3], 255u);
    }
}

/** Keeps the Args resource table separate from pixel-local inputs after sampled handles are freed. */
TEST_F(ConstructorArgumentsTest, DrawsPixelLocalArgsWithCapturedTextureAndSampler)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto image = createArgumentTexture(*device);
    auto sampler = createArgumentSampler(*device);
    const eastl::array<uint8_t, 16> imagePixels{0, 0, 64, 0, 0, 0, 64, 0, 0, 0, 64, 0, 0, 0, 64, 0};
    queue->writeTexture(image, imagePixels.data(), imagePixels.size())->submit();
    auto texture = proxy->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {2, 2, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto albedo = proxy->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::PixelLocalAttachment,
        .size = {2, 2, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        .storageMode = GVM::RHI::TextureStorageMode::TransientAttachment});
    ArgumentPixelFrame frame;
    frame.albedo = albedo->createView();
    frame.present = texture->createView();
    auto raster = proxy->createRenderClass<ArgumentPixelRaster>(float4(0));
    auto resolve = proxy->createRenderClass<ArgumentPixelTextureResolve>(image->createView(), sampler, float4(0));
    auto pixel = resolve->draw(image->createView(), sampler, float4(0, 0.5f, 0, 0));
    auto draw = raster->draw(float4(1, 0, 0, 1), 3, 1, 0, 0);
    device->freeTexture(image);
    device->freeSampler(sampler);
    raster.reset();
    resolve.reset();
    queue->renderPass("ResourcePixelLocalArgs", frame, GVM::Core::pixelLocalPass(
        eastl::move(draw), GVM::Core::nextPixelLocalPass(), eastl::move(pixel)))->submit();
    eastl::array<uint8_t, 16> pixels{};
    queue->readTexture(texture, pixels.data(), pixels.size())->submit();
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        EXPECT_EQ(pixels[offset], 255u);
        EXPECT_NEAR(pixels[offset + 1], 128u, 1u);
        EXPECT_EQ(pixels[offset + 2], 64u);
        EXPECT_EQ(pixels[offset + 3], 255u);
    }
}

/** Captures zero-prefix control resources without a GPU parameter page or vertex-buffer overwrite. */
TEST_F(ConstructorArgumentsTest, DrawsZeroArgumentIndexedTasksWithoutAParameterPage)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto indices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index | GVM::RHI::BufferUsage::CopyDst, .size = 6});
    auto vertices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::CopyDst,
        .size = sizeof(ArgumentVertexInput) * 3});
    const eastl::array<uint16_t, 3> indexData{0, 1, 2};
    const eastl::array<ArgumentVertexInput, 3> vertexData{{{float2(-1, -1), 0}, {float2(3, -1), 0}, {float2(-1, 3), 0}}};
    queue->writeBuffer(indices, indexData.data(), sizeof(indexData));
    queue->writeBuffer(vertices, vertexData.data(), sizeof(vertexData))->submit();
    auto raster = proxy->createRenderClass<ArgumentZeroRaster>();
    device->setArgumentMemoryBudget(1);
    auto draw = raster->drawIndexed(GVM::RHI::BufferRange(indices), GVM::RHI::IndexFormat::Uint16, 3, 1, 0, 0, 0);
    raster.reset();
    GVM::Core::RenderPassTaskDescriptor bindVertices;
    bindVertices.drawFn = [vertices](GVM::RHI::RenderPassEncoder pass)
    {
        pass->setVertexBuffer(GVM::RHI::BufferRange(vertices), 0);
    };
    auto texture = proxy->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = texture->createView();
    const eastl::array<GVM::Core::RenderPassTaskDescriptor, 2> tasks{bindVertices, draw};
    auto *recorded = queue->renderPass("ZeroArgsCapturedIndex", target, tasks);
    device->freeBuffer(indices);
    recorded->submit();
    eastl::array<uint8_t, 4> pixel{};
    queue->readTexture(texture, pixel.data(), pixel.size())->submit();
    EXPECT_EQ(pixel[0], 0u);
    EXPECT_EQ(pixel[2], 255u);
    EXPECT_EQ(pixel[3], 255u);
}

/** Rejects indirect inputs that cannot be resolved when the task is recorded into a pass. */
TEST_F(ConstructorArgumentsTest, RejectsInvalidIndirectArgumentCapture)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentWorkgroupCompute>(GVM::RHI::BufferRange(output), 0u);
    auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect, .size = 32});
    auto nullTask = pipeline->dispatchWorkgroupsIndirect(GVM::RHI::BufferRange(output), 0u, GVM::RHI::BufferRange{});
    auto wrongUsage = pipeline->dispatchWorkgroupsIndirect(GVM::RHI::BufferRange(output), 0u, GVM::RHI::BufferRange(output));
    auto misaligned = pipeline->dispatchWorkgroupsIndirect(GVM::RHI::BufferRange(output), 0u,
        GVM::RHI::BufferRange(commands, 2, 12));
    GVM::RHI::BufferRange invalid(commands);
    invalid.size = 36;
    auto outOfBounds = pipeline->dispatchWorkgroupsIndirect(GVM::RHI::BufferRange(output), 0u, invalid);
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto pass = encoder->beginComputePass({});
    EXPECT_THROW(nullTask.dispatchFn(pass), std::invalid_argument);
    EXPECT_THROW(wrongUsage.dispatchFn(pass), std::invalid_argument);
    EXPECT_THROW(misaligned.dispatchFn(pass), std::out_of_range);
    EXPECT_THROW(outOfBounds.dispatchFn(pass), std::out_of_range);
    pass->end();
    encoder->end();
}

/** Rejects invalid indexed inputs when the deferred task is recorded into a pass. */
TEST_F(ConstructorArgumentsTest, RejectsInvalidIndexedArgumentCapture)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto raster = proxy->createRenderClass<ArgumentIndexedRaster>(float4(0), GVM::RHI::BufferRange(output), 0u);
    auto indices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index, .size = 16});
    auto nullIndices = raster->drawIndexed(float4(1), GVM::RHI::BufferRange(output), 0u, GVM::RHI::BufferRange{},
        GVM::RHI::IndexFormat::Uint32, 3, 1, 0, 0, 0);
    auto wrongUsage = raster->drawIndexed(float4(1), GVM::RHI::BufferRange(output), 0u, GVM::RHI::BufferRange(output),
        GVM::RHI::IndexFormat::Uint32, 3, 1, 0, 0, 0);
    auto undefinedFormat = raster->drawIndexed(float4(1), GVM::RHI::BufferRange(output), 0u, GVM::RHI::BufferRange(indices),
        GVM::RHI::IndexFormat::Undefined, 3, 1, 0, 0, 0);
    auto misaligned = raster->drawIndexed(float4(1), GVM::RHI::BufferRange(output), 0u, GVM::RHI::BufferRange(indices, 1, 8),
        GVM::RHI::IndexFormat::Uint32, 3, 1, 0, 0, 0);
    GVM::RHI::BufferRange invalid(indices);
    invalid.size = 20;
    auto outOfBounds = raster->drawIndexed(float4(1), GVM::RHI::BufferRange(output), 0u, invalid,
        GVM::RHI::IndexFormat::Uint32, 3, 1, 0, 0, 0);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    EXPECT_THROW(nullIndices.drawFn(render), std::invalid_argument);
    EXPECT_THROW(wrongUsage.drawFn(render), std::invalid_argument);
    EXPECT_THROW(undefinedFormat.drawFn(render), std::invalid_argument);
    EXPECT_THROW(misaligned.drawFn(render), std::out_of_range);
    EXPECT_THROW(outOfBounds.drawFn(render), std::out_of_range);
    render->end();
    encoder->end();
}

/** Checks cached feature and count restrictions without submitting commands unsupported by the native test device. */
TEST_F(ConstructorArgumentsTest, RejectsIndirectCountsOutsideCachedDeviceCapabilities)
{
    if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
    {
        GTEST_SKIP() << "Exercises cached Vulkan recording limits.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect, .size = 64});
    auto indices = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Index, .size = 12});
    auto raster = proxy->createRenderClass<ArgumentIndexedRaster>(float4(1), GVM::RHI::BufferRange(output), 0u);
    auto indirect = raster->drawIndirect(float4(1), GVM::RHI::BufferRange(output), 0u, GVM::RHI::BufferRange(commands), 2, 16);
    auto indexed = raster->drawIndexedIndirect(float4(1), GVM::RHI::BufferRange(output), 0u,
        GVM::RHI::BufferRange(indices), GVM::RHI::IndexFormat::Uint32, GVM::RHI::BufferRange(commands), 2, 20);
    auto color = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = color->createView();
    auto &nativeDevice = *static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
    // Only this isolated test device's CPU recording cache changes; no invalid native command is submitted.
    auto &limits = const_cast<vk::PhysicalDeviceProperties &>(nativeDevice.getCapabilities().getPhysicalDeviceProperties()).limits;
    auto &features = const_cast<vk::PhysicalDeviceFeatures &>(nativeDevice.getCapabilities().getEnabledShaderFeatures());
    const uint32_t originalLimit = limits.maxDrawIndirectCount;
    const auto originalFeature = features.multiDrawIndirect;
    for (bool featureSupported : {false, true})
    {
        features.multiDrawIndirect = featureSupported ? VK_TRUE : VK_FALSE;
        limits.maxDrawIndirectCount = featureSupported ? 1 : 8;
        // The pass recorder snapshots device limits when the pass begins, so each pass is recorded after mutating the cache.
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
        EXPECT_THROW(indirect.drawFn(render), std::out_of_range);
        EXPECT_THROW(indexed.drawFn(render), std::out_of_range);
        EXPECT_THROW(render->drawIndirect(GVM::RHI::BufferRange(commands), 2, 16), std::out_of_range);
        EXPECT_THROW(render->drawIndexedIndirect(GVM::RHI::BufferRange(commands), 2, 20), std::out_of_range);
        render->end();
        encoder->end();
    }
    limits.maxDrawIndirectCount = originalLimit;
    features.multiDrawIndirect = originalFeature;
}

/** Rejects shader resource fields in zero-byte layouts; execution resources do not belong to Args. */
TEST_F(ConstructorArgumentsTest, RejectsShaderResourceFieldsInZeroByteLayouts)
{
    GVM::RHI::PipelineLayoutDescriptor layout{};
    layout.arguments.buffers.push_back({});
    EXPECT_THROW(device->createPipelineLayout(layout), std::invalid_argument);
    layout.arguments.buffers.clear();
    GVM::RHI::ArgumentResourceLayout field{};
    field.entry.texture.sampleType = GVM::RHI::TextureSampleType::Float;
    field.entry.texture.viewDimension = GVM::RHI::TextureViewDimension::e2D;
    layout.arguments.resources.push_back(field);
    EXPECT_THROW(device->createPipelineLayout(layout), std::invalid_argument);
    layout.arguments.resources.clear();
    field.entry.texture.sampleType = GVM::RHI::TextureSampleType::Undefined;
    field.entry.sampler.type = GVM::RHI::SamplerBindingType::Filtering;
    layout.arguments.resources.push_back(field);
    EXPECT_THROW(device->createPipelineLayout(layout), std::invalid_argument);
    layout.arguments.resources.clear();
    EXPECT_NO_THROW(device->createPipelineLayout(layout));
}

/** Establishes explicit GPU-to-host visibility for a buffer retained only by Args. */
TEST_F(ConstructorArgumentsTest, MakesArgumentWritesVisibleToDirectMappedReadback)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead,
        .size = sizeof(uint32_t) * 4});
    auto pipeline = proxy->createComputeClass<ArgumentWorkgroupCompute>(GVM::RHI::BufferRange(output), 0u);
    auto queue = proxy->graphicsQueue(0);
    queue->computePass("ArgsDirectMappedOutput", pipeline->dispatchThreads(GVM::RHI::BufferRange(output), 71u, 4, 1, 1))->submit();
    if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan)
    {
        const auto state = GVM::RHI::Vulkan::Testing::getCurrentBufferState(device->getMainQueue(), output);
        EXPECT_NE(state.stageMaskBits & uint64_t(VK_PIPELINE_STAGE_2_HOST_BIT), 0u);
        EXPECT_NE(state.accessMaskBits & uint64_t(VK_ACCESS_2_HOST_READ_BIT), 0u);
    }
    eastl::array<uint32_t, 4> words{};
    queue->readBuffer(output, words.data(), sizeof(words))->submit();
    for (uint32_t index = 0; index < words.size(); ++index)
    {
        EXPECT_EQ(words[index], 71u + index);
    }
}

/** Confirms that an external Metal validation launch instruments the native pipeline before claiming validation coverage. */
TEST_F(ConstructorArgumentsTest, RequiresNativeShaderValidationWhenRequestedOnMetal)
{
#if defined(__APPLE__)
    if (instance->getBackend() != GVM::RHI::GraphicsBackend::Metal || !GVM::Tests::shaderReadbackValidationEnabled)
    {
        GTEST_SKIP() << "Requires an explicit Metal validation launch through the external Xcode scheme.";
    }
    const auto shader = device->createShaderModule({.code = "#include <metal_stdlib>\nusing namespace metal;\nkernel void nativeValidationProbe()\n{\n}\n"});
    const auto pipeline = device->createComputePipeline({.compute = {.module = shader, .entryPoint = "nativeValidationProbe", .workgroupX = 1, .workgroupY = 1, .workgroupZ = 1}});
    auto *native = static_cast<GVM::RHI::Metal::MComputePipeline *>(pipeline.get())->getNativePipelineState();
    const bool enabled = native->shaderValidation() == MTL::ShaderValidationEnabled;
    RecordProperty("metalShaderValidation", enabled ? "enabled" : "disabled");
    ASSERT_TRUE(enabled) << "The external Apple shader validation layer must instrument the pipeline.";
#else
    GTEST_SKIP() << "Metal validation is available only on Apple platforms.";
#endif
}

TEST_F(ConstructorArgumentsTest, RejectsInvalidRangesAndBufferUsageWhenCreatingTheTask)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto data = makeArgumentData(0);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), data);
    EXPECT_THROW(pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 128, 4), data, 1, 1, 1), std::out_of_range);
    GVM::RHI::BufferRange invalidRange(output);
    invalidRange.offset = 124; invalidRange.size = 8;
    EXPECT_THROW(pipeline->dispatchThreads(invalidRange, data, 1, 1, 1), std::out_of_range);
    auto vertexOnly = proxy->createBuffer({.label = "ArgsInvalidUsage", .usage = GVM::RHI::BufferUsage::Vertex, .size = 128});
    EXPECT_THROW(pipeline->dispatchThreads(GVM::RHI::BufferRange(vertexOnly), data, 1, 1, 1), std::invalid_argument);
}

TEST_F(ConstructorArgumentsTest, ReusesOneTaskAcrossSeparateSubmissionsWithoutRecreatingThePipeline)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto data = makeArgumentData(7);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), data);
    auto task = pipeline->dispatchThreads(GVM::RHI::BufferRange(output), data, 1, 1, 1);
    auto queue = proxy->graphicsQueue(0);
    for (uint32_t index = 0; index < 3; ++index)
    {
        uint32_t word = 0;
        queue->writeBuffer(output, &word, sizeof(word))->submit();
        const eastl::array<GVM::Core::ComputePassTaskDescriptor, 1> tasks = {task};
        queue->computePass("ArgsRepeatedTask", tasks)->submit();
        queue->readBuffer(output, &word, sizeof(word))->submit();
        EXPECT_EQ(word, 148u);
    }
    EXPECT_EQ(pipeline->creations, 1u);
}

TEST_F(ConstructorArgumentsTest, EvaluatesEachInvocationArgumentOnceAndRequiresCreationInputs)
{
    static_assert(!std::is_default_constructible_v<ArgumentCompute>);
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto data = makeArgumentData(0);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), data);
    uint32_t evaluations = 0;
    auto task = pipeline->dispatchThreads(GVM::RHI::BufferRange(output), makeArgumentData(++evaluations), 1, 1, 1);
    EXPECT_EQ(evaluations, 1u);
    auto queue = proxy->graphicsQueue(0);
    queue->computePass("ArgsSingleEvaluation", eastl::move(task))->submit();
    uint32_t result = 0;
    queue->readBuffer(output, &result, sizeof(result))->submit();
    EXPECT_EQ(result, 142u);
    EXPECT_EQ(evaluations, 1u);
}

/** Verifies that tasks cannot silently bind native resources through another logical RHI device. */
TEST_F(ConstructorArgumentsTest, RejectsRecordingArgumentsOnAnotherDevice)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto data = makeArgumentData(0);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), data);
    auto task = pipeline->dispatchThreads(GVM::RHI::BufferRange(output), data, 1, 1, 1);
    const GVM::RHI::InstanceDescriptor descriptor{.preferredBackend = GVM::Tests::shaderReadbackBackend};
    eastl::unique_ptr<GVM::RHI::InstanceImpl, void (*)(GVM::RHI::Instance)> otherInstance(
        GVM::RHI::createInstance(descriptor), GVM::RHI::destroyInstance);
    auto other = otherInstance->createDevice();
    ASSERT_NE(other, nullptr);
    ASSERT_NE(other, device);
    auto encoder = other->getMainQueue()->createCommandEncoder();
    auto pass = encoder->beginComputePass({});
    EXPECT_THROW(task.dispatchFn(pass), std::invalid_argument);
    pass->end();
    encoder->end();
}

/** Resolves dependencies from submission order even when consumers were recorded before producers. */
TEST_F(ConstructorArgumentsTest, ReadsBufferWritesInFinalSubmissionOrder)
{
    GVM::Core::DeviceProxy proxy(device);
    auto intermediate = createArgumentOutput(proxy);
    auto output = createArgumentOutput(proxy);
    const auto data = makeArgumentData(10);
    auto writer = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(intermediate), data);
    auto reader = proxy->createComputeClass<ArgumentReadCompute>(GVM::RHI::BufferRange(intermediate), GVM::RHI::BufferRange(output), 9u);
    auto consume = recordArgumentDispatch(*device, reader->dispatchThreads(GVM::RHI::BufferRange(intermediate, 16, 8), GVM::RHI::BufferRange(output), 9u, 2, 1, 1));
    auto produce = recordArgumentDispatch(*device, writer->dispatchThreads(GVM::RHI::BufferRange(intermediate, 16, 8), data, 2, 1, 1));
    device->getMainQueue()->submit({produce, consume});
    eastl::array<uint32_t, 2> result{};
    proxy->graphicsQueue(0)->readBuffer(output, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 160u);
    EXPECT_EQ(result[1], 161u);
}

/** Makes a compute write visible to a fragment-stage BDA read in the following submitted render pass. */
TEST_F(ConstructorArgumentsTest, DrawReadsComputeWritesThroughArguments)
{
    GVM::Core::DeviceProxy proxy(device);
    auto intermediate = createArgumentOutput(proxy);
    const auto data = makeArgumentData(10);
    auto writer = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(intermediate), data);
    auto raster = proxy->createRenderClass<ArgumentReadRaster>(GVM::RHI::BufferRange(intermediate));
    auto texture = proxy->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {4, 4, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = texture->createView();
    auto consume = device->getMainQueue()->createCommandEncoder();
    auto render = consume->beginRenderPass(target.getRenderPassDescriptor());
    raster->draw(GVM::RHI::BufferRange(intermediate), 3, 1, 0, 0).drawFn(render);
    render->end();
    render.reset();
    consume->end();
    auto produce = recordArgumentDispatch(*device, writer->dispatchThreads(GVM::RHI::BufferRange(intermediate), data, 1, 1, 1));
    device->getMainQueue()->submit({produce, consume});
    eastl::array<uint8_t, 4 * 4 * 4> pixels{};
    proxy->graphicsQueue(0)->readTexture(texture, pixels.data(), pixels.size())->submit();
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
    {
        EXPECT_NEAR(pixels[offset], 151, 1);
        EXPECT_EQ(pixels[offset + 1], 0u);
        EXPECT_EQ(pixels[offset + 2], 0u);
        EXPECT_EQ(pixels[offset + 3], 255u);
    }
}

/** Releases submission pages on completion while caller-owned command encoder objects remain alive. */
TEST_F(ConstructorArgumentsTest, ReclaimsPagesAcrossMultipleSubmittedEncoders)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), makeArgumentData(0));
    device->setArgumentMemoryBudget(512 * 1024);
    eastl::vector<GVM::RHI::CommandEncoder> encoders;
    for (uint32_t index = 0; index < 4; ++index)
    {
        encoders.push_back(recordArgumentDispatch(*device,
            pipeline->dispatchThreads(GVM::RHI::BufferRange(output, index * 16, 4), makeArgumentData(index * 10), 1, 1, 1)));
        device->getMainQueue()->submit({encoders.back()});
    }
    eastl::array<uint32_t, 16> words{};
    proxy->graphicsQueue(0)->readBuffer(output, words.data(), sizeof(words))->submit();
    for (uint32_t index = 0; index < 4; ++index)
    {
        EXPECT_EQ(words[index * 4], 141u + index * 10);
    }
    EXPECT_NO_THROW(device->setArgumentMemoryBudget(512 * 1024));
}

/** Preserves normal local resource copy, assignment, and by-value helper semantics on both backends. */
TEST_F(ConstructorArgumentsTest, CopiesAssignsAndPassesResourceHandlesByValue)
{
    GVM::Core::DeviceProxy proxy(device);
    auto first = createArgumentOutput(proxy);
    auto second = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentHandleCompute>(GVM::RHI::BufferRange(first), GVM::RHI::BufferRange(second), 0u);
    auto queue = proxy->graphicsQueue(0);
    auto selectFirst = pipeline->dispatchThreads(GVM::RHI::BufferRange(first), GVM::RHI::BufferRange(second), 0u, 2, 1, 1);
    auto selectSecond = pipeline->dispatchThreads(GVM::RHI::BufferRange(first), GVM::RHI::BufferRange(second), 31u, 2, 1, 1);
    queue->computePass("ArgsLocalResourceSelection", eastl::move(selectFirst), eastl::move(selectSecond))->submit();
    eastl::array<uint32_t, 2> result{};
    queue->readBuffer(first, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 0u);
    EXPECT_EQ(result[1], 1u);
    queue->readBuffer(second, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 31u);
    EXPECT_EQ(result[1], 32u);
}

/** Resolves helper parameters and local resource copies before identically named top-level Args fields. */
TEST_F(ConstructorArgumentsTest, PreservesShadowedBufferParameterAndLocalValues)
{
    GVM::Core::DeviceProxy proxy(device);
    auto first = createArgumentOutput(proxy);
    auto second = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentShadowedHandleCompute>(
        GVM::RHI::BufferRange(first), GVM::RHI::BufferRange(second), 29u);
    auto queue = proxy->graphicsQueue(0);
    eastl::array<uint32_t, 4> words{};
    queue->writeBuffer(first, words.data(), sizeof(words))->submit();
    queue->writeBuffer(second, words.data(), sizeof(words))->submit();
    queue->computePass("ArgsShadowedResourceValues", pipeline->dispatchThreads(
        GVM::RHI::BufferRange(first), GVM::RHI::BufferRange(second), 29u, 2, 1, 1))->submit();
    queue->readBuffer(first, words.data(), sizeof(words))->submit();
    for (const auto value : words)
    {
        EXPECT_EQ(value, 0u);
    }
    queue->readBuffer(second, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[0], 17u);
    EXPECT_EQ(words[1], 18u);
    EXPECT_EQ(words[2], 29u);
    EXPECT_EQ(words[3], 30u);
}

/** Keeps nested resources attached to the aggregate passed through helpers, local copies, and lambdas. */
TEST_F(ConstructorArgumentsTest, PreservesShadowedAggregateResourceValues)
{
    GVM::Core::DeviceProxy proxy(device);
    auto first = createArgumentOutput(proxy);
    auto second = createArgumentOutput(proxy);
    const ArgumentShadowedPayload material{5u, GVM::RHI::BufferRange(first)};
    const ArgumentShadowedPayload other{37u, GVM::RHI::BufferRange(second)};
    auto pipeline = proxy->createComputeClass<ArgumentShadowedAggregateCompute>(material, other);
    auto queue = proxy->graphicsQueue(0);
    eastl::array<uint32_t, 6> words{};
    queue->writeBuffer(first, words.data(), sizeof(words))->submit();
    queue->writeBuffer(second, words.data(), sizeof(words))->submit();
    queue->computePass("ArgsShadowedAggregateValues", pipeline->dispatchThreads(material, other, 2, 1, 1))->submit();
    queue->readBuffer(first, words.data(), sizeof(words))->submit();
    for (const auto value : words)
    {
        EXPECT_EQ(value, 0u);
    }
    queue->readBuffer(second, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[0], 37u);
    EXPECT_EQ(words[1], 38u);
    EXPECT_EQ(words[2], 47u);
    EXPECT_EQ(words[3], 48u);
    EXPECT_EQ(words[4], 57u);
    EXPECT_EQ(words[5], 58u);
}

/** Keeps ordinary object methods bound to their receiver while shader fields continue to read root Args. */
TEST_F(ConstructorArgumentsTest, DistinguishesObjectMembersFromShaderArguments)
{
    GVM::Core::DeviceProxy proxy(device);
    auto objectOutput = createArgumentOutput(proxy);
    auto shaderOutput = createArgumentOutput(proxy);
    const ArgumentShadowedPayload payload{41u, GVM::RHI::BufferRange(objectOutput)};
    auto pipeline = proxy->createComputeClass<ArgumentShadowedMemberCompute>(
        payload, 97u, GVM::RHI::BufferRange(shaderOutput));
    auto queue = proxy->graphicsQueue(0);
    eastl::array<uint32_t, 4> words{};
    queue->writeBuffer(objectOutput, words.data(), sizeof(words))->submit();
    queue->writeBuffer(shaderOutput, words.data(), sizeof(words))->submit();
    queue->computePass("ArgsDistinctObjectReceiver", pipeline->dispatchThreads(
        payload, 97u, GVM::RHI::BufferRange(shaderOutput), 2, 1, 1))->submit();
    queue->readBuffer(objectOutput, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[0], 41u);
    EXPECT_EQ(words[1], 42u);
    queue->readBuffer(shaderOutput, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[0], 41u);
    EXPECT_EQ(words[1], 41u);
    EXPECT_EQ(words[2], 97u);
    EXPECT_EQ(words[3], 97u);
}

/** Preserves typed atomic output stores for Args buffers and evaluates each output lvalue once. */
TEST_F(ConstructorArgumentsTest, WritesAtomicCompareExchangeResultsToBuffersAndLocals)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentAtomicCompute>(GVM::RHI::BufferRange(output));
    auto queue = proxy->graphicsQueue(0);
    eastl::array<uint32_t, 8> words{};
    queue->writeBuffer(output, words.data(), sizeof(words))->submit();
    queue->computePass("ArgsAtomicOriginalOutputs", pipeline->dispatchThreads(
        GVM::RHI::BufferRange(output), 1, 1, 1))->submit();
    queue->readBuffer(output, words.data(), sizeof(words))->submit();
    EXPECT_EQ(words[0], 17u);
    EXPECT_EQ(words[1], 0u);
    EXPECT_EQ(words[2], 7u);
    EXPECT_EQ(words[3], 7u);
    EXPECT_EQ(words[4], 13u);
    EXPECT_EQ(words[5], 5u);
    EXPECT_EQ(words[6], 23u);
    EXPECT_EQ(words[7], 31u);
}

/** Enables and exercises fragment buffer stores without adding a second invocation or validation dispatch. */
TEST_F(ConstructorArgumentsTest, WritesBufferContentsFromFragmentArguments)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto texture = proxy->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = texture->createView();
    auto raster = proxy->createRenderClass<ArgumentWriteRaster>(GVM::RHI::BufferRange(output), 0u);
    auto queue = proxy->graphicsQueue(0);
    queue->renderPass("ArgsFragmentStore", target, raster->draw(GVM::RHI::BufferRange(output), 79u, 3, 1, 0, 0))->submit();
    uint32_t result = 0;
    queue->readBuffer(output, &result, sizeof(result))->submit();
    EXPECT_EQ(result, 79u);
}

/** Keeps render writes pending across disjoint Args and Slot uses recorded inside one render pass. */
TEST_F(ConstructorArgumentsTest, PreservesRenderWritesAcrossOverlappingArgumentSnapshots)
{
    if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
    {
        GTEST_SKIP() << "Queue resource-state observations are specific to the Vulkan backend.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto buffer = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Storage |
        GVM::RHI::BufferUsage::CopyDst, .size = 32});
    auto writer = proxy->createRenderClass<ArgumentWriteRaster>(GVM::RHI::BufferRange(buffer, 0, 4), 99u);
    auto group = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(buffer, 16, 8));
    auto slotWriter = proxy->createRenderClass<ArgumentSlotWriteRaster>(group);
    auto reader = proxy->createRenderClass<ArgumentReadRaster>(GVM::RHI::BufferRange(buffer));
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = image->createView();
    for (bool useSlot : {false, true})
    {
        SCOPED_TRACE(useSlot ? "Slot writer and Args reader" : "Separate Args snapshots");
        const eastl::array<uint32_t, 4> initial{10u, 0u, 0u, 0u};
        queue->writeBuffer(buffer, initial.data(), sizeof(initial))->submit();
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
        if (useSlot)
        {
            // The bound Slot group stays visible to every later draw in the pass, so the reader runs first.
            reader->draw(GVM::RHI::BufferRange(buffer, 4, 4), 3, 1, 0, 0).drawFn(render);
            slotWriter->run(3, 1, 0, 0).drawFn(render);
        }
        else
        {
            writer->draw(GVM::RHI::BufferRange(buffer, 0, 4), 99u, 3, 1, 0, 0).drawFn(render);
            reader->draw(GVM::RHI::BufferRange(buffer, 4, 4), 3, 1, 0, 0).drawFn(render);
        }
        render->end();
        render.reset();
        encoder->end();
        device->getMainQueue()->submit({encoder});
        const auto state = GVM::RHI::Vulkan::Testing::getCurrentBufferState(device->getMainQueue(), buffer);
        EXPECT_TRUE(state.lastWriteInitialized);
        EXPECT_NE(state.accessMaskBits & state.lastWriteAccessMaskBits, 0u);
    }
}

/** Reads a preceding pass's fragment write after shared-buffer declarations that access disjoint elements. */
TEST_F(ConstructorArgumentsTest, ReadsRenderWritesAfterOverlappingArgumentSnapshots)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto buffer = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Storage |
        GVM::RHI::BufferUsage::CopyDst, .size = 24});
    auto writer = proxy->createRenderClass<ArgumentWriteRaster>(GVM::RHI::BufferRange(buffer, 16, 4), 99u);
    auto group = proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(buffer, 16, 8));
    auto slotWriter = proxy->createRenderClass<ArgumentSlotWriteRaster>(group);
    auto reader = proxy->createRenderClass<ArgumentReadRaster>(GVM::RHI::BufferRange(buffer));
    auto firstImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto secondImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment |
        GVM::RHI::TextureUsage::CopySrc, .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget firstTarget;
    firstTarget.color = firstImage->createView();
    ArgumentRasterTarget secondTarget;
    secondTarget.color = secondImage->createView();
    for (uint32_t variant = 0; variant < 4; ++variant)
    {
        SCOPED_TRACE(variant);
        const eastl::array<uint32_t, 6> initial{10u, 0u, 0u, 0u, 0u, 0u};
        queue->writeBuffer(buffer, initial.data(), sizeof(initial))->submit();
        auto encoder = device->getMainQueue()->createCommandEncoder();
        auto render = encoder->beginRenderPass(firstTarget.getRenderPassDescriptor());
        if ((variant & 1u) != 0)
        {
            reader->draw(GVM::RHI::BufferRange(buffer, 0, 4), 3, 1, 0, 0).drawFn(render);
        }
        if (variant >= 2)
        {
            slotWriter->run(3, 1, 0, 0).drawFn(render);
        }
        else
        {
            writer->draw(GVM::RHI::BufferRange(buffer, 16, 4), 99u, 3, 1, 0, 0).drawFn(render);
        }
        if ((variant & 1u) == 0)
        {
            reader->draw(GVM::RHI::BufferRange(buffer, 0, 4), 3, 1, 0, 0).drawFn(render);
        }
        render->end();
        render.reset();
        render = encoder->beginRenderPass(secondTarget.getRenderPassDescriptor());
        reader->draw(GVM::RHI::BufferRange(buffer, variant >= 2 ? 20 : 16, 4), 3, 1, 0, 0).drawFn(render);
        render->end();
        render.reset();
        encoder->end();
        device->getMainQueue()->submit({encoder});
        eastl::array<uint8_t, 4> pixel{};
        queue->readTexture(secondImage, pixel.data(), pixel.size())->submit();
        EXPECT_EQ(pixel[0], 99u);
        EXPECT_EQ(pixel[3], 255u);
    }
}

/** Preserves both graphics writer stages across partially overlapping Slot and Args image views. */
TEST_F(ConstructorArgumentsTest, UnionsOverlappingImageWritesBeforeAdvancingBatchState)
{
    if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
    {
        GTEST_SKIP() << "Checks the Vulkan batch state for mixed Slot and Args image views.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::CopySrc,
        .size = {2, 1, 1}, .format = GVM::RHI::TextureFormat::R32Uint, .arrayLayerCount = 3});
    auto slotView = image->createView({.dimension = GVM::RHI::TextureViewDimension::e2DArray,
        .baseArrayLayer = 0, .arrayLayerCount = 2});
    auto argsView = image->createView({.dimension = GVM::RHI::TextureViewDimension::e2DArray,
        .baseArrayLayer = 1, .arrayLayerCount = 2});
    auto group = proxy->createBindGroup<ArgumentOverlapTextureBindings>(slotView);
    auto slotWriter = proxy->createRenderClass<ArgumentSlotArrayWriteRaster>(group);
    auto argsWriter = proxy->createRenderClass<ArgumentArrayWriteRaster>(argsView);
    auto color = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = color->createView();
    queue->renderPass("MixedImageViewWriters", target, slotWriter->run(3, 1, 0, 0),
        argsWriter->draw(argsView, 3, 1, 0, 0))->submit();
    auto &nativeQueue = *static_cast<GVM::RHI::Vulkan::VKQueue *>(device->getMainQueue());
    auto &nativeImage = *static_cast<GVM::RHI::Vulkan::VKTexture *>(image.get());
    vk::ImageLayout layout{};
    vk::PipelineStageFlags2 stages{};
    vk::AccessFlags2 accesses{};
    ASSERT_TRUE(nativeQueue.getResourceStateDB().tryGetSharedTextureStateForTransition(nativeImage,
        GVM::RHI::TextureAspect::All, 0, 1, 1, 1, layout, stages, accesses));
    EXPECT_NE(stages & vk::PipelineStageFlagBits2::eVertexShader, vk::PipelineStageFlags2{});
    EXPECT_NE(stages & vk::PipelineStageFlagBits2::eFragmentShader, vk::PipelineStageFlags2{});
    EXPECT_EQ(layout, vk::ImageLayout::eGeneral);
    eastl::array<uint32_t, 2> pixels{};
    queue->readTexture(image, pixels.data(), sizeof(pixels), 0, 1)->submit();
    EXPECT_EQ(pixels[0], 31u);
    EXPECT_EQ(pixels[1], 47u);
}

/** Checks independent texture/sampler replacement and native lifetime after the source handles are retired. */
TEST_F(ConstructorArgumentsTest, CapturesTextureViewsAndSamplersBeforeDeferredRecording)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto firstImage = createArgumentTexture(*device);
    auto secondImage = createArgumentTexture(*device);
    auto nearest = createArgumentSampler(*device);
    auto linear = createArgumentSampler(*device, GVM::RHI::FilterMode::Linear);
    const eastl::array<uint8_t, 16> firstPixels{0, 0, 0, 255, 200, 0, 0, 255, 0, 0, 0, 255, 200, 0, 0, 255};
    const eastl::array<uint8_t, 16> secondPixels{40, 0, 0, 255, 120, 0, 0, 255, 40, 0, 0, 255, 120, 0, 0, 255};
    queue->writeTexture(firstImage, firstPixels.data(), firstPixels.size())->submit();
    queue->writeTexture(secondImage, secondPixels.data(), secondPixels.size())->submit();
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentTextureCompute>(firstImage->createView(), nearest, GVM::RHI::BufferRange(output), 0u);
    auto first = pipeline->dispatchThreads(firstImage->createView(), linear, GVM::RHI::BufferRange(output, 0, 4), 1u, 1, 1, 1);
    auto second = pipeline->dispatchThreads(secondImage->createView(), nearest, GVM::RHI::BufferRange(output, 16, 4), 2u, 1, 1, 1);
    auto copy = first;
    device->freeTexture(firstImage);
    device->freeSampler(linear);
    pipeline.reset();
    queue->computePass("ArgsTextureReplacement", eastl::move(second), eastl::move(first), eastl::move(copy))->submit();
    eastl::array<uint32_t, 5> result{};
    queue->readBuffer(output, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 101u);
    EXPECT_EQ(result[4], 122u);
}

/** Verifies fixed resource arrays preserve dynamic selection through copies, assignment, and helper calls. */
TEST_F(ConstructorArgumentsTest, SelectsResourceArraysWithInvocationVaryingIndices)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto firstImage = createArgumentTexture(*device);
    auto secondImage = createArgumentTexture(*device);
    const eastl::array<uint8_t, 16> firstPixels{31, 0, 0, 255, 31, 0, 0, 255, 31, 0, 0, 255, 31, 0, 0, 255};
    const eastl::array<uint8_t, 16> secondPixels{207, 0, 0, 255, 207, 0, 0, 255, 207, 0, 0, 255, 207, 0, 0, 255};
    queue->writeTexture(firstImage, firstPixels.data(), firstPixels.size())->submit();
    queue->writeTexture(secondImage, secondPixels.data(), secondPixels.size())->submit();
    auto firstOutput = createArgumentOutput(proxy);
    auto secondOutput = createArgumentOutput(proxy);
    auto sampler = createArgumentSampler(*device);
    ArgumentResourceArrays resources{{firstImage->createView(), secondImage->createView()}, {sampler, sampler},
        {GVM::RHI::BufferRange(firstOutput), GVM::RHI::BufferRange(secondOutput)}, 1u};
    auto pipeline = proxy->createComputeClass<ArgumentResourceArrayCompute>(resources);
    auto task = pipeline->dispatchThreads(resources, 2, 1, 1);
    resources.images = {};
    resources.outputs = {};
    queue->computePass("ArgsResourceArrays", eastl::move(task))->submit();
    eastl::array<uint32_t, 2> result{};
    queue->readBuffer(firstOutput, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 31u);
    queue->readBuffer(secondOutput, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[1], 207u);
}

/** Keeps native images, samplers, and buffers from top-level arrays alive after explicit host retirement. */
TEST_F(ConstructorArgumentsTest, RetainsTopLevelResourceArraysAfterExplicitDestruction)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    eastl::array<GVM::RHI::Texture, 2> images{createArgumentTexture(*device), createArgumentTexture(*device)};
    eastl::array<GVM::RHI::Sampler, 2> samplers{createArgumentSampler(*device), createArgumentSampler(*device)};
    eastl::array<GVM::RHI::Buffer, 2> inputs{createArgumentOutput(proxy), createArgumentOutput(proxy)};
    const eastl::array<uint8_t, 2> colors{31, 207};
    const eastl::array<uint32_t, 2> values{11, 23};
    for (size_t index = 0; index < images.size(); ++index)
    {
        eastl::array<uint8_t, 16> pixels{};
        for (size_t offset = 0; offset < pixels.size(); offset += 4)
        {
            pixels[offset] = colors[index];
            pixels[offset + 3] = 255;
        }
        queue->writeTexture(images[index], pixels.data(), pixels.size())->submit();
        queue->writeBuffer(inputs[index], &values[index], sizeof(values[index]))->submit();
    }
    std::array<GVM::RHI::TextureView, 2> views{images[0]->createView(), images[1]->createView()};
    const std::array<GVM::RHI::Sampler, 2> sampling{samplers[0], samplers[1]};
    const std::array<GVM::RHI::BufferRange, 2> ranges{GVM::RHI::BufferRange(inputs[0]), GVM::RHI::BufferRange(inputs[1])};
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentTopLevelArrayCompute>(views, sampling, ranges, GVM::RHI::BufferRange(output));
    auto task = pipeline->dispatchThreads(views, sampling, ranges, GVM::RHI::BufferRange(output), 2, 1, 1);
    pipeline.reset();
    for (size_t index = 0; index < images.size(); ++index)
    {
        views[index]->destroy();
        device->freeTexture(images[index]);
        device->freeSampler(samplers[index]);
        device->freeBuffer(inputs[index]);
    }
    queue->computePass("ArgsRetiredTopLevelArrays", eastl::move(task))->submit();
    eastl::array<uint32_t, 2> observed{};
    queue->readBuffer(output, observed.data(), sizeof(observed))->submit();
    EXPECT_EQ(observed[0], 42u);
    EXPECT_EQ(observed[1], 230u);
}

/** Orders storage writes from top-level image arrays before sampling the selected view in the same pass. */
TEST_F(ConstructorArgumentsTest, SynchronizesTopLevelStorageArraysBeforeSampling)
{
    GVM::Core::DeviceProxy proxy(device);
    auto first = createArgumentTexture(*device, 2);
    auto second = createArgumentTexture(*device, 2);
    const std::array<GVM::RHI::TextureView, 2> views{
        first->createView({.baseMipLevel = 1, .mipLevelCount = 1}),
        second->createView({.baseMipLevel = 1, .mipLevelCount = 1})};
    auto sampler = createArgumentSampler(*device);
    auto output = createArgumentOutput(proxy);
    auto writer = proxy->createComputeClass<ArgumentStorageArrayCompute>(views);
    auto reader = proxy->createComputeClass<ArgumentTextureCompute>(views[0], sampler, GVM::RHI::BufferRange(output), 0u);
    auto queue = proxy->graphicsQueue(0);
    queue->computePass("ArgsTopLevelArrayRoleTransition", writer->dispatchThreads(views, 2, 1, 1),
        reader->dispatchThreads(views[0], sampler, GVM::RHI::BufferRange(output), 0u, 1, 1, 1))->submit();
    uint32_t observed = 0;
    queue->readBuffer(output, &observed, sizeof(observed))->submit();
    EXPECT_EQ(observed, 64u);
}

/** Resolves sampled/storage roles per dispatch and according to final submission order rather than capture order. */
TEST_F(ConstructorArgumentsTest, SamplesStorageTextureWritesInFinalSubmissionOrder)
{
    GVM::Core::DeviceProxy proxy(device);
    auto image = createArgumentTexture(*device);
    auto view = image->createView();
    auto sampler = createArgumentSampler(*device);
    auto output = createArgumentOutput(proxy);
    auto writer = proxy->createComputeClass<ArgumentStorageTextureCompute>(view, float4(0));
    auto reader = proxy->createComputeClass<ArgumentTextureCompute>(view, sampler, GVM::RHI::BufferRange(output), 0u);
    auto consume = recordArgumentDispatch(*device, reader->dispatchThreads(view, sampler, GVM::RHI::BufferRange(output), 3u, 1, 1, 1));
    auto produce = recordArgumentDispatch(*device, writer->dispatchThreads(view, float4(0.8f, 0, 0, 1), 2, 2, 1));
    device->getMainQueue()->submit({produce, consume});
    eastl::array<uint32_t, 1> result{};
    proxy->graphicsQueue(0)->readBuffer(output, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 207u);
    auto samePass = device->getMainQueue()->createCommandEncoder();
    auto compute = samePass->beginComputePass({});
    writer->dispatchThreads(view, float4(0.4f, 0, 0, 1), 2, 2, 1).dispatchFn(compute);
    reader->dispatchThreads(view, sampler, GVM::RHI::BufferRange(output), 9u, 1, 1, 1).dispatchFn(compute);
    compute->end();
    compute.reset();
    samePass->end();
    device->getMainQueue()->submit({samePass});
    proxy->graphicsQueue(0)->readBuffer(output, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 111u);
}

/** Keeps view mip ranges precise while making compute writes visible to a subsequent fragment texture read. */
TEST_F(ConstructorArgumentsTest, DrawSamplesTheCapturedMipViewAfterComputeWrites)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto image = createArgumentTexture(*device, 2);
    const eastl::array<uint8_t, 16> base{17, 0, 0, 255, 17, 0, 0, 255, 17, 0, 0, 255, 17, 0, 0, 255};
    queue->writeTexture(image, base.data(), base.size())->submit();
    auto mip = image->createView({.format = GVM::RHI::TextureFormat::RGBA8Unorm, .dimension = GVM::RHI::TextureViewDimension::e2D,
        .baseMipLevel = 1, .mipLevelCount = 1});
    auto sampler = createArgumentSampler(*device);
    auto writer = proxy->createComputeClass<ArgumentStorageTextureCompute>(mip, float4(0));
    auto raster = proxy->createRenderClass<ArgumentTextureRaster>(mip, sampler);
    auto targetImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {4, 4, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = targetImage->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto compute = encoder->beginComputePass({});
    writer->dispatchThreads(mip, float4(0.6f, 0, 0, 1), 1, 1, 1).dispatchFn(compute);
    compute->end();
    compute.reset();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    raster->draw(mip, sampler, 3, 1, 0, 0).drawFn(render);
    render->end();
    render.reset();
    encoder->end();
    device->getMainQueue()->submit({encoder});
    eastl::array<uint8_t, 64> result{};
    queue->readTexture(targetImage, result.data(), result.size())->submit();
    for (size_t index = 0; index < result.size(); index += 4)
    {
        EXPECT_EQ(result[index], 153u);
        EXPECT_EQ(result[index + 3], 255u);
    }
    eastl::array<uint8_t, 16> unchanged{};
    queue->readTexture(image, unchanged.data(), unchanged.size())->submit();
    EXPECT_EQ(unchanged, base);
}

/** Verifies unsigned sampled textures use their own typed table and preserve top-level array invocation signatures. */
TEST_F(ConstructorArgumentsTest, ReadsUnsignedTexturesThroughRootUniformArrayIndices)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto first = device->createTexture({.usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Uint});
    auto second = device->createTexture({.usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Uint});
    const eastl::array<uint8_t, 4> firstPixel{37, 0, 0, 255};
    const eastl::array<uint8_t, 4> secondPixel{219, 0, 0, 255};
    queue->writeTexture(first, firstPixel.data(), firstPixel.size())->submit();
    queue->writeTexture(second, secondPixel.data(), secondPixel.size())->submit();
    std::array<GVM::RHI::TextureView, 2> views{first->createView(), second->createView()};
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentIntegerTextureCompute>(views, GVM::RHI::BufferRange(output), 0u);
    queue->computePass("ArgsIntegerTextures", pipeline->dispatchThreads(views, GVM::RHI::BufferRange(output), 1u, 1, 1, 1))->submit();
    uint32_t result = 0;
    queue->readBuffer(output, &result, sizeof(result))->submit();
    EXPECT_EQ(result, 219u);
}

/** Confirms non-uniform storage-image array indices select independent writable allocations. */
TEST_F(ConstructorArgumentsTest, WritesInvocationSelectedStorageImageArrayElements)
{
    GVM::Core::DeviceProxy proxy(device);
    auto first = createArgumentTexture(*device);
    auto second = createArgumentTexture(*device);
    std::array<GVM::RHI::TextureView, 2> views{first->createView(), second->createView()};
    auto pipeline = proxy->createComputeClass<ArgumentStorageArrayCompute>(views);
    proxy->graphicsQueue(0)->computePass("ArgsStorageImageArrays", pipeline->dispatchThreads(views, 2, 1, 1))->submit();
    eastl::array<uint8_t, 16> result{};
    proxy->graphicsQueue(0)->readTexture(first, result.data(), result.size())->submit();
    EXPECT_EQ(result[0], 64u);
    proxy->graphicsQueue(0)->readTexture(second, result.data(), result.size())->submit();
    EXPECT_EQ(result[0], 128u);
}

/** Verifies 3D, 2D-array, and storage-format table separation with concrete GPU observations. */
TEST_F(ConstructorArgumentsTest, PreservesImageViewDimensionsAndStorageFormats)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto volume = device->createTexture({.usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
        .dimension = GVM::RHI::TextureDimension::e3D, .size = {1, 1, 2}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto layers = device->createTexture({.usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm, .arrayLayerCount = 2});
    auto outputVolume = device->createTexture({.usage = GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::CopySrc,
        .dimension = GVM::RHI::TextureDimension::e3D, .size = {1, 1, 2}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto outputLayers = device->createTexture({.usage = GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::CopySrc,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::R16Float, .arrayLayerCount = 2});
    const eastl::array<uint8_t, 8> volumePixels{51, 0, 0, 255, 153, 0, 0, 255};
    const eastl::array<uint8_t, 8> layerPixels{102, 0, 0, 255, 204, 0, 0, 255};
    queue->writeTexture(volume, volumePixels.data(), volumePixels.size())->submit();
    queue->writeTexture(layers, layerPixels.data(), 4, 0, 0)->submit();
    queue->writeTexture(layers, layerPixels.data() + 4, 4, 0, 1)->submit();
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentImageShapesCompute>(volume->createView(), layers->createView(),
        outputVolume->createView(), outputLayers->createView(), GVM::RHI::BufferRange(output));
    queue->computePass("ArgsImageShapes", pipeline->dispatchThreads(volume->createView(), layers->createView(), outputVolume->createView(),
        outputLayers->createView(), GVM::RHI::BufferRange(output), 1, 1, 1))->submit();
    eastl::array<uint32_t, 3> result{};
    queue->readBuffer(output, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 357u);
    EXPECT_EQ(result[1], 2u);
    EXPECT_EQ(result[2], 2u);
    eastl::array<uint8_t, 8> volumeResult{};
    queue->readTexture(outputVolume, volumeResult.data(), volumeResult.size())->submit();
    EXPECT_EQ(volumeResult[4], 153u);
    uint16_t layerResult = 0;
    queue->readTexture(outputLayers, &layerResult, sizeof(layerResult), 0, 1)->submit();
    EXPECT_EQ(layerResult, 0x3800u);
}

/** Ensures switching between Args resource tables and an ordinary Slot group restores the correct native set. */
TEST_F(ConstructorArgumentsTest, SwitchesBetweenArgumentTextureTablesAndSlotGroups)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto first = createArgumentTexture(*device);
    auto second = createArgumentTexture(*device);
    const eastl::array<uint8_t, 16> firstPixels{43, 0, 0, 255, 43, 0, 0, 255, 43, 0, 0, 255, 43, 0, 0, 255};
    const eastl::array<uint8_t, 16> secondPixels{187, 0, 0, 255, 187, 0, 0, 255, 187, 0, 0, 255, 187, 0, 0, 255};
    queue->writeTexture(first, firstPixels.data(), firstPixels.size())->submit();
    queue->writeTexture(second, secondPixels.data(), secondPixels.size())->submit();
    auto sampler = createArgumentSampler(*device);
    auto args = proxy->createRenderClass<ArgumentTextureRaster>(first->createView(), sampler);
    auto group = proxy->createBindGroup<ArgumentTextureBindings>(second->createView(), sampler);
    auto slot = proxy->createRenderClass<ArgumentSlotTextureRaster>(group);
    auto targetImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
        .size = {4, 4, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = targetImage->createView();
    queue->renderPass("ArgsToSlotToArgs", target, args->draw(first->createView(), sampler, 3, 1, 0, 0),
        slot->run(3u, 1u, 0u, 0u), args->draw(first->createView(), sampler, 3, 1, 0, 0))->submit();
    eastl::array<uint8_t, 64> result{};
    queue->readTexture(targetImage, result.data(), result.size())->submit();
    EXPECT_EQ(result[0], 43u);
    queue->renderPass("SlotToArgsToSlot", target, slot->run(3u, 1u, 0u, 0u),
        args->draw(first->createView(), sampler, 3, 1, 0, 0), slot->run(3u, 1u, 0u, 0u))->submit();
    queue->readTexture(targetImage, result.data(), result.size())->submit();
    EXPECT_EQ(result[0], 187u);
}

/** Keeps a captured native view alive when its public handle is explicitly destroyed before execution. */
TEST_F(ConstructorArgumentsTest, RetainsDestroyedTextureViewsUntilCapturedTasksComplete)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto image = createArgumentTexture(*device);
    auto view = image->createView();
    auto sampler = createArgumentSampler(*device);
    auto output = createArgumentOutput(proxy);
    const eastl::array<uint8_t, 16> pixels{91, 0, 0, 255, 91, 0, 0, 255, 91, 0, 0, 255, 91, 0, 0, 255};
    queue->writeTexture(image, pixels.data(), pixels.size())->submit();
    auto pipeline = proxy->createComputeClass<ArgumentTextureCompute>(view, sampler, GVM::RHI::BufferRange(output), 0u);
    auto task = pipeline->dispatchThreads(view, sampler, GVM::RHI::BufferRange(output), 5u, 1, 1, 1);
    view->destroy();
    EXPECT_THROW(pipeline->dispatchThreads(view, sampler, GVM::RHI::BufferRange(output), 0u, 1, 1, 1), std::invalid_argument);
    queue->computePass("ArgsDestroyedViewLifetime", eastl::move(task))->submit();
    uint32_t result = 0;
    queue->readBuffer(output, &result, sizeof(result))->submit();
    EXPECT_EQ(result, 96u);
}

/** Confirms cheap snapshot checks reject missing handles and incompatible view formats and dimensions. */
TEST_F(ConstructorArgumentsTest, RejectsIncompatibleTextureAndSamplerArguments)
{
    GVM::Core::DeviceProxy proxy(device);
    auto image = createArgumentTexture(*device);
    auto sampler = createArgumentSampler(*device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentTextureCompute>(image->createView(), sampler, GVM::RHI::BufferRange(output), 0u);
    EXPECT_THROW(pipeline->dispatchThreads(GVM::RHI::TextureView{}, sampler, GVM::RHI::BufferRange(output), 0u, 1, 1, 1), std::invalid_argument);
    EXPECT_THROW(pipeline->dispatchThreads(image->createView(), GVM::RHI::Sampler{}, GVM::RHI::BufferRange(output), 0u, 1, 1, 1), std::invalid_argument);
    auto integerImage = device->createTexture({.usage = GVM::RHI::TextureUsage::TextureBinding,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Uint});
    EXPECT_THROW(pipeline->dispatchThreads(integerImage->createView(), sampler, GVM::RHI::BufferRange(output), 0u, 1, 1, 1), std::invalid_argument);
    auto volume = device->createTexture({.usage = GVM::RHI::TextureUsage::TextureBinding,
        .dimension = GVM::RHI::TextureDimension::e3D, .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    EXPECT_THROW(pipeline->dispatchThreads(volume->createView(), sampler, GVM::RHI::BufferRange(output), 0u, 1, 1, 1), std::invalid_argument);
}

/** Verifies bounded table exhaustion and slot reuse after abandoning a recorded, unsubmitted snapshot. */
TEST_F(ConstructorArgumentsTest, RecyclesDescriptorSlotsOnlyAfterRecordedReferencesEnd)
{
    if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
    {
        GTEST_SKIP() << "Persistent descriptor slots are specific to the Vulkan backend.";
    }
    GVM::RHI::Vulkan::Testing::setArgumentTableCapacityForTesting(device, 8, 8, 8);
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto image = createArgumentTexture(*device);
    auto firstView = image->createView();
    auto sampler = createArgumentSampler(*device);
    auto pipeline = proxy->createComputeClass<ArgumentTextureCompute>(firstView, sampler, GVM::RHI::BufferRange(output), 0u);
    auto firstTask = pipeline->dispatchThreads(firstView, sampler, GVM::RHI::BufferRange(output), 0u, 1, 1, 1);
    auto encoder = recordArgumentDispatch(*device, firstTask);
    eastl::vector<GVM::RHI::Texture> images{image};
    GVM::RHI::TextureView overflowView;
    for (uint32_t index = 0; index < 16; ++index)
    {
        auto next = createArgumentTexture(*device);
        images.push_back(next);
        auto view = next->createView();
        try
        {
            static_cast<void>(pipeline->dispatchThreads(view, sampler, GVM::RHI::BufferRange(output), 0u, 1, 1, 1));
        }
        catch (const std::runtime_error &error)
        {
            ASSERT_NE(eastl::string(error.what()).find("table is full"), eastl::string::npos);
            overflowView = view;
            break;
        }
    }
    ASSERT_FALSE(overflowView.isNull());
    firstView->destroy();
    firstTask = {};
    EXPECT_THROW(pipeline->dispatchThreads(overflowView, sampler, GVM::RHI::BufferRange(output), 0u, 1, 1, 1), std::runtime_error);
    encoder.reset();
    EXPECT_NO_THROW(pipeline->dispatchThreads(overflowView, sampler, GVM::RHI::BufferRange(output), 0u, 1, 1, 1));
}

/** Samples the same texture view through two distinct Args pipeline layouts sharing one device table. */
TEST_F(ConstructorArgumentsTest, ReadsOneViewThroughDistinctArgsPipelineLayouts)
{
    if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
    {
        GTEST_SKIP() << "The shared Args descriptor table is specific to the Vulkan backend.";
    }
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto first = createArgumentOutput(proxy);
    auto second = createArgumentOutput(proxy);
    auto image = createArgumentTexture(*device);
    eastl::array<uint8_t, 16> pixels{};
    for (size_t index = 0; index < pixels.size(); index += 4u)
    {
        pixels[index] = 137u;
        pixels[index + 3u] = 255u;
    }
    queue->writeTexture(image, pixels.data(), pixels.size())->submit();
    auto view = image->createView();
    auto sampler = createArgumentSampler(*device);
    auto reader = proxy->createComputeClass<ArgumentTextureCompute>(view, sampler, GVM::RHI::BufferRange(first), 0u);
    ArgumentResourceArrays arrays{};
    arrays.images = {view, view};
    arrays.samplers = {sampler, sampler};
    arrays.outputs = {GVM::RHI::BufferRange(second), GVM::RHI::BufferRange(second, 4, 4)};
    arrays.selection = 0u;
    auto arrayReader = proxy->createComputeClass<ArgumentResourceArrayCompute>(arrays);
    const eastl::array<GVM::Core::ComputePassTaskDescriptor, 2> tasks{
        reader->dispatchThreads(view, sampler, GVM::RHI::BufferRange(first), 0u, 1, 1, 1),
        arrayReader->dispatchThreads(arrays, 1, 1, 1)};
    queue->computePass("SharedArgsTable", tasks)->submit();
    uint32_t observed = 0;
    queue->readBuffer(GVM::RHI::BufferRange(first), &observed, sizeof(observed))->submit();
    EXPECT_EQ(observed, 137u);
    observed = 0;
    queue->readBuffer(GVM::RHI::BufferRange(second), &observed, sizeof(observed))->submit();
    EXPECT_EQ(observed, 137u);
}

/** Records 300 distinct Args texture views in one pass, exceeding the old per-layout 256-slot table. */
TEST_F(ConstructorArgumentsTest, DispatchesHundredsOfDistinctArgumentViewsInOnePass)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto sampler = createArgumentSampler(*device);
    auto output = proxy->createBuffer({.label = "ArgsManyViewsOutput", .usage = GVM::RHI::BufferUsage::Storage |
        GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst, .size = 300u * 4u});
    auto firstImage = device->createTexture({.label = "ArgsManyViews0", .usage = GVM::RHI::TextureUsage::TextureBinding |
        GVM::RHI::TextureUsage::CopyDst, .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto firstView = firstImage->createView();
    auto pipeline = proxy->createComputeClass<ArgumentTextureCompute>(firstView, sampler, GVM::RHI::BufferRange(output), 0u);
    eastl::vector<GVM::RHI::Texture> images{firstImage};
    eastl::vector<GVM::RHI::TextureView> views{firstView};
    eastl::vector<GVM::Core::ComputePassTaskDescriptor> tasks;
    tasks.reserve(300u);
    for (uint32_t index = 0; index < 300u; ++index)
    {
        auto image = index == 0 ? firstImage :
            device->createTexture({.label = "ArgsManyViews", .usage = GVM::RHI::TextureUsage::TextureBinding |
                GVM::RHI::TextureUsage::CopyDst, .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
        if (index != 0)
        {
            images.push_back(image);
            views.push_back(image->createView());
        }
        const eastl::array<uint8_t, 4> pixel{static_cast<uint8_t>(index % 256u), 0u, 0u, 255u};
        queue->writeTexture(image, pixel.data(), pixel.size());
        tasks.push_back(pipeline->dispatchThreads(views[index], sampler, GVM::RHI::BufferRange(output, index * 4u, 4u), index, 1, 1, 1));
    }
    queue->submit();
    queue->computePass("ArgsManyViews", tasks)->submit();
    eastl::vector<uint32_t> observed(300u, 0u);
    queue->readBuffer(GVM::RHI::BufferRange(output), observed.data(), observed.size() * sizeof(uint32_t))->submit();
    for (uint32_t index = 0; index < 300u; ++index)
    {
        EXPECT_EQ(observed[index], index % 256u + index) << index;
    }
}

/** Rejects image hazards that cannot be represented by the render-pass dependency model. */
TEST_F(ConstructorArgumentsTest, RequiresPassBoundariesForOverlappingRenderImageWrites)
{
    GVM::Core::DeviceProxy proxy(device);
    auto image = createArgumentTexture(*device);
    auto sampler = createArgumentSampler(*device);
    auto writer = proxy->createRenderClass<ArgumentStorageTextureRaster>(image->createView());
    auto reader = proxy->createRenderClass<ArgumentTextureRaster>(image->createView(), sampler);
    auto targetImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = targetImage->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    writer->draw(image->createView(), 3, 1, 0, 0).drawFn(render);
    EXPECT_THROW(reader->draw(image->createView(), sampler, 3, 1, 0, 0).drawFn(render), std::logic_error);
}

/** Accepts repeated draws that only read one fragment-visible Args storage image in a single render pass. */
TEST_F(ConstructorArgumentsTest, AllowsRepeatedReadOnlyArgumentStorageTextureAccess)
{
    GVM::Core::DeviceProxy proxy(device);
    auto image = createArgumentTexture(*device);
    auto reader = proxy->createRenderClass<ArgumentReadStorageTextureRaster>(image->createView());
    auto targetImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = targetImage->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    reader->draw(image->createView(), 3, 1, 0, 0).drawFn(render);
    reader->draw(image->createView(), 3, 1, 0, 0).drawFn(render);
    render->end();
    render.reset();
    encoder->end();
    device->getMainQueue()->submit({encoder});
}

/** Rejects a second draw that writes one fragment-visible Args storage image already written in the same render pass. */
TEST_F(ConstructorArgumentsTest, RejectsRepeatedReadWriteArgumentStorageTextureAccess)
{
    GVM::Core::DeviceProxy proxy(device);
    auto image = createArgumentTexture(*device);
    auto writer = proxy->createRenderClass<ArgumentStorageTextureRaster>(image->createView());
    auto targetImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = targetImage->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    writer->draw(image->createView(), 3, 1, 0, 0).drawFn(render);
    EXPECT_THROW(writer->draw(image->createView(), 3, 1, 0, 0).drawFn(render), std::logic_error);
    render->end();
    render.reset();
    encoder->end();
}

/** Rejects sampling an active attachment through Args before native commands are submitted. */
TEST_F(ConstructorArgumentsTest, RejectsArgumentTextureAliasingWithAnActiveAttachment)
{
    GVM::Core::DeviceProxy proxy(device);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::TextureBinding,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto sampler = createArgumentSampler(*device);
    auto raster = proxy->createRenderClass<ArgumentTextureRaster>(image->createView(), sampler);
    ArgumentRasterTarget target;
    target.color = image->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    EXPECT_THROW(raster->draw(image->createView(), sampler, 3, 1, 0, 0).drawFn(render), std::logic_error);
    render->end();
    render.reset();
    encoder->end();
}

/** Rejects sampling an active attachment through a legacy Slot bind group before native commands are submitted. */
TEST_F(ConstructorArgumentsTest, RejectsSlotTextureAliasingWithAnActiveAttachment)
{
    GVM::Core::DeviceProxy proxy(device);
    auto image = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::TextureBinding,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    auto sampler = createArgumentSampler(*device);
    auto group = proxy->createBindGroup<ArgumentTextureBindings>(image->createView(), sampler);
    auto raster = proxy->createRenderClass<ArgumentSlotTextureRaster>(group);
    ArgumentRasterTarget target;
    target.color = image->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    EXPECT_THROW(raster->run(3, 1, 0, 0).drawFn(render), std::logic_error);
    render->end();
    render.reset();
    encoder->end();
}

/** Rejects using one texture as both a Slot-sampled image and an Args storage image in one render pass. */
TEST_F(ConstructorArgumentsTest, RejectsSampledAndStorageUseOfOneTextureInARenderPass)
{
    GVM::Core::DeviceProxy proxy(device);
    auto image = createArgumentTexture(*device);
    auto sampler = createArgumentSampler(*device);
    auto group = proxy->createBindGroup<ArgumentTextureBindings>(image->createView(), sampler);
    auto slot = proxy->createRenderClass<ArgumentSlotTextureRaster>(group);
    auto writer = proxy->createRenderClass<ArgumentStorageTextureRaster>(image->createView());
    auto targetImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = targetImage->createView();
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    slot->run(3, 1, 0, 0).drawFn(render);
    EXPECT_THROW(writer->draw(image->createView(), 3, 1, 0, 0).drawFn(render), std::logic_error);
    render->end();
    render.reset();
    encoder->end();
}

/** Applies the same Args buffer, sampler, and texture-view validation on both backends. */
TEST_F(ConstructorArgumentsTest, RejectsInvalidArgumentResourcesIdentically)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto data = makeArgumentData(0);
    auto pipeline = proxy->createComputeClass<ArgumentCompute>(GVM::RHI::BufferRange(output), data);
    EXPECT_THROW(pipeline->dispatchThreads(GVM::RHI::BufferRange(output, 2, 4), data, 1, 1, 1), std::invalid_argument);
    auto image = createArgumentTexture(*device);
    auto sampler = createArgumentSampler(*device);
    auto texturePipeline = proxy->createComputeClass<ArgumentTextureCompute>(image->createView(), sampler,
        GVM::RHI::BufferRange(output), 0u);
    auto freedSampler = createArgumentSampler(*device);
    device->freeSampler(freedSampler);
    EXPECT_THROW(texturePipeline->dispatchThreads(image->createView(), freedSampler, GVM::RHI::BufferRange(output), 0u,
        1, 1, 1), std::invalid_argument);
    auto freedImage = createArgumentTexture(*device);
    auto freedView = freedImage->createView();
    device->freeTexture(freedImage);
    EXPECT_THROW(texturePipeline->dispatchThreads(freedView, sampler, GVM::RHI::BufferRange(output), 0u,
        1, 1, 1), std::invalid_argument);
}

/** Requires an Args snapshot for indirect draws and dispatches, not only direct calls. */
TEST_F(ConstructorArgumentsTest, RequiresArgumentsBeforeIndirectDrawsAndDispatches)
{
    GVM::Core::DeviceProxy proxy(device);
    auto output = createArgumentOutput(proxy);
    auto commands = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::Index,
        .size = 64});
    auto targetImage = device->createTexture({.usage = GVM::RHI::TextureUsage::RenderAttachment,
        .size = {1, 1, 1}, .format = GVM::RHI::TextureFormat::RGBA8Unorm});
    ArgumentRasterTarget target;
    target.color = targetImage->createView();
    auto raster = proxy->createRenderClass<ArgumentIndexedRaster>(float4(0), GVM::RHI::BufferRange(output), 0u);
    auto compute = proxy->createComputeClass<ArgumentWorkgroupCompute>(GVM::RHI::BufferRange(output), 0u);
    auto encoder = device->getMainQueue()->createCommandEncoder();
    auto render = encoder->beginRenderPass(target.getRenderPassDescriptor());
    render->setPipeline(raster->getPipeline());
    EXPECT_THROW(render->drawIndirect(GVM::RHI::BufferRange(commands, 0, 16), 1, 16), std::logic_error);
    render->setIndexBuffer(GVM::RHI::BufferRange(commands, 32, 12), GVM::RHI::IndexFormat::Uint32);
    EXPECT_THROW(render->drawIndexedIndirect(GVM::RHI::BufferRange(commands, 0, 16), 1, 16), std::logic_error);
    render->end();
    render.reset();
    auto computePass = encoder->beginComputePass({});
    computePass->setPipeline(compute->getPipeline());
    EXPECT_THROW(computePass->dispatchWorkgroupsIndirect(GVM::RHI::BufferRange(commands, 0, 12)), std::logic_error);
    computePass->end();
}

/** Applies shared bind-group buffer range validation on both backends. */
TEST_F(ConstructorArgumentsTest, RejectsInvalidBindGroupBufferRanges)
{
    GVM::Core::DeviceProxy proxy(device);
    auto buffer = proxy->createBuffer({.usage = GVM::RHI::BufferUsage::Storage, .size = 16});
    EXPECT_THROW(proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(buffer, 2, 4)), std::invalid_argument);
    EXPECT_THROW(proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(buffer, 20, 4)), std::out_of_range);
    EXPECT_THROW(proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(buffer, 16, 4)), std::out_of_range);
    EXPECT_NO_THROW(proxy->createBindGroup<ArgumentBindings>(GVM::RHI::BufferRange(buffer, 4, 12)));
}

/** Allocates another cached table slot after submission while preserving the earlier snapshot's selected resource. */
TEST_F(ConstructorArgumentsTest, AddsResourceSlotsWithoutReplacingSubmittedSnapshotDescriptors)
{
    GVM::Core::DeviceProxy proxy(device);
    auto queue = proxy->graphicsQueue(0);
    auto first = createArgumentTexture(*device);
    auto second = createArgumentTexture(*device);
    const eastl::array<uint8_t, 16> firstPixels{23, 0, 0, 255, 23, 0, 0, 255, 23, 0, 0, 255, 23, 0, 0, 255};
    const eastl::array<uint8_t, 16> secondPixels{167, 0, 0, 255, 167, 0, 0, 255, 167, 0, 0, 255, 167, 0, 0, 255};
    queue->writeTexture(first, firstPixels.data(), firstPixels.size())->submit();
    queue->writeTexture(second, secondPixels.data(), secondPixels.size())->submit();
    auto sampler = createArgumentSampler(*device);
    auto output = createArgumentOutput(proxy);
    auto pipeline = proxy->createComputeClass<ArgumentTextureCompute>(first->createView(), sampler, GVM::RHI::BufferRange(output), 0u);
    auto submitted = recordArgumentDispatch(*device, pipeline->dispatchThreads(first->createView(), sampler, GVM::RHI::BufferRange(output, 0, 4), 0u, 1, 1, 1));
    device->getMainQueue()->submit({submitted});
    device->freeTexture(first);
    auto following = recordArgumentDispatch(*device, pipeline->dispatchThreads(second->createView(), sampler, GVM::RHI::BufferRange(output, 4, 4), 0u, 1, 1, 1));
    device->getMainQueue()->submit({following});
    eastl::array<uint32_t, 2> result{};
    queue->readBuffer(output, result.data(), sizeof(result))->submit();
    EXPECT_EQ(result[0], 23u);
    EXPECT_EQ(result[1], 167u);
}
