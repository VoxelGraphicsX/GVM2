#include <gtest/gtest.h>
#include <GVMTestCommon.hpp>
#include <GVMRHI/GVMRHI.hpp>
#include <MBuffer.hpp>
#include <MDevice.hpp>
#include <MQueue.hpp>

#include <array>
#include <cstring>

namespace
{
    using namespace GVM::RHI;
    constexpr uint32_t TargetWidth = 32u;
    constexpr uint32_t TargetHeight = 16u;
    using PixelColor = std::array<uint8_t, 4>;
    constexpr PixelColor Red{255u, 0u, 0u, 255u};
    constexpr PixelColor Green{0u, 255u, 0u, 255u};
    constexpr PixelColor Black{0u, 0u, 0u, 255u};

    constexpr const char *RangeShader = R"(
#include <metal_stdlib>
using namespace metal;
struct RangeVertex
{
    float4 position [[position]];
    uint identity [[flat]];
};
vertex RangeVertex rangeVertex(uint vertexId [[vertex_id]], uint instanceId [[instance_id]])
{
    const float2 corners[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    RangeVertex output;
    output.position = float4(corners[vertexId % 3u], 0.0, 1.0);
    output.identity = (instanceId ^ (vertexId / 3u)) & 1u;
    return output;
}
fragment float4 rangeFragment(RangeVertex input [[stage_in]])
{
    return input.identity == 0u ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
}
)";

    /** Describes closely related two-draw layouts without coupling the test to ICB internals. */
    struct IndirectRangeSettings
    {
        bool indexed = true;
        bool sharedBuffer = true;
        bool differentIndexBuffer = false;
        bool insertZeroDraw = false;
        uint32_t firstStride = sizeof(IndirectIndexedRenderCommand);
        uint32_t secondStride = sizeof(IndirectIndexedRenderCommand);
        uint32_t firstCount = 2u;
        uint32_t secondCount = 2u;
        uint32_t secondOffset = 64u;
        uint32_t secondValidRecord = 0u;
        uint32_t firstInstance = 0u;
        uint32_t secondInstance = 1u;
        uint32_t secondIndexOffset = 0u;
        IndexFormat secondIndexFormat = IndexFormat::Uint32;
        PrimitiveTopology secondTopology = PrimitiveTopology::TriangleList;
    };

    /** Exercises command conversion through tiny real Metal targets and exact pixel assertions. */
    class MetalIndirectRangeTest : public ::testing::Test
    {
    protected:
        /** Creates real-device resources; other backends keep their existing coverage. */
        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);
            if (instance->getBackend() != GraphicsBackend::Metal)
            {
                GTEST_SKIP() << "Exercises the native Metal ICB conversion path.";
            }
            device = instance->createDevice();
            queue = device->getMainQueue();
            trianglePipeline = createPipeline(PrimitiveTopology::TriangleList);
            packedCommands = createBuffer(256u, BufferUsage::Indirect | BufferUsage::CopyDst);
            separateCommands = createBuffer(256u, BufferUsage::Indirect | BufferUsage::CopyDst);
            firstIndices = createBuffer(32u, BufferUsage::Index | BufferUsage::CopyDst);
            secondIndices = createBuffer(32u, BufferUsage::Index | BufferUsage::CopyDst);
            target = device->createTexture({.label = "IndirectRangeTarget",
                .usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc,
                .size = {TargetWidth, TargetHeight, 1u}, .format = TextureFormat::RGBA8Unorm});
        }

        /** Frees resources after readback and releases the test instance on every exit path. */
        void TearDown() override
        {
            if (device != nullptr)
            {
                releaseBuffer(packedCommands);
                releaseBuffer(separateCommands);
                releaseBuffer(firstIndices);
                releaseBuffer(secondIndices);
                if (!target.isNull())
                {
                    device->freeTexture(target);
                }
            }
            target.reset();
            trianglePipeline = nullptr;
            secondPipeline = nullptr;
            queue = nullptr;
            device = nullptr;
            if (instance != nullptr)
            {
                destroyInstance(instance);
                instance = nullptr;
            }
        }

        /** Creates a fullscreen identity shader with the topology captured by conversion. */
        RenderPipeline createPipeline(PrimitiveTopology topology)
        {
            auto shader = device->createShaderModule({.label = "IndirectRangeShader", .code = RangeShader});
            RenderPipelineDescriptor descriptor{};
            descriptor.label = "IndirectRangePipeline";
            descriptor.layout = device->createPipelineLayout({.bindGroupLayouts = {}});
            descriptor.vertex.module = shader;
            descriptor.vertex.entryPoint = "rangeVertex";
            descriptor.fragment.module = shader;
            descriptor.fragment.entryPoint = "rangeFragment";
            descriptor.fragment.targets.push_back({.format = TextureFormat::RGBA8Unorm});
            descriptor.primitive.topology = topology;
            descriptor.primitive.cullMode = CullMode::None;
            return device->createRenderPipeline(descriptor);
        }

        /** Allocates one source buffer using the public RHI contract. */
        Buffer createBuffer(uint64_t bytes, BufferUsageFlags usage)
        {
            return device->createBuffer({.label = "IndirectRangeBuffer", .usage = usage, .size = bytes});
        }

        /** Releases a still-owned source exactly once, including the early-free test. */
        void releaseBuffer(Buffer &buffer)
        {
            if (!buffer.isNull())
            {
                device->freeBuffer(buffer);
                buffer.reset();
            }
        }

        /** Writes one valid record while leaving all remaining commands explicitly empty. */
        void writeCommand(std::array<uint8_t, 256> &bytes, uint32_t offset,
            uint32_t firstInstance, bool indexed)
        {
            if (indexed)
            {
                const IndirectIndexedRenderCommand command{3u, 1u, 0u, 0, firstInstance};
                std::memcpy(bytes.data() + offset, &command, sizeof(command));
                return;
            }
            const IndirectRenderCommand command{3u, 1u, 0u, firstInstance};
            std::memcpy(bytes.data() + offset, &command, sizeof(command));
        }

        /** Uploads explicit record layouts and distinguishable index-binding data. */
        void uploadCommands(const IndirectRangeSettings &settings)
        {
            std::array<uint8_t, 256> firstBytes{};
            std::array<uint8_t, 256> secondBytes{};
            writeCommand(firstBytes, 0u, settings.firstInstance, settings.indexed);
            const uint32_t secondOffset = settings.secondOffset +
                settings.secondValidRecord * settings.secondStride;
            if (settings.sharedBuffer)
            {
                writeCommand(firstBytes, secondOffset, settings.secondInstance, settings.indexed);
            }
            else
            {
                writeCommand(secondBytes, 0u, settings.secondInstance, settings.indexed);
            }
            queue->writeBuffer(BufferRange(packedCommands), firstBytes.data(), firstBytes.size());
            queue->writeBuffer(BufferRange(separateCommands), secondBytes.data(), secondBytes.size());
            const std::array<uint32_t, 6> indices{0u, 1u, 2u, 3u, 4u, 5u};
            const std::array<uint32_t, 3> alternate32{3u, 4u, 5u};
            const std::array<uint16_t, 6> alternate16{3u, 4u, 5u, 0u, 1u, 2u};
            queue->writeBuffer(BufferRange(firstIndices), indices.data(), sizeof(indices));
            if (settings.secondIndexFormat == IndexFormat::Uint16)
            {
                queue->writeBuffer(BufferRange(secondIndices), alternate16.data(), sizeof(alternate16));
            }
            else
            {
                queue->writeBuffer(BufferRange(secondIndices), alternate32.data(), sizeof(alternate32));
            }
            queue->submit({});
        }

        /** Records one side with independently captured source, index and topology state. */
        void recordSide(RenderPassEncoder &pass, const IndirectRangeSettings &settings,
            bool secondSide, uint32_t commandCount)
        {
            const Buffer &commands = secondSide && !settings.sharedBuffer
                ? separateCommands : packedCommands;
            const uint32_t offset = secondSide && settings.sharedBuffer ? settings.secondOffset : 0u;
            const uint32_t stride = secondSide ? settings.secondStride : settings.firstStride;
            const BufferRange source(commands, offset, commandCount * stride);
            pass->setScissorRect(secondSide ? TargetWidth / 2u : 0u,
                0u, TargetWidth / 2u, TargetHeight);
            pass->setPipeline(secondSide ? secondPipeline : trianglePipeline);
            if (settings.indexed)
            {
                recordIndexedSide(pass, settings, secondSide, source);
                return;
            }
            pass->drawIndirect(source, commandCount, stride);
        }

        /** Applies the captured index binding and derives the logical count from the source range. */
        void recordIndexedSide(RenderPassEncoder &pass, const IndirectRangeSettings &settings,
            bool secondSide, const BufferRange &source)
        {
            const Buffer &indices = secondSide && settings.differentIndexBuffer
                ? secondIndices : firstIndices;
            const uint32_t stride = secondSide ? settings.secondStride : settings.firstStride;
            pass->setIndexBuffer(BufferRange(indices, secondSide ? settings.secondIndexOffset : 0u),
                secondSide ? settings.secondIndexFormat : IndexFormat::Uint32);
            pass->drawIndexedIndirect(source, static_cast<uint32_t>(source.size / stride), stride);
        }

        /** Submits both draws together, retaining the original per-draw render-state order. */
        void renderRanges(const IndirectRangeSettings &settings)
        {
            secondPipeline = settings.secondTopology == PrimitiveTopology::TriangleList
                ? trianglePipeline : createPipeline(settings.secondTopology);
            RenderPassDescriptor descriptor{};
            descriptor.label = "IndirectRangeRegression";
            descriptor.colorAttachments.push_back({.view = target->createView(),
                .loadOp = LoadOp::Clear, .storeOp = StoreOp::Store,
                .clearValue = {0.0, 0.0, 0.0, 1.0}});
            auto encoder = queue->createCommandEncoder();
            auto pass = encoder->beginRenderPass(descriptor);
            pass->setViewport(0.0f, 0.0f, TargetWidth, TargetHeight, 0.0f, 1.0f);
            recordSide(pass, settings, false, settings.firstCount);
            if (settings.insertZeroDraw)
            {
                recordSide(pass, settings, false, 0u);
            }
            recordSide(pass, settings, true, settings.secondCount);
            pass->end();
            encoder->end();
            queue->submit({encoder});
        }

        /** Reads the center of each scissor region and checks exact RGBA output. */
        void expectColors(const PixelColor &leftColor, const PixelColor &rightColor)
        {
            std::array<uint8_t, TargetWidth * TargetHeight * 4u> pixels{};
            queue->readTexture({.texture = target}, pixels.data(), pixels.size(),
                {.bytesPerRow = TargetWidth * 4u, .rowsPerImage = TargetHeight},
                {TargetWidth, TargetHeight, 1u});
            queue->submit({});
            const uint32_t leftPixel = (TargetHeight / 2u * TargetWidth + TargetWidth / 4u) * 4u;
            const uint32_t rightPixel = (TargetHeight / 2u * TargetWidth + 3u * TargetWidth / 4u) * 4u;
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                EXPECT_EQ(pixels[leftPixel + channel], leftColor[channel]) << "Left channel " << channel;
                EXPECT_EQ(pixels[rightPixel + channel], rightColor[channel]) << "Right channel " << channel;
            }
        }

        Instance instance = nullptr;
        Device device = nullptr;
        Queue queue = nullptr;
        RenderPipeline trianglePipeline;
        RenderPipeline secondPipeline;
        Buffer packedCommands;
        Buffer separateCommands;
        Buffer firstIndices;
        Buffer secondIndices;
        Texture target;
    };

    /** Establishes the separate-buffer control used by the original reproducer. */
    TEST_F(MetalIndirectRangeTest, SeparateBuffersPreserveBothDraws)
    {
        IndirectRangeSettings settings;
        settings.sharedBuffer = false;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Reproduces the original alias defect with two indexed ranges in one buffer. */
    TEST_F(MetalIndirectRangeTest, SharedIndexedRangesPreserveBothDraws)
    {
        IndirectRangeSettings settings;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Exercises the same alias defect through multi-command non-indexed conversion. */
    TEST_F(MetalIndirectRangeTest, SharedNonIndexedRangesPreserveBothDraws)
    {
        IndirectRangeSettings settings;
        settings.indexed = false;
        settings.firstStride = sizeof(IndirectRenderCommand);
        settings.secondStride = sizeof(IndirectRenderCommand);
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Verifies that padded indexed records do not select another draw's native result. */
    TEST_F(MetalIndirectRangeTest, DifferentIndexedStridesPreserveBothDraws)
    {
        IndirectRangeSettings settings;
        settings.secondStride = 32u;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Verifies both non-indexed conversion and the existing native single-command path. */
    TEST_F(MetalIndirectRangeTest, SingleNonIndexedDrawInterleavesWithConvertedDraw)
    {
        IndirectRangeSettings settings;
        settings.indexed = false;
        settings.firstCount = 1u;
        settings.firstStride = sizeof(IndirectRenderCommand);
        settings.secondStride = 32u;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Exercises two converted non-indexed ranges with different record padding. */
    TEST_F(MetalIndirectRangeTest, DifferentNonIndexedStridesPreserveBothDraws)
    {
        IndirectRangeSettings settings;
        settings.indexed = false;
        settings.firstStride = sizeof(IndirectRenderCommand);
        settings.secondStride = 32u;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Distinguishes two index buffers even when the original indirect range is identical. */
    TEST_F(MetalIndirectRangeTest, SameRangeCapturesDifferentIndexBuffers)
    {
        IndirectRangeSettings settings;
        settings.secondOffset = 0u;
        settings.secondInstance = 0u;
        settings.differentIndexBuffer = true;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Preserves the index element type captured separately for each draw conversion. */
    TEST_F(MetalIndirectRangeTest, SameRangeCapturesDifferentIndexFormats)
    {
        IndirectRangeSettings settings;
        settings.secondOffset = 0u;
        settings.secondInstance = 0u;
        settings.differentIndexBuffer = true;
        settings.secondIndexFormat = IndexFormat::Uint16;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Captures different byte offsets within one shared index buffer. */
    TEST_F(MetalIndirectRangeTest, SameRangeCapturesDifferentIndexOffsets)
    {
        IndirectRangeSettings settings;
        settings.secondOffset = 0u;
        settings.secondInstance = 0u;
        settings.secondIndexOffset = 3u * sizeof(uint32_t);
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Distinguishes triangle and line conversions of exactly the same command bytes. */
    TEST_F(MetalIndirectRangeTest, SameRangeCapturesDifferentTopologies)
    {
        IndirectRangeSettings settings;
        settings.secondOffset = 0u;
        settings.secondInstance = 0u;
        settings.secondTopology = PrimitiveTopology::LineList;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Black);
    }

    /** Allows repeated identical ranges without sharing mutable preparation destinations. */
    TEST_F(MetalIndirectRangeTest, RepeatedIdenticalRangeRemainsCorrect)
    {
        IndirectRangeSettings settings;
        settings.secondOffset = 0u;
        settings.secondInstance = 0u;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Red);
    }

    /** Covers overlapping source ranges containing distinct non-empty command positions. */
    TEST_F(MetalIndirectRangeTest, OverlappingSourceRangesPreserveBothDraws)
    {
        IndirectRangeSettings settings;
        settings.secondOffset = sizeof(IndirectIndexedRenderCommand);
        settings.secondValidRecord = 1u;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Treats an intervening zero-command draw as a no-op without consuming a conversion slot. */
    TEST_F(MetalIndirectRangeTest, ZeroCommandDrawDoesNotOverwritePreparedResults)
    {
        IndirectRangeSettings settings;
        settings.insertZeroDraw = true;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
    }

    /** Reuses native allocations across submissions while regenerating their command contents. */
    TEST_F(MetalIndirectRangeTest, RewrittenSourceChangesBothDrawsAcrossFrames)
    {
        IndirectRangeSettings settings;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
        settings.firstInstance = 1u;
        settings.secondInstance = 0u;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Green, Red);
    }

    /** Shrinking the execution count must not expose stale commands from a previous submission. */
    TEST_F(MetalIndirectRangeTest, ShrinkingCommandCountsExcludeOldCommands)
    {
        IndirectRangeSettings settings;
        settings.secondValidRecord = 1u;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Red, Green);
        settings.firstCount = 1u;
        settings.secondCount = 1u;
        settings.secondValidRecord = 0u;
        settings.firstInstance = 1u;
        settings.secondInstance = 0u;
        uploadCommands(settings);
        renderRanges(settings);
        expectColors(Green, Red);
    }

    /** Native commands retain their inputs and results until submitted GPU work has finished. */
    TEST_F(MetalIndirectRangeTest, SourceReleaseAfterSubmissionPreservesDraws)
    {
        IndirectRangeSettings settings;
        uploadCommands(settings);
        renderRanges(settings);
        releaseBuffer(packedCommands);
        expectColors(Red, Green);
    }

    /** Confirms resource reuse within one queue and isolation across native queues and slots. */
    TEST_F(MetalIndirectRangeTest, NativeStorageReusesSlotsAndSeparatesQueues)
    {
        auto &metalDevice = *static_cast<Metal::MDevice *>(device);
        auto &metalBuffer = *static_cast<Metal::MBuffer *>(packedCommands.get());
        const auto firstQueue = NS::TransferPtr(metalDevice.getNativeDevice()->newCommandQueue());
        const auto secondQueue = NS::TransferPtr(metalDevice.getNativeDevice()->newCommandQueue());
        const auto &first = metalBuffer.getOrCreateNativeICB(
            MTL::IndirectCommandTypeDrawIndexed, *firstQueue.get(), 0u, 2u);
        const auto *firstCommands = first.commands.get();
        const auto *firstArgument = first.argumentBuffer.get();
        const auto &nextSlot = metalBuffer.getOrCreateNativeICB(
            MTL::IndirectCommandTypeDrawIndexed, *firstQueue.get(), 1u, 2u);
        const auto &otherQueue = metalBuffer.getOrCreateNativeICB(
            MTL::IndirectCommandTypeDrawIndexed, *secondQueue.get(), 0u, 2u);
        const auto &reused = metalBuffer.getOrCreateNativeICB(
            MTL::IndirectCommandTypeDrawIndexed, *firstQueue.get(), 0u, 1u);
        EXPECT_EQ(firstCommands, reused.commands.get());
        EXPECT_EQ(firstArgument, reused.argumentBuffer.get());
        EXPECT_NE(firstCommands, nextSlot.commands.get());
        EXPECT_NE(firstCommands, otherQueue.commands.get());
        uint64_t argumentResourceId = 0u;
        std::memcpy(&argumentResourceId, reused.argumentBuffer->contents(), sizeof(argumentResourceId));
        EXPECT_EQ(argumentResourceId, reused.commands->gpuResourceID()._impl);
        EXPECT_EQ(reused.commandCapacity, 2u);
        EXPECT_EQ(reused.commands->size(), 2u);
        const auto retainedCommands = NS::RetainPtr(reused.commands.get());
        const auto &grown = metalBuffer.getOrCreateNativeICB(
            MTL::IndirectCommandTypeDrawIndexed, *firstQueue.get(), 0u, 5u);
        EXPECT_EQ(grown.commandCapacity, 5u);
        EXPECT_EQ(grown.commands->size(), 5u);
        EXPECT_NE(grown.commands.get(), retainedCommands.get());
        const auto &smaller = metalBuffer.getOrCreateNativeICB(
            MTL::IndirectCommandTypeDrawIndexed, *firstQueue.get(), 0u, 2u);
        EXPECT_EQ(smaller.commands.get(), grown.commands.get());
    }

    /** Keeps conversion capacity proportional to actual work even when the source allocation is much larger. */
    TEST_F(MetalIndirectRangeTest, SmallDrawDoesNotAllocateSourceSizedConversionStorage)
    {
        auto source = createBuffer(64u * 1024u * 1024u, BufferUsage::Indirect);
        auto &metalBuffer = *static_cast<Metal::MBuffer *>(source.get());
        const auto &storage = metalBuffer.getOrCreateNativeICB(
            MTL::IndirectCommandTypeDrawIndexed, *static_cast<Metal::MDevice *>(device)->getMainQueueImpl()->getNativeQueue(), 0u, 2u);
        EXPECT_EQ(storage.commands->size(), 2u);
        EXPECT_EQ(storage.commandCapacity, 2u);
        device->freeBuffer(source);
    }
}
