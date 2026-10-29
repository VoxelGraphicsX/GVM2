#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>
#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/GVMRHI.Vulkan/VKTesting.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "MCommandEncoder.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace GVM::RHI::Vulkan
{
    using VKSubmissionSerial = uint64_t;

    class VKQueue : public QueueImpl
    {
    public:
        [[nodiscard]] VKSubmissionSerial getMostRecentSubmissionSerial() const;
        void waitForSubmission(VKSubmissionSerial serial, const char *waitSource = "unspecified");
    };
} // namespace GVM::RHI::Vulkan

namespace
{
    const char *backendName(GVM::RHI::GraphicsBackend backend)
    {
        switch (backend)
        {
        case GVM::RHI::GraphicsBackend::Metal:
            return "metal";
        case GVM::RHI::GraphicsBackend::Vulkan:
            return "vulkan";
        default:
            return "undefined";
        }
    }

    constexpr const char *kComputeWriteShaderCode = R"(
#include <metal_stdlib>
using namespace metal;

struct WriteOutputBindGroup
{
    device uint *values [[id(0)]];
};

kernel void fill_main(uint3 gid [[thread_position_in_grid]], const constant WriteOutputBindGroup *bindGroup [[buffer(0)]])
{
    bindGroup->values[gid.x] = gid.x * 17u + 5u;
}
)";

    constexpr const char *kBindGroupPersistenceShaderCode = R"(
#include <metal_stdlib>
using namespace metal;

struct PersistenceBindGroup
{
    device uint *value [[id(0)]];
};

struct PersistenceVertexIn
{
    float2 position [[attribute(0)]];
};

struct PersistenceVertexOut
{
    float4 position [[position]];
};

struct PersistenceGroupVertexOut
{
    float4 position [[position]];
    uint value [[flat]];
};

vertex PersistenceVertexOut procedural_vertex_main(uint vertexId [[vertex_id]])
{
    constexpr float2 positions[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    PersistenceVertexOut output;
    output.position = float4(positions[vertexId], 0, 1);
    return output;
}

vertex PersistenceVertexOut buffered_vertex_main(PersistenceVertexIn input [[stage_in]])
{
    PersistenceVertexOut output;
    output.position = float4(input.position, 0, 1);
    return output;
}

fragment float4 write_zero_main(PersistenceVertexOut input [[stage_in]])
{
    return float4(0, 0, 0, 1);
}

vertex PersistenceGroupVertexOut buffered_group_vertex_main(PersistenceVertexIn input [[stage_in]],
                                 const constant PersistenceBindGroup *bindGroup [[buffer(0)]])
{
    PersistenceGroupVertexOut output;
    output.position = float4(input.position, 0, 1);
    output.value = bindGroup->value[0];
    return output;
}

fragment float4 write_vertex_group_main(PersistenceGroupVertexOut input [[stage_in]])
{
    return float4(0, float(input.value) / 255.0, 0, 1);
}

fragment float4 write_white_main(PersistenceVertexOut input [[stage_in]])
{
    return float4(1, 1, 1, 1);
}
)";

    class RhiExecutionTest : public ::testing::Test
    {
    protected:
        struct ComputeObjects
        {
            GVM::RHI::Buffer outputBuffer;
            GVM::RHI::BindGroupLayout bindGroupLayout;
            GVM::RHI::BindGroup bindGroup;
            GVM::RHI::PipelineLayout pipelineLayout;
            GVM::RHI::ShaderModule shaderModule;
            GVM::RHI::ComputePipeline computePipeline;
        };

        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);

            device = instance->createDevice();
            ASSERT_NE(device, nullptr);
            ASSERT_NE(device->getMainQueue(), nullptr);
        }

        void TearDown() override
        {
            if (instance != nullptr)
            {
                GVM::RHI::destroyInstance(instance);
                instance = nullptr;
            }
            device = nullptr;
        }

        void submitAndWait(const eastl::vector<GVM::RHI::CommandEncoder> &encoders)
        {
            device->getMainQueue()->submit(encoders);
            if (instance == nullptr)
            {
                return;
            }

            if (instance->getBackend() == GVM::RHI::GraphicsBackend::Metal)
            {
                auto *metalEncoder = static_cast<GVM::RHI::Metal::MCommandEncoder *>(encoders.back().get());
                ASSERT_NE(metalEncoder, nullptr);
                metalEncoder->waitUntilCompleted();
                return;
            }

            if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan)
            {
                auto *vulkanQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(device->getMainQueue());
                ASSERT_NE(vulkanQueue, nullptr);
                const auto serial = vulkanQueue->getMostRecentSubmissionSerial();
                if (serial != 0u)
                {
                    vulkanQueue->waitForSubmission(serial);
                }
            }
        }

        [[nodiscard]]
        bool inlineMetalComputeSupported() const
        {
            return instance != nullptr && instance->getBackend() == GVM::RHI::GraphicsBackend::Metal;
        }

        ComputeObjects createComputeObjects()
        {
            constexpr uint64_t kByteSize = sizeof(uint32_t) * 4;

            ComputeObjects objects = {};
            objects.outputBuffer = device->createBuffer({
                .label = "ComputeOutput",
                .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
                .size = kByteSize,
            });
            EXPECT_FALSE(objects.outputBuffer.isNull());

            GVM::RHI::BindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
            bindGroupLayoutDescriptor.label = "ComputeOutputBindGroupLayout";
            bindGroupLayoutDescriptor.entries.resize(1);
            bindGroupLayoutDescriptor.entries[0].binding = 0;
            bindGroupLayoutDescriptor.entries[0].visibility = GVM::RHI::ShaderStage::Compute;
            bindGroupLayoutDescriptor.entries[0].buffer.type = GVM::RHI::BufferBindingType::Storage;
            bindGroupLayoutDescriptor.entries[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadWrite;
            objects.bindGroupLayout = device->createBindGroupLayout(bindGroupLayoutDescriptor);
            EXPECT_TRUE(static_cast<bool>(objects.bindGroupLayout));

            GVM::RHI::BindGroupDescriptor bindGroupDescriptor = {};
            bindGroupDescriptor.label = "ComputeOutputBindGroup";
            bindGroupDescriptor.layout = objects.bindGroupLayout;
            GVM::RHI::BindGroupEntry bindGroupEntry = {};
            bindGroupEntry.binding = 0;
            bindGroupEntry.buffer = GVM::RHI::BufferRange(objects.outputBuffer, 0, kByteSize);
            bindGroupDescriptor.entries = bindGroupEntry;
            objects.bindGroup = device->createBindGroup(bindGroupDescriptor);
            EXPECT_TRUE(static_cast<bool>(objects.bindGroup));

            GVM::RHI::PipelineLayoutDescriptor pipelineLayoutDescriptor = {};
            pipelineLayoutDescriptor.label = "ComputeReadbackPipelineLayout";
            pipelineLayoutDescriptor.bindGroupLayouts.resize(1);
            pipelineLayoutDescriptor.bindGroupLayouts[0] = objects.bindGroupLayout;
            objects.pipelineLayout = device->createPipelineLayout(pipelineLayoutDescriptor);
            EXPECT_TRUE(static_cast<bool>(objects.pipelineLayout));

            objects.shaderModule = device->createShaderModule({
                .label = "ComputeWriteShader",
                .code = kComputeWriteShaderCode,
            });
            EXPECT_TRUE(static_cast<bool>(objects.shaderModule));

            objects.computePipeline = device->createComputePipeline({
                .label = "ComputeReadbackPipeline",
                .layout = objects.pipelineLayout,
                .compute = {
                    .module = objects.shaderModule,
                    .entryPoint = "fill_main",
                    .workgroupX = 1,
                    .workgroupY = 1,
                    .workgroupZ = 1,
                },
            });
            EXPECT_TRUE(static_cast<bool>(objects.computePipeline));

            return objects;
        }

        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device device = nullptr;
    };

    TEST_F(RhiExecutionTest, CreatesStorageBindGroupForComputeDispatch)
    {
        if (!inlineMetalComputeSupported())
        {
            GTEST_SKIP() << "This readback case currently validates inline MSL compute execution and is skipped on backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }
        const auto objects = createComputeObjects();

        ASSERT_FALSE(objects.outputBuffer.isNull());
        ASSERT_TRUE(static_cast<bool>(objects.bindGroupLayout));
        ASSERT_TRUE(static_cast<bool>(objects.bindGroup));
        ASSERT_TRUE(static_cast<bool>(objects.pipelineLayout));
        ASSERT_TRUE(static_cast<bool>(objects.shaderModule));
        ASSERT_TRUE(static_cast<bool>(objects.computePipeline));

        device->freeBuffer(objects.outputBuffer);
    }

    TEST_F(RhiExecutionTest, DispatchesComputePipelineCompletesWithoutGpuError)
    {
        if (!inlineMetalComputeSupported())
        {
            GTEST_SKIP() << "This readback case currently validates inline MSL compute execution and is skipped on backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }
        const auto objects = createComputeObjects();
        ASSERT_TRUE(static_cast<bool>(objects.computePipeline));

        auto commandEncoder = device->getMainQueue()->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));
        auto computePass = commandEncoder->beginComputePass({.label = "ComputeExecutionPass"});
        ASSERT_TRUE(static_cast<bool>(computePass));

        computePass->setPipeline(objects.computePipeline);
        computePass->setBindGroup(objects.bindGroup, 0);
        computePass->dispatchWorkgroups(4, 1, 1);
        computePass->end();
        commandEncoder->end();

        eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
        encoders[0] = commandEncoder;
        submitAndWait(encoders);

        device->freeBuffer(objects.outputBuffer);
    }

    TEST_F(RhiExecutionTest, DispatchesComputePipelineWritesReadableMappedStorageBuffer)
    {
        if (!inlineMetalComputeSupported())
        {
            GTEST_SKIP() << "This readback case currently validates inline MSL compute execution and is skipped on backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }
        const auto objects = createComputeObjects();
        ASSERT_TRUE(static_cast<bool>(objects.computePipeline));

        auto commandEncoder = device->getMainQueue()->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));
        auto computePass = commandEncoder->beginComputePass({.label = "ComputeReadbackPass"});
        ASSERT_TRUE(static_cast<bool>(computePass));

        computePass->setPipeline(objects.computePipeline);
        computePass->setBindGroup(objects.bindGroup, 0);
        computePass->dispatchWorkgroups(4, 1, 1);
        computePass->end();
        commandEncoder->end();

        eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
        encoders[0] = commandEncoder;
        submitAndWait(encoders);

        objects.outputBuffer->map();
        const auto *mapped = static_cast<const uint32_t *>(objects.outputBuffer->getConstMappedRange(0, sizeof(uint32_t) * 4));
        ASSERT_NE(mapped, nullptr);
        EXPECT_EQ(mapped[0], 5u);
        EXPECT_EQ(mapped[1], 22u);
        EXPECT_EQ(mapped[2], 39u);
        EXPECT_EQ(mapped[3], 56u);
        objects.outputBuffer->unmap();

        device->freeBuffer(objects.outputBuffer);
    }

    TEST_F(RhiExecutionTest, BlitCopiesStorageOnlyComputeOutputIntoMappedReadbackBuffer)
    {
        if (!inlineMetalComputeSupported())
        {
            GTEST_SKIP() << "This readback case currently validates inline MSL compute execution and is skipped on backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }
        constexpr uint64_t kByteSize = sizeof(uint32_t) * 4;

        auto outputBuffer = device->createBuffer({
            .label = "ComputeStorageOnlyOutput",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc,
            .size = kByteSize,
        });
        auto readbackBuffer = device->createBuffer({
            .label = "ComputeStorageReadback",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = kByteSize,
        });
        ASSERT_FALSE(outputBuffer.isNull());
        ASSERT_FALSE(readbackBuffer.isNull());

        GVM::RHI::BindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
        bindGroupLayoutDescriptor.label = "ComputeOutputBindGroupLayout";
        bindGroupLayoutDescriptor.entries.resize(1);
        bindGroupLayoutDescriptor.entries[0].binding = 0;
        bindGroupLayoutDescriptor.entries[0].visibility = GVM::RHI::ShaderStage::Compute;
        bindGroupLayoutDescriptor.entries[0].buffer.type = GVM::RHI::BufferBindingType::Storage;
        bindGroupLayoutDescriptor.entries[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadWrite;
        auto bindGroupLayout = device->createBindGroupLayout(bindGroupLayoutDescriptor);
        ASSERT_TRUE(static_cast<bool>(bindGroupLayout));

        GVM::RHI::BindGroupDescriptor bindGroupDescriptor = {};
        bindGroupDescriptor.label = "ComputeStorageOnlyOutputBindGroup";
        bindGroupDescriptor.layout = bindGroupLayout;
        GVM::RHI::BindGroupEntry bindGroupEntry = {};
        bindGroupEntry.binding = 0;
        bindGroupEntry.buffer = GVM::RHI::BufferRange(outputBuffer, 0, kByteSize);
        bindGroupDescriptor.entries = bindGroupEntry;
        auto bindGroup = device->createBindGroup(bindGroupDescriptor);
        ASSERT_TRUE(static_cast<bool>(bindGroup));

        GVM::RHI::PipelineLayoutDescriptor pipelineLayoutDescriptor = {};
        pipelineLayoutDescriptor.label = "ComputeStorageOnlyPipelineLayout";
        pipelineLayoutDescriptor.bindGroupLayouts.resize(1);
        pipelineLayoutDescriptor.bindGroupLayouts[0] = bindGroupLayout;
        auto pipelineLayout = device->createPipelineLayout(pipelineLayoutDescriptor);
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));

        auto shaderModule = device->createShaderModule({
            .label = "ComputeWriteShader",
            .code = kComputeWriteShaderCode,
        });
        ASSERT_TRUE(static_cast<bool>(shaderModule));

        auto computePipeline = device->createComputePipeline({
            .label = "ComputeStorageOnlyPipeline",
            .layout = pipelineLayout,
            .compute = {
                .module = shaderModule,
                .entryPoint = "fill_main",
                .workgroupX = 1,
                .workgroupY = 1,
                .workgroupZ = 1,
            },
        });
        ASSERT_TRUE(static_cast<bool>(computePipeline));

        auto commandEncoder = device->getMainQueue()->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));
        auto computePass = commandEncoder->beginComputePass({.label = "ComputeStorageOnlyPass"});
        ASSERT_TRUE(static_cast<bool>(computePass));
        computePass->setPipeline(computePipeline);
        computePass->setBindGroup(bindGroup, 0);
        computePass->dispatchWorkgroups(4, 1, 1);
        computePass->end();

        auto blitPass = commandEncoder->beginBlitPass({.label = "ComputeStorageOnlyReadbackPass"});
        ASSERT_TRUE(static_cast<bool>(blitPass));
        blitPass->copyBufferToBuffer(GVM::RHI::BufferRange(outputBuffer, 0, kByteSize), GVM::RHI::BufferRange(readbackBuffer, 0, kByteSize));
        blitPass->end();
        commandEncoder->end();

        eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
        encoders[0] = commandEncoder;
        submitAndWait(encoders);

        readbackBuffer->map();
        const auto *mapped = static_cast<const uint32_t *>(readbackBuffer->getConstMappedRange(0, kByteSize));
        ASSERT_NE(mapped, nullptr);
        EXPECT_EQ(mapped[0], 5u);
        EXPECT_EQ(mapped[1], 22u);
        EXPECT_EQ(mapped[2], 39u);
        EXPECT_EQ(mapped[3], 56u);
        readbackBuffer->unmap();

        device->freeBuffer(readbackBuffer);
        device->freeBuffer(outputBuffer);
    }

    TEST_F(RhiExecutionTest, SubmitsBlitFillPassCompletesWithoutGpuError)
    {
        auto buffer = device->createBuffer({
            .label = "BlitFillBuffer",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 256,
        });
        ASSERT_FALSE(buffer.isNull());

        auto commandEncoder = device->getMainQueue()->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));
        auto blitPass = commandEncoder->beginBlitPass({.label = "BlitFillPass"});
        ASSERT_TRUE(static_cast<bool>(blitPass));

        blitPass->fillBuffer(GVM::RHI::BufferRange(buffer, 0, 256), 0x7f);
        blitPass->end();
        commandEncoder->end();

        eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
        encoders[0] = commandEncoder;
        submitAndWait(encoders);

        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, RepeatedComputeDispatchBatchesComplete)
    {
        if (!inlineMetalComputeSupported())
        {
            GTEST_SKIP() << "This readback case currently validates inline MSL compute execution and is skipped on backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }
        const auto objects = createComputeObjects();
        ASSERT_TRUE(static_cast<bool>(objects.computePipeline));

        for (uint32_t iteration = 0; iteration < 6; ++iteration)
        {
            SCOPED_TRACE(iteration);

            auto commandEncoder = device->getMainQueue()->createCommandEncoder();
            ASSERT_TRUE(static_cast<bool>(commandEncoder));
            auto computePass = commandEncoder->beginComputePass({.label = "RepeatedComputePass"});
            ASSERT_TRUE(static_cast<bool>(computePass));

            computePass->setPipeline(objects.computePipeline);
            computePass->setBindGroup(objects.bindGroup, 0);
            computePass->dispatchWorkgroups(4, 1, 1);
            computePass->end();
            commandEncoder->end();

            eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
            encoders[0] = commandEncoder;
            submitAndWait(encoders);
        }

        device->freeBuffer(objects.outputBuffer);
    }

    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionRejectsNonAlignedRanges)
    {
        auto srcBuffer = device->createBuffer({
            .label = "MultiRegionSrc",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 16,
        });
        auto dstBuffer = device->createBuffer({
            .label = "MultiRegionDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 16,
        });
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionDescriptor",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = sizeof(GVM::RHI::BufferCopyRegion),
        });

        ASSERT_FALSE(srcBuffer.isNull());
        ASSERT_FALSE(dstBuffer.isNull());
        ASSERT_FALSE(regionBuffer.isNull());

        const std::array<std::uint8_t, 16> srcBytes = {
            0x10, 0x20, 0x30, 0x40,
            0x50, 0x60, 0x70, 0x80,
            0x90, 0xa0, 0xb0, 0xc0,
            0xd0, 0xe0, 0xf0, 0xff
        };
        std::array<std::uint8_t, 16> dstBytes = {};
        dstBytes.fill(0xcd);

        srcBuffer->map();
        dstBuffer->map();
        auto *srcMapped = static_cast<std::uint8_t *>(srcBuffer->getMappedRange(0, srcBytes.size()));
        auto *dstMapped = static_cast<std::uint8_t *>(dstBuffer->getMappedRange(0, dstBytes.size()));
        ASSERT_NE(srcMapped, nullptr);
        ASSERT_NE(dstMapped, nullptr);
        std::memcpy(srcMapped, srcBytes.data(), srcBytes.size());
        std::memcpy(dstMapped, dstBytes.data(), dstBytes.size());
        srcBuffer->unmap();
        dstBuffer->unmap();

        regionBuffer->map();
        const GVM::RHI::BufferCopyRegion region = {
            .srcOffset = 1,
            .dstOffset = 5,
            .size = 6,
        };
        auto *regionMapped = static_cast<GVM::RHI::BufferCopyRegion *>(regionBuffer->getMappedRange(0, sizeof(region)));
        ASSERT_NE(regionMapped, nullptr);
        *regionMapped = region;
        regionBuffer->unmap();

        EXPECT_THROW(device->getMainQueue()->copyBufferToBufferMultipleRegion(srcBuffer, dstBuffer, regionBuffer, 1), std::invalid_argument);

        device->freeBuffer(regionBuffer);
        device->freeBuffer(dstBuffer);
        device->freeBuffer(srcBuffer);
    }

    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionCopiesAlignedWordRanges)
    {
        auto srcBuffer = device->createBuffer({
            .label = "MultiRegionFastPathSrc",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 32,
        });
        auto dstBuffer = device->createBuffer({
            .label = "MultiRegionFastPathDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 32,
        });
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionFastPathDescriptor",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = sizeof(GVM::RHI::BufferCopyRegion),
        });

        ASSERT_FALSE(srcBuffer.isNull());
        ASSERT_FALSE(dstBuffer.isNull());
        ASSERT_FALSE(regionBuffer.isNull());

        std::array<std::uint8_t, 32> srcBytes = {};
        std::array<std::uint8_t, 32> dstBytes = {};
        for (std::size_t index = 0; index < srcBytes.size(); ++index)
        {
            srcBytes[index] = static_cast<std::uint8_t>(0x20u + index);
            dstBytes[index] = static_cast<std::uint8_t>(0xd0u + index);
        }

        srcBuffer->map();
        dstBuffer->map();
        auto *srcMapped = static_cast<std::uint8_t *>(srcBuffer->getMappedRange(0, srcBytes.size()));
        auto *dstMapped = static_cast<std::uint8_t *>(dstBuffer->getMappedRange(0, dstBytes.size()));
        ASSERT_NE(srcMapped, nullptr);
        ASSERT_NE(dstMapped, nullptr);
        std::memcpy(srcMapped, srcBytes.data(), srcBytes.size());
        std::memcpy(dstMapped, dstBytes.data(), dstBytes.size());
        srcBuffer->unmap();
        dstBuffer->unmap();

        regionBuffer->map();
        const GVM::RHI::BufferCopyRegion region = {
            .srcOffset = 4,
            .dstOffset = 12,
            .size = 12,
        };
        auto *regionMapped = static_cast<GVM::RHI::BufferCopyRegion *>(regionBuffer->getMappedRange(0, sizeof(region)));
        ASSERT_NE(regionMapped, nullptr);
        *regionMapped = region;
        regionBuffer->unmap();

        device->getMainQueue()->copyBufferToBufferMultipleRegion(srcBuffer, dstBuffer, regionBuffer, 1);

        auto commandEncoder = device->getMainQueue()->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));
        commandEncoder->end();

        eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
        encoders[0] = commandEncoder;
        submitAndWait(encoders);

        dstBuffer->map();
        const auto *resultBytes = static_cast<const std::uint8_t *>(dstBuffer->getConstMappedRange(0, dstBytes.size()));
        ASSERT_NE(resultBytes, nullptr);
        for (std::uint32_t index = 0; index < region.size; ++index)
        {
            EXPECT_EQ(resultBytes[region.dstOffset + index], srcBytes[region.srcOffset + index]);
        }
        EXPECT_EQ(resultBytes[region.dstOffset - 1], dstBytes[region.dstOffset - 1]);
        EXPECT_EQ(resultBytes[region.dstOffset + region.size], dstBytes[region.dstOffset + region.size]);
        dstBuffer->unmap();

        device->freeBuffer(regionBuffer);
        device->freeBuffer(dstBuffer);
        device->freeBuffer(srcBuffer);
    }

    /** Verifies that the RenderSet-sized copy path processes regions beyond one compute workgroup. */
    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionCopiesLargeMultiWorkgroupBatch)
    {
        constexpr uint32_t RegionCount = 130u;
        constexpr uint32_t RegionBytes = 4096u;
        constexpr uint32_t StorageBytes = RegionCount * RegionBytes;
        auto srcBuffer = device->createBuffer({
            .label = "MultiRegionLargeSrc",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::Storage |
                GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = StorageBytes,
        });
        auto dstBuffer = device->createBuffer({
            .label = "MultiRegionLargeDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Storage |
                GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = StorageBytes,
        });
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionLargeDescriptor",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead |
                GVM::RHI::BufferUsage::MapWrite,
            .size = uint64_t(RegionCount) * sizeof(GVM::RHI::BufferCopyRegion),
        });

        ASSERT_FALSE(srcBuffer.isNull());
        ASSERT_FALSE(dstBuffer.isNull());
        ASSERT_FALSE(regionBuffer.isNull());
        srcBuffer->map();
        dstBuffer->map();
        auto *sourceWords = static_cast<uint32_t *>(
            srcBuffer->getMappedRange(0u, StorageBytes));
        auto *destinationWords = static_cast<uint32_t *>(
            dstBuffer->getMappedRange(0u, StorageBytes));
        ASSERT_NE(sourceWords, nullptr);
        ASSERT_NE(destinationWords, nullptr);
        for (uint32_t word = 0u; word < StorageBytes / sizeof(uint32_t); ++word)
        {
            sourceWords[word] = word * 17u + 5u;
            destinationWords[word] = 0xcdcdcdcdu;
        }
        srcBuffer->unmap();
        dstBuffer->unmap();

        regionBuffer->map();
        auto *regions = static_cast<GVM::RHI::BufferCopyRegion *>(
            regionBuffer->getMappedRange(
                0u, uint64_t(RegionCount) * sizeof(GVM::RHI::BufferCopyRegion)));
        ASSERT_NE(regions, nullptr);
        for (uint32_t region = 0u; region < RegionCount; ++region)
        {
            regions[region] = {
                .srcOffset = region * RegionBytes,
                .dstOffset = region * RegionBytes,
                .size = RegionBytes,
            };
        }
        regionBuffer->unmap();

        device->getMainQueue()->copyBufferToBufferMultipleRegion(
            srcBuffer, dstBuffer, regionBuffer, RegionCount);
        auto commandEncoder = device->getMainQueue()->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));
        commandEncoder->end();
        eastl::vector<GVM::RHI::CommandEncoder> encoders(1u);
        encoders[0u] = commandEncoder;
        submitAndWait(encoders);

        dstBuffer->map();
        const auto *resultWords = static_cast<const uint32_t *>(
            dstBuffer->getConstMappedRange(0u, StorageBytes));
        ASSERT_NE(resultWords, nullptr);
        for (uint32_t region = 0u; region < RegionCount; ++region)
        {
            const uint32_t firstWord = region * RegionBytes / sizeof(uint32_t);
            const uint32_t lastWord = firstWord + RegionBytes / sizeof(uint32_t) - 1u;
            EXPECT_EQ(resultWords[firstWord], firstWord * 17u + 5u);
            EXPECT_EQ(resultWords[lastWord], lastWord * 17u + 5u);
        }
        dstBuffer->unmap();

        device->freeBuffer(regionBuffer);
        device->freeBuffer(dstBuffer);
        device->freeBuffer(srcBuffer);
    }

    /** Verifies that region counts beyond one 65472-region dispatch chunk copy correctly on both sides of the boundary. */
    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionCopiesMoreRegionsThanOneDispatchChunk)
    {
        constexpr uint32_t RegionCount = 65473u;
        constexpr uint32_t RegionBytes = 4u;
        constexpr uint64_t StorageBytes = uint64_t(RegionCount) * RegionBytes;
        const auto usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc |
            GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite;
        auto srcBuffer = device->createBuffer({.label = "MultiRegionChunkedSrc", .usage = usage, .size = StorageBytes});
        auto dstBuffer = device->createBuffer({.label = "MultiRegionChunkedDst", .usage = usage, .size = StorageBytes});
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionChunkedDescriptor",
            .usage = usage,
            .size = uint64_t(RegionCount) * sizeof(GVM::RHI::BufferCopyRegion),
        });

        ASSERT_FALSE(srcBuffer.isNull());
        ASSERT_FALSE(dstBuffer.isNull());
        ASSERT_FALSE(regionBuffer.isNull());
        srcBuffer->map();
        dstBuffer->map();
        auto *sourceWords = static_cast<uint32_t *>(srcBuffer->getMappedRange(0u, StorageBytes));
        auto *destinationWords = static_cast<uint32_t *>(dstBuffer->getMappedRange(0u, StorageBytes));
        ASSERT_NE(sourceWords, nullptr);
        ASSERT_NE(destinationWords, nullptr);
        for (uint32_t word = 0u; word < RegionCount; ++word)
        {
            sourceWords[word] = word + 1u;
            destinationWords[word] = 0xcdcdcdcdu;
        }
        srcBuffer->unmap();
        dstBuffer->unmap();

        regionBuffer->map();
        auto *regions = static_cast<GVM::RHI::BufferCopyRegion *>(
            regionBuffer->getMappedRange(0u, uint64_t(RegionCount) * sizeof(GVM::RHI::BufferCopyRegion)));
        ASSERT_NE(regions, nullptr);
        for (uint32_t region = 0u; region < RegionCount; ++region)
        {
            regions[region] = {
                .srcOffset = region * RegionBytes,
                .dstOffset = region * RegionBytes,
                .size = RegionBytes,
            };
        }
        regionBuffer->unmap();

        device->getMainQueue()->copyBufferToBufferMultipleRegion(srcBuffer, dstBuffer, regionBuffer, RegionCount);
        auto commandEncoder = device->getMainQueue()->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));
        commandEncoder->end();
        eastl::vector<GVM::RHI::CommandEncoder> encoders(1u);
        encoders[0u] = commandEncoder;
        submitAndWait(encoders);

        dstBuffer->map();
        const auto *resultWords = static_cast<const uint32_t *>(dstBuffer->getConstMappedRange(0u, StorageBytes));
        ASSERT_NE(resultWords, nullptr);
        constexpr uint32_t ChunkBoundary = 65472u;
        EXPECT_EQ(resultWords[0u], 1u);
        EXPECT_EQ(resultWords[ChunkBoundary - 1u], ChunkBoundary);
        EXPECT_EQ(resultWords[ChunkBoundary], ChunkBoundary + 1u);
        EXPECT_EQ(resultWords[RegionCount - 1u], RegionCount);
        dstBuffer->unmap();

        device->freeBuffer(regionBuffer);
        device->freeBuffer(dstBuffer);
        device->freeBuffer(srcBuffer);
    }

    /** Verifies that queued writes and multi-region copies execute in call order within one submission. */
    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionPreservesCallOrderBetweenQueuedWritesAndMultiRegionCopies)
    {
        const auto copyUsage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc |
            GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite;
        auto srcBuffer = device->createBuffer({.label = "MultiRegionOrderSrc", .usage = copyUsage, .size = 16u});
        auto dstBuffer = device->createBuffer({.label = "MultiRegionOrderDst", .usage = copyUsage, .size = 16u});
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionOrderDescriptor",
            .usage = copyUsage,
            .size = sizeof(GVM::RHI::BufferCopyRegion),
        });
        ASSERT_FALSE(srcBuffer.isNull());
        ASSERT_FALSE(dstBuffer.isNull());
        ASSERT_FALSE(regionBuffer.isNull());

        srcBuffer->map();
        auto *sourceWords = static_cast<uint32_t *>(srcBuffer->getMappedRange(0u, 16u));
        ASSERT_NE(sourceWords, nullptr);
        for (uint32_t word = 0u; word < 4u; ++word)
        {
            sourceWords[word] = 2u;
        }
        srcBuffer->unmap();
        regionBuffer->map();
        auto *regions = static_cast<GVM::RHI::BufferCopyRegion *>(
            regionBuffer->getMappedRange(0u, sizeof(GVM::RHI::BufferCopyRegion)));
        ASSERT_NE(regions, nullptr);
        regions[0] = {.srcOffset = 0u, .dstOffset = 0u, .size = 16u};
        regionBuffer->unmap();

        const std::array<uint32_t, 4> writeA = {1u, 1u, 1u, 1u};
        const std::array<uint32_t, 2> writeC = {3u, 3u};
        auto *queue = device->getMainQueue();
        queue->writeBuffer(GVM::RHI::BufferRange{dstBuffer, 0u, 16u}, writeA.data(), writeA.size() * sizeof(uint32_t));
        queue->copyBufferToBufferMultipleRegion(srcBuffer, dstBuffer, regionBuffer, 1u);
        queue->writeBuffer(GVM::RHI::BufferRange{dstBuffer, 8u, 8u}, writeC.data(), writeC.size() * sizeof(uint32_t));

        auto commandEncoder = queue->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));
        commandEncoder->end();
        eastl::vector<GVM::RHI::CommandEncoder> encoders(1u);
        encoders[0u] = commandEncoder;
        submitAndWait(encoders);

        dstBuffer->map();
        const auto *resultWords = static_cast<const uint32_t *>(dstBuffer->getConstMappedRange(0u, 16u));
        ASSERT_NE(resultWords, nullptr);
        EXPECT_EQ(resultWords[0u], 2u);
        EXPECT_EQ(resultWords[1u], 2u);
        EXPECT_EQ(resultWords[2u], 3u);
        EXPECT_EQ(resultWords[3u], 3u);
        dstBuffer->unmap();

        device->freeBuffer(regionBuffer);
        device->freeBuffer(dstBuffer);
        device->freeBuffer(srcBuffer);
    }

    /** Verifies that a destination buffer created without Storage usage is rejected on both backends. */
    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionRejectsDestinationWithoutStorageUsage)
    {
        const auto usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc |
            GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite;
        auto srcBuffer = device->createBuffer({.label = "MultiRegionNoStorageSrc", .usage = usage, .size = 16u});
        auto dstBuffer = device->createBuffer({
            .label = "MultiRegionNoStorageDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 16u,
        });
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionNoStorageDescriptor",
            .usage = usage,
            .size = sizeof(GVM::RHI::BufferCopyRegion),
        });
        ASSERT_FALSE(srcBuffer.isNull());
        ASSERT_FALSE(dstBuffer.isNull());
        ASSERT_FALSE(regionBuffer.isNull());

        EXPECT_THROW(device->getMainQueue()->copyBufferToBufferMultipleRegion(srcBuffer, dstBuffer, regionBuffer, 1u), std::invalid_argument);

        device->freeBuffer(regionBuffer);
        device->freeBuffer(dstBuffer);
        device->freeBuffer(srcBuffer);
    }

    /** Verifies that a rejected multi-region copy leaves no queue state behind that disturbs later queued writes. */
    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionRejectedCallLeavesQueueUsable)
    {
        const auto usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc |
            GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite;
        auto srcBuffer = device->createBuffer({.label = "MultiRegionRejectSrc", .usage = usage, .size = 16u});
        auto rejectedDstBuffer = device->createBuffer({
            .label = "MultiRegionRejectDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 16u,
        });
        auto writeDstBuffer = device->createBuffer({
            .label = "MultiRegionRejectWriteDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 16u,
        });
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionRejectDescriptor",
            .usage = usage,
            .size = sizeof(GVM::RHI::BufferCopyRegion),
        });
        ASSERT_FALSE(srcBuffer.isNull());
        ASSERT_FALSE(rejectedDstBuffer.isNull());
        ASSERT_FALSE(writeDstBuffer.isNull());
        ASSERT_FALSE(regionBuffer.isNull());

        auto *queue = device->getMainQueue();
        EXPECT_THROW(queue->copyBufferToBufferMultipleRegion(srcBuffer, rejectedDstBuffer, regionBuffer, 1u), std::invalid_argument);

        const std::array<uint32_t, 4> writeWords = {11u, 22u, 33u, 44u};
        queue->writeBuffer(GVM::RHI::BufferRange{writeDstBuffer, 0u, 16u}, writeWords.data(), writeWords.size() * sizeof(uint32_t));

        auto commandEncoder = queue->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));
        commandEncoder->end();
        eastl::vector<GVM::RHI::CommandEncoder> encoders(1u);
        encoders[0u] = commandEncoder;
        EXPECT_NO_THROW(submitAndWait(encoders));

        writeDstBuffer->map();
        const auto *resultWords = static_cast<const uint32_t *>(writeDstBuffer->getConstMappedRange(0u, 16u));
        ASSERT_NE(resultWords, nullptr);
        for (uint32_t word = 0u; word < 4u; ++word)
        {
            EXPECT_EQ(resultWords[word], writeWords[word]);
        }
        writeDstBuffer->unmap();

        device->freeBuffer(regionBuffer);
        device->freeBuffer(writeDstBuffer);
        device->freeBuffer(rejectedDstBuffer);
        device->freeBuffer(srcBuffer);
    }

    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionRejectsNonAlignedOffsets)
    {
        auto srcBuffer = device->createBuffer({
            .label = "MultiRegionByteFallbackSrc",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 24,
        });
        auto dstBuffer = device->createBuffer({
            .label = "MultiRegionByteFallbackDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 24,
        });
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionByteFallbackDescriptor",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = sizeof(GVM::RHI::BufferCopyRegion),
        });

        ASSERT_FALSE(srcBuffer.isNull());
        ASSERT_FALSE(dstBuffer.isNull());
        ASSERT_FALSE(regionBuffer.isNull());

        std::array<std::uint8_t, 24> srcBytes = {};
        std::array<std::uint8_t, 24> dstBytes = {};
        for (std::size_t index = 0; index < srcBytes.size(); ++index)
        {
            srcBytes[index] = static_cast<std::uint8_t>(0x40u + index);
            dstBytes[index] = static_cast<std::uint8_t>(0xa0u + index);
        }

        srcBuffer->map();
        dstBuffer->map();
        auto *srcMapped = static_cast<std::uint8_t *>(srcBuffer->getMappedRange(0, srcBytes.size()));
        auto *dstMapped = static_cast<std::uint8_t *>(dstBuffer->getMappedRange(0, dstBytes.size()));
        ASSERT_NE(srcMapped, nullptr);
        ASSERT_NE(dstMapped, nullptr);
        std::memcpy(srcMapped, srcBytes.data(), srcBytes.size());
        std::memcpy(dstMapped, dstBytes.data(), dstBytes.size());
        srcBuffer->unmap();
        dstBuffer->unmap();

        regionBuffer->map();
        const GVM::RHI::BufferCopyRegion region = {
            .srcOffset = 1,
            .dstOffset = 2,
            .size = 8,
        };
        auto *regionMapped = static_cast<GVM::RHI::BufferCopyRegion *>(regionBuffer->getMappedRange(0, sizeof(region)));
        ASSERT_NE(regionMapped, nullptr);
        *regionMapped = region;
        regionBuffer->unmap();

        EXPECT_THROW(device->getMainQueue()->copyBufferToBufferMultipleRegion(srcBuffer, dstBuffer, regionBuffer, 1), std::invalid_argument);

        device->freeBuffer(regionBuffer);
        device->freeBuffer(dstBuffer);
        device->freeBuffer(srcBuffer);
    }

    TEST_F(RhiExecutionTest, RepeatedResourceCreationRoundTripDoesNotCrash)
    {
        for (uint32_t iteration = 0; iteration < 64; ++iteration)
        {
            SCOPED_TRACE(iteration);

            auto buffer = device->createBuffer({
                .label = "ChurnBuffer",
                .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead,
                .size = 256,
            });
            auto texture = device->createTexture({
                .label = "ChurnTexture",
                .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
                .dimension = GVM::RHI::TextureDimension::e2D,
                .size = {8, 8, 1},
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .mipLevelCount = 1,
                .arrayLayerCount = 1,
            });
            auto sampler = device->createSampler({
                .label = "ChurnSampler",
                .addressModeU = GVM::RHI::AddressMode::ClampToEdge,
                .addressModeV = GVM::RHI::AddressMode::ClampToEdge,
                .addressModeW = GVM::RHI::AddressMode::ClampToEdge,
                .magFilter = GVM::RHI::FilterMode::Nearest,
                .minFilter = GVM::RHI::FilterMode::Nearest,
                .mipmapFilter = GVM::RHI::MipmapFilterMode::Nearest,
                .lodMinClamp = 0.0f,
                .lodMaxClamp = 1.0f,
                .compare = GVM::RHI::CompareFunction::Always,
                .maxAnisotropy = 1,
            });

            ASSERT_FALSE(buffer.isNull());
            ASSERT_FALSE(texture.isNull());
            ASSERT_FALSE(sampler.isNull());

            device->freeSampler(sampler);
            device->freeTexture(texture);
            device->freeBuffer(buffer);
        }
    }

    TEST_F(RhiExecutionTest, QueueReadBufferCopiesFromMappedSourceWhenMapReadIsAvailable)
    {
        const std::array<uint32_t, 4> expected = {11u, 22u, 33u, 44u};
        std::array<uint32_t, 4> actual = {};

        auto buffer = device->createBuffer({
            .label = "QueueMappedReadbackBuffer",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = sizeof(expected),
        });
        ASSERT_FALSE(buffer.isNull());

        auto *queue = device->getMainQueue();
        ASSERT_NE(queue, nullptr);
        queue->writeBuffer(GVM::RHI::BufferRange(buffer), expected.data(), sizeof(expected));
        queue->readBuffer(GVM::RHI::BufferRange(buffer), actual.data(), sizeof(actual));

        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        queue->submit(encoders);

        EXPECT_EQ(actual, expected);
        device->freeBuffer(buffer);
    }

    /// Verifies a direct-mapped queue readback on a queue with no prior submission resolves during an empty submit.
    TEST_F(RhiExecutionTest, QueueReadBufferResolvesDirectMappedSourceOnFirstEmptySubmit)
    {
        const std::array<uint32_t, 4> expected = {7u, 17u, 27u, 37u};
        std::array<uint32_t, 4> actual = {};

        auto buffer = device->createBuffer({
            .label = "FirstSubmitMappedReadbackBuffer",
            .usage = GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = sizeof(expected),
        });
        ASSERT_FALSE(buffer.isNull());

        buffer->map();
        auto *mapped = static_cast<uint32_t *>(buffer->getMappedRange(0, sizeof(expected)));
        ASSERT_NE(mapped, nullptr);
        memcpy(mapped, expected.data(), sizeof(expected));
        buffer->unmap();

        auto *queue = device->getMainQueue();
        ASSERT_NE(queue, nullptr);
        queue->readBuffer(GVM::RHI::BufferRange(buffer), actual.data(), sizeof(actual));

        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        queue->submit(encoders);

        EXPECT_EQ(actual, expected);
        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, QueueReadBufferStagesCopySrcOnlySourceIntoCpuMemory)
    {
        const std::array<uint32_t, 4> expected = {101u, 202u, 303u, 404u};
        std::array<uint32_t, 4> actual = {};

        auto buffer = device->createBuffer({
            .label = "QueueStagedReadbackBuffer",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(expected),
        });
        ASSERT_FALSE(buffer.isNull());

        auto *queue = device->getMainQueue();
        ASSERT_NE(queue, nullptr);
        queue->writeBuffer(GVM::RHI::BufferRange(buffer), expected.data(), sizeof(expected));
        queue->readBuffer(GVM::RHI::BufferRange(buffer), actual.data(), sizeof(actual));

        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        queue->submit(encoders);

        EXPECT_EQ(actual, expected);
        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, RepeatedBlockingReadbacksReuseUploadStorageWithoutPresenting)
    {
        constexpr uint32_t Iterations = 128u;
        std::array<uint32_t, 64> expected = {};
        std::array<uint32_t, 64> actual = {};
        auto buffer = device->createBuffer({
            .label = "HeadlessRepeatedUpload",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(expected),
        });
        ASSERT_FALSE(buffer.isNull());
        auto *queue = device->getMainQueue();
        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        uint64_t initialResourceBytes = 0;
        for (uint32_t iteration = 0; iteration < Iterations; ++iteration)
        {
            expected.fill(0x13570000u + iteration);
            actual.fill(0u);
            queue->writeBuffer(GVM::RHI::BufferRange(buffer), expected.data(), sizeof(expected));
            queue->submit(encoders);
            queue->readBuffer(GVM::RHI::BufferRange(buffer), actual.data(), sizeof(actual));
            queue->submit(encoders);
            ASSERT_EQ(actual, expected) << "iteration=" << iteration;
            if (iteration == 0) { initialResourceBytes = device->getDiagnosticsResourceSnapshot().totalEstimatedBytes; }
        }
        EXPECT_LE(device->getDiagnosticsResourceSnapshot().totalEstimatedBytes, initialResourceBytes);
        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, MetalQueuePreservesSequentialUploadStagingWithinOneFrame)
    {
        if (instance == nullptr || instance->getBackend() != GVM::RHI::GraphicsBackend::Metal)
        {
            GTEST_SKIP() << "This regression covers Metal upload staging lifetime only, got backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }

        constexpr uint32_t UploadCount = 16u;
        constexpr uint32_t ValuesPerUpload = 65536u;
        const uint64_t uploadBytes = uint64_t(ValuesPerUpload) * sizeof(uint32_t);
        eastl::vector<GVM::RHI::Buffer> buffers;
        eastl::vector<eastl::vector<uint32_t>> expectedValues;
        eastl::vector<eastl::vector<uint32_t>> actualValues;
        buffers.reserve(UploadCount);
        expectedValues.reserve(UploadCount);
        actualValues.reserve(UploadCount);

        auto *queue = device->getMainQueue();
        ASSERT_NE(queue, nullptr);
        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        for (uint32_t uploadIndex = 0u; uploadIndex < UploadCount; ++uploadIndex)
        {
            buffers.push_back(device->createBuffer({
                .label = "SequentialMetalUploadBuffer",
                .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
                .size = uploadBytes,
            }));
            ASSERT_FALSE(buffers.back().isNull());
            expectedValues.emplace_back(ValuesPerUpload, 0x13570000u + uploadIndex);
            actualValues.emplace_back(ValuesPerUpload, 0u);
            queue->writeBuffer(
                GVM::RHI::BufferRange(buffers.back()),
                expectedValues.back().data(),
                uploadBytes);
            queue->submit(encoders);
        }

        for (uint32_t uploadIndex = 0u; uploadIndex < UploadCount; ++uploadIndex)
        {
            queue->readBuffer(
                GVM::RHI::BufferRange(buffers[uploadIndex]),
                actualValues[uploadIndex].data(),
                uploadBytes);
        }
        queue->submit(encoders);

        for (uint32_t uploadIndex = 0u; uploadIndex < UploadCount; ++uploadIndex)
        {
            EXPECT_EQ(actualValues[uploadIndex], expectedValues[uploadIndex]);
            device->freeBuffer(buffers[uploadIndex]);
        }
    }

    TEST_F(RhiExecutionTest, VulkanQueueReadbackPreservesProducerForLaterReaderStages)
    {
        if (instance == nullptr || instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
        {
            GTEST_SKIP() << "This regression covers Vulkan buffer state tracking only, got backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }

        const std::array<uint32_t, 4> expected = {901u, 902u, 903u, 904u};
        std::array<uint32_t, 4> firstReadback = {};
        std::array<uint32_t, 4> secondReadback = {};

        auto buffer = device->createBuffer({
            .label = "OutstandingWriteReadbackBuffer",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(expected),
        });
        ASSERT_FALSE(buffer.isNull());

        auto *queue = device->getMainQueue();
        ASSERT_NE(queue, nullptr);

        eastl::vector<GVM::RHI::CommandEncoder> encoders;

        queue->writeBuffer(GVM::RHI::BufferRange(buffer), expected.data(), sizeof(expected));
        queue->submit(encoders);

        const auto stateAfterWrite = GVM::RHI::Vulkan::Testing::getCurrentBufferState(queue, buffer);
        EXPECT_TRUE(stateAfterWrite.initialized);
        EXPECT_TRUE(stateAfterWrite.lastWriteInitialized);
        EXPECT_NE(stateAfterWrite.lastWriteAccessMaskBits, 0u);

        queue->readBuffer(GVM::RHI::BufferRange(buffer), firstReadback.data(), sizeof(firstReadback));
        queue->submit(encoders);
        EXPECT_EQ(firstReadback, expected);

        const auto stateAfterFirstRead = GVM::RHI::Vulkan::Testing::getCurrentBufferState(queue, buffer);
        EXPECT_TRUE(stateAfterFirstRead.initialized);
        EXPECT_TRUE(stateAfterFirstRead.lastWriteInitialized);
        EXPECT_EQ(stateAfterFirstRead.lastWriteStageMaskBits, stateAfterWrite.lastWriteStageMaskBits);
        EXPECT_EQ(stateAfterFirstRead.lastWriteAccessMaskBits, stateAfterWrite.lastWriteAccessMaskBits);

        queue->readBuffer(GVM::RHI::BufferRange(buffer), secondReadback.data(), sizeof(secondReadback));
        queue->submit(encoders);
        EXPECT_EQ(secondReadback, expected);

        const auto stateAfterSecondRead = GVM::RHI::Vulkan::Testing::getCurrentBufferState(queue, buffer);
        EXPECT_TRUE(stateAfterSecondRead.initialized);
        EXPECT_TRUE(stateAfterSecondRead.lastWriteInitialized);
        EXPECT_EQ(stateAfterSecondRead.lastWriteStageMaskBits, stateAfterWrite.lastWriteStageMaskBits);
        EXPECT_EQ(stateAfterSecondRead.lastWriteAccessMaskBits, stateAfterWrite.lastWriteAccessMaskBits);

        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, SubmittedOrderOverridesRecordedOrderAndPreservesDisjointRanges)
    {
        auto buffer = device->createBuffer({.label = "ReorderedRanges",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst, .size = 16});
        auto *queue = device->getMainQueue();
        auto first = queue->createCommandEncoder();
        auto pass = first->beginBlitPass({});
        pass->fillBuffer(GVM::RHI::BufferRange(buffer, 0, 8), 17u); pass->end(); first->end();
        auto second = queue->createCommandEncoder();
        pass = second->beginBlitPass({});
        pass->fillBuffer(GVM::RHI::BufferRange(buffer), 29u); pass->end(); second->end();
        queue->submit({second, first});
        std::array<uint32_t, 4> values = {};
        queue->readBuffer(GVM::RHI::BufferRange(buffer), values.data(), sizeof(values)); queue->submit({});
        const uint32_t firstValue = 17u * 0x01010101u;
        const uint32_t secondValue = 29u * 0x01010101u;
        EXPECT_EQ(values, (std::array<uint32_t, 4>{firstValue, firstValue, secondValue, secondValue}));
        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, FillBufferPreservesBackendPatternAndRangeBoundaries)
    {
        constexpr size_t wordCount = 1029u;
        const std::array<uint32_t, 4> patterns = {0u, 0xffu, 0x12u, 0x7fu};
        auto buffer = device->createBuffer({.label = "FillPatternRange",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = wordCount * sizeof(uint32_t)});
        auto *queue = device->getMainQueue();
        for (uint32_t pattern : patterns)
        {
            SCOPED_TRACE(pattern);
            std::array<uint32_t, wordCount> values;
            values.fill(0xabcdef01u);
            queue->writeBuffer(GVM::RHI::BufferRange(buffer), values.data(), sizeof(values));
            auto encoder = queue->createCommandEncoder();
            auto pass = encoder->beginBlitPass({});
            pass->fillBuffer(GVM::RHI::BufferRange(buffer, 4u, sizeof(values) - 8u), pattern);
            pass->end();
            encoder->end();
            queue->submit({encoder});
            queue->readBuffer(GVM::RHI::BufferRange(buffer), values.data(), sizeof(values));
            queue->submit({});
            EXPECT_EQ(values.front(), 0xabcdef01u);
            EXPECT_EQ(values.back(), 0xabcdef01u);
            const uint32_t expected = pattern * 0x01010101u;
            for (size_t index = 1u; index + 1u < wordCount; ++index)
                ASSERT_EQ(values[index], expected) << "word=" << index;
        }
        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, FillBufferAcceptsByteValueOnBothBackends)
    {
        auto buffer = device->createBuffer({.label = "FillByteRange",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst, .size = 16});
        auto *queue = device->getMainQueue();
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginBlitPass({});
        pass->fillBuffer(GVM::RHI::BufferRange(buffer), 0xffu);
        pass->end();
        encoder->end();
        queue->submit({encoder});
        std::array<uint32_t, 4> values = {};
        queue->readBuffer(GVM::RHI::BufferRange(buffer), values.data(), sizeof(values));
        queue->submit({});
        EXPECT_EQ(values, (std::array<uint32_t, 4>{0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}));
        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, AbandonedEncoderDoesNotPublishBufferStateOrWrites)
    {
        auto buffer = device->createBuffer({.label = "AbandonedEncoderBuffer",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst, .size = 16});
        auto *queue = device->getMainQueue();
        {
            auto encoder = queue->createCommandEncoder();
            auto pass = encoder->beginBlitPass({});
            pass->fillBuffer(GVM::RHI::BufferRange(buffer), 91u); pass->end(); encoder->end();
        }
        if (instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan)
            EXPECT_FALSE(GVM::RHI::Vulkan::Testing::getCurrentBufferState(queue, buffer).initialized);
        const std::array<uint32_t, 4> expected = {1, 2, 3, 4};
        std::array<uint32_t, 4> values = {};
        queue->writeBuffer(GVM::RHI::BufferRange(buffer), expected.data(), sizeof(expected));
        queue->readBuffer(GVM::RHI::BufferRange(buffer), values.data(), sizeof(values)); queue->submit({});
        EXPECT_EQ(values, expected);
        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, CoreQueueReadTextureMatchesCoreQueueWriteTextureForMipReadback)
    {
        GVM::Core::DeviceProxy coreDevice(device);

        auto texture = device->createTexture({
            .label = "CoreReadbackTexture",
            .usage = GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {4, 4, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 2,
            .arrayLayerCount = 1,
        });
        ASSERT_FALSE(texture.isNull());

        const std::array<std::uint8_t, 16> mipLevelOneBytes = {
            0x10, 0x20, 0x30, 0x40,
            0x50, 0x60, 0x70, 0x80,
            0x90, 0xa0, 0xb0, 0xc0,
            0xd0, 0xe0, 0xf0, 0xff,
        };
        std::array<std::uint8_t, mipLevelOneBytes.size()> readbackBytes = {};

        auto queue = coreDevice->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->writeTexture(texture, mipLevelOneBytes.data(), mipLevelOneBytes.size(), 1u)
            ->readTexture(texture, readbackBytes.data(), readbackBytes.size(), 1u)
            ->submit();

        EXPECT_EQ(readbackBytes, mipLevelOneBytes);
        device->freeTexture(texture);
    }

    TEST_F(RhiExecutionTest, CoreQueueReadTextureMatchesCoreQueueWriteTextureFor3DVolume)
    {
        GVM::Core::DeviceProxy coreDevice(device);

        auto texture = device->createTexture({
            .label = "CoreReadbackTexture3D",
            .usage = GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc,
            .dimension = GVM::RHI::TextureDimension::e3D,
            .size = {4, 4, 3},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
        });
        ASSERT_FALSE(texture.isNull());

        std::vector<std::uint8_t> volumeBytes(4u * 4u * 3u * 4u);
        for (std::size_t index = 0; index < volumeBytes.size(); ++index)
        {
            volumeBytes[index] = static_cast<std::uint8_t>((index * 17u + 3u) & 0xffu);
        }
        std::vector<std::uint8_t> readbackBytes(volumeBytes.size(), 0u);

        auto queue = coreDevice->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->writeTexture(texture, volumeBytes.data(), volumeBytes.size())
            ->readTexture(texture, readbackBytes.data(), readbackBytes.size())
            ->submit();

        EXPECT_EQ(readbackBytes, volumeBytes);
        device->freeTexture(texture);
    }
    /** Rejects oversized region counts before inspecting descriptors beyond their allocation. */
    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionRejectsDescriptorCountBeyondCapacity)
    {
        using namespace GVM::RHI;
        const auto usage = BufferUsage::Storage | BufferUsage::MapWrite | BufferUsage::CopySrc | BufferUsage::CopyDst;
        auto source = device->createBuffer({.usage = usage, .size = 16u});
        auto destination = device->createBuffer({.usage = usage, .size = 16u});
        auto regions = device->createBuffer({.usage = usage, .size = sizeof(BufferCopyRegion)});
        EXPECT_THROW(device->getMainQueue()->copyBufferToBufferMultipleRegion(
            source, destination, regions, ~uint32_t(0)), std::exception);
        device->freeBuffer(regions);
        device->freeBuffer(destination);
        device->freeBuffer(source);
    }

    /** Rejects explicit mip and layer counts that would wrap the end of a texture view. */
    TEST_F(RhiExecutionTest, TextureViewsRejectWrappedMipAndLayerCounts)
    {
        using namespace GVM::RHI;
        auto texture = device->createTexture({.size = {8u, 8u, 1u}, .arrayLayerCount = 2u,
            .mipLevelCount = 2u, .dimension = TextureDimension::e2D,
            .format = TextureFormat::RGBA8Unorm, .usage = TextureUsage::TextureBinding});
        TextureViewDescriptor view;
        view.baseMipLevel = 1u;
        view.mipLevelCount = ~uint32_t(0);
        EXPECT_THROW(texture->createView(view), std::out_of_range);
        view.baseMipLevel = 0u;
        view.mipLevelCount = 1u;
        view.baseArrayLayer = 1u;
        view.arrayLayerCount = ~uint32_t(0);
        EXPECT_THROW(texture->createView(view), std::out_of_range);
        device->freeTexture(texture);
    }

    /** Rejects view dimensions that cannot be created from the texture shape on both backends. */
    TEST_F(RhiExecutionTest, TextureViewsRejectIncompatibleDimensions)
    {
        using namespace GVM::RHI;
        auto volume = device->createTexture({.size = {8u, 8u, 8u}, .arrayLayerCount = 1u,
            .mipLevelCount = 1u, .dimension = TextureDimension::e3D,
            .format = TextureFormat::RGBA8Unorm, .usage = TextureUsage::TextureBinding});
        TextureViewDescriptor planarView;
        planarView.dimension = TextureViewDimension::e2D;
        EXPECT_THROW(volume->createView(planarView), std::invalid_argument);
        device->freeTexture(volume);

        auto layers = device->createTexture({.size = {8u, 8u, 1u}, .arrayLayerCount = 4u,
            .mipLevelCount = 1u, .dimension = TextureDimension::e2D,
            .format = TextureFormat::RGBA8Unorm, .usage = TextureUsage::TextureBinding});
        TextureViewDescriptor cubeView;
        cubeView.dimension = TextureViewDimension::Cube;
        cubeView.arrayLayerCount = 4u;
        EXPECT_THROW(layers->createView(cubeView), std::invalid_argument);
        device->freeTexture(layers);
    }

    /** Round-trips a compressed 2D-array upload whose buffer layout spaces layers farther apart than the image rows. */
    TEST_F(RhiExecutionTest, WriteThenReadCompressedArrayTextureWithExplicitRowsPerImage)
    {
        using namespace GVM::RHI;
        Texture texture;
        try
        {
            texture = device->createTexture({
                .label = "BC1ArrayFootprint",
                .usage = TextureUsage::CopyDst | TextureUsage::CopySrc,
                .dimension = TextureDimension::e2D,
                .size = {8u, 8u, 1u},
                .format = TextureFormat::BC1RGBAUnorm,
                .mipLevelCount = 1u,
                .arrayLayerCount = 2u,
            });
        }
        catch (const std::exception &)
        {
            GTEST_SKIP() << "The device does not support BC1 texture copies.";
        }
        if (texture.isNull())
        {
            GTEST_SKIP() << "The device does not support BC1 texture copies.";
        }

        // A BC1 8x8 layer holds 2x2 blocks; the explicit layout spaces layers four block rows apart.
        constexpr uint32_t bytesPerRow = 16u;
        constexpr uint32_t rowsPerImage = 4u;
        constexpr uint64_t bytesPerImage = uint64_t(bytesPerRow) * rowsPerImage;
        constexpr uint32_t layerPayloadBytes = 32u;
        std::array<uint8_t, 2u * bytesPerImage> uploadBytes = {};
        for (uint32_t index = 0; index < uploadBytes.size(); ++index)
        {
            uploadBytes[index] = static_cast<uint8_t>((index * 31u + 7u) & 0xffu);
        }
        std::array<uint8_t, uploadBytes.size()> readbackBytes = {};

        const TextureDataLayout layout = {
            .offset = 0u,
            .bytesPerRow = bytesPerRow,
            .rowsPerImage = rowsPerImage,
        };
        const ImageCopyTexture target = {.texture = texture};
        const Extent3D extent = {8u, 8u, 2u};

        auto *queue = device->getMainQueue();
        queue->writeTexture(target, uploadBytes.data(), uploadBytes.size(), layout, extent);
        queue->readTexture(target, readbackBytes.data(), readbackBytes.size(), layout, extent);
        queue->submit({});

        for (uint32_t layer = 0; layer < 2u; ++layer)
        {
            SCOPED_TRACE(layer);
            EXPECT_EQ(0, std::memcmp(
                readbackBytes.data() + layer * bytesPerImage,
                uploadBytes.data() + layer * bytesPerImage,
                layerPayloadBytes));
        }
        device->freeTexture(texture);
    }

    /// Provides a one-color-attachment framebuffer for QueueProxy ordering render tests.
    struct QueueOrderingRenderTarget
    {
        GVM::RHI::TextureView colorView;

        /// Builds a render pass descriptor that clears the single color attachment.
        GVM::RHI::RenderPassDescriptor getRenderPassDescriptor() const
        {
            GVM::RHI::RenderPassDescriptor descriptor = {};
            descriptor.colorAttachments.push_back({
                .view = colorView,
                .loadOp = GVM::RHI::LoadOp::Clear,
                .storeOp = GVM::RHI::StoreOp::Store,
                .clearValue = {0.0, 0.0, 0.0, 1.0},
            });
            return descriptor;
        }
    };

    /** Verifies that a QueueProxy write queued after a recorded pass still executes before that pass at submit. */
    TEST_F(RhiExecutionTest, HoistsQueueWritesAheadOfRecordedPasses)
    {
        GVM::Core::DeviceProxy coreDevice(device);
        auto queue = coreDevice->graphicsQueue(0);
        constexpr auto usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc;
        auto source = device->createBuffer({
            .label = "QueueProxyHoistedWriteSource",
            .usage = usage,
            .size = sizeof(uint32_t),
        });
        auto destination = device->createBuffer({
            .label = "QueueProxyHoistedWriteDestination",
            .usage = usage,
            .size = sizeof(uint32_t),
        });
        ASSERT_FALSE(source.isNull());
        ASSERT_FALSE(destination.isNull());

        const uint32_t initialValue = 0u;
        const uint32_t hoistedValue = 0xdeadbeefu;
        queue->writeBuffer(GVM::RHI::BufferRange(source), &initialValue, sizeof(initialValue));
        queue->submit();

        uint32_t copied = 0u;
        queue->blitPass("CopyAfterQueuedWrite", GVM::Core::copyBufferToBuffer(source, 0, destination, 0, sizeof(uint32_t)))
            ->writeBuffer(GVM::RHI::BufferRange(source), &hoistedValue, sizeof(hoistedValue))
            ->submit();

        queue->readBuffer(GVM::RHI::BufferRange(destination), &copied, sizeof(copied));
        queue->submit();
        EXPECT_EQ(copied, hoistedValue);
        device->freeBuffer(destination);
        device->freeBuffer(source);
    }

    /** Verifies that queued QueueProxy writes execute in call order among themselves. */
    TEST_F(RhiExecutionTest, KeepsQueueWritesInCallOrder)
    {
        GVM::Core::DeviceProxy coreDevice(device);
        auto queue = coreDevice->graphicsQueue(0);
        constexpr auto usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc;
        auto buffer = device->createBuffer({
            .label = "QueueProxyWriteOrderBuffer",
            .usage = usage,
            .size = 4u * sizeof(uint32_t),
        });
        ASSERT_FALSE(buffer.isNull());
        auto texture = device->createTexture({
            .label = "QueueProxyWriteOrderTexture",
            .usage = GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {2, 2, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
        });
        ASSERT_FALSE(texture.isNull());

        // Metal fills bytes while Vulkan fills 32-bit words; keep the pattern a single byte.
        const bool metal = instance->getBackend() == GVM::RHI::GraphicsBackend::Metal;
        const uint32_t fillPattern = 0x5au;
        const uint32_t expectedFillWord = fillPattern * 0x01010101u;
        const std::array<uint32_t, 4> firstValues = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
        const uint32_t lastValue = 0xaaaaaaaau;
        const std::array<uint32_t, 4> expectedValues = {firstValues[0], expectedFillWord, lastValue, firstValues[3]};
        const std::array<std::uint8_t, 16> firstPixels = {
            0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
            0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        };
        const std::array<std::uint8_t, 16> secondPixels = {
            0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
            0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
        };

        queue->writeBuffer(GVM::RHI::BufferRange(buffer), firstValues.data(), sizeof(firstValues))
            ->fillBuffer(GVM::RHI::BufferRange(buffer, sizeof(uint32_t), 2u * sizeof(uint32_t)), fillPattern)
            ->writeBuffer(GVM::RHI::BufferRange(buffer, 2u * sizeof(uint32_t), sizeof(uint32_t)), &lastValue, sizeof(lastValue))
            ->writeTexture(texture, firstPixels.data(), firstPixels.size())
            ->writeTexture(texture, secondPixels.data(), secondPixels.size())
            ->submit();

        std::array<uint32_t, 4> bufferReadback = {};
        std::array<std::uint8_t, 16> textureReadback = {};
        queue->readBuffer(GVM::RHI::BufferRange(buffer), bufferReadback.data(), sizeof(bufferReadback))
            ->readTexture(texture, textureReadback.data(), textureReadback.size())
            ->submit();

        EXPECT_EQ(bufferReadback, expectedValues);
        EXPECT_EQ(textureReadback, secondPixels);
        device->freeTexture(texture);
        device->freeBuffer(buffer);
    }

    /** Verifies that a QueueProxy read queued before submit() observes the recorded passes once the submission completes. */
    TEST_F(RhiExecutionTest, ResolvesQueueReadsAfterSubmit)
    {
        GVM::Core::DeviceProxy coreDevice(device);
        auto queue = coreDevice->graphicsQueue(0);
        auto source = device->createBuffer({
            .label = "QueueProxyReadSource",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
            .size = sizeof(uint32_t),
        });
        ASSERT_FALSE(source.isNull());

        for (const GVM::RHI::BufferUsageFlags destinationUsage : {
                 GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead,
                 GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
             })
        {
            auto destination = device->createBuffer({
                .label = "QueueProxyReadDestination",
                .usage = destinationUsage,
                .size = sizeof(uint32_t),
            });
            ASSERT_FALSE(destination.isNull());

            const uint32_t initialValue = 0u;
            const uint32_t passValue = 0x7f7f7f7fu;
            queue->writeBuffer(GVM::RHI::BufferRange(source), &passValue, sizeof(passValue));
            queue->writeBuffer(GVM::RHI::BufferRange(destination), &initialValue, sizeof(initialValue));
            queue->submit();

            uint32_t readback = 0u;
            queue->blitPass("CopyPassResult", GVM::Core::copyBufferToBuffer(source, 0, destination, 0, sizeof(uint32_t)))
                ->readBuffer(GVM::RHI::BufferRange(destination), &readback, sizeof(readback))
                ->submit();

            EXPECT_EQ(readback, passValue);
            device->freeBuffer(destination);
        }
        device->freeBuffer(source);
    }

    /** Verifies that a QueueProxy texture read queued before submit() observes the recorded pass output. */
    TEST_F(RhiExecutionTest, ResolvesQueueTextureReadsAfterSubmit)
    {
        GVM::Core::DeviceProxy coreDevice(device);
        auto queue = coreDevice->graphicsQueue(0);
        auto source = device->createBuffer({
            .label = "QueueProxyTextureReadSource",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
            .size = 16u,
        });
        auto texture = device->createTexture({
            .label = "QueueProxyTextureReadTarget",
            .usage = GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::CopySrc,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {2, 2, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
        });
        ASSERT_FALSE(source.isNull());
        ASSERT_FALSE(texture.isNull());

        const std::array<std::uint8_t, 16> oldPixels = {
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        };
        const std::array<std::uint8_t, 16> passPixels = {
            0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
            0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
        };
        queue->writeTexture(texture, oldPixels.data(), oldPixels.size());
        queue->submit();

        std::array<std::uint8_t, 16> readback = {};
        queue->writeBuffer(GVM::RHI::BufferRange(source), passPixels.data(), passPixels.size());
        GVM::Core::BlitPassTaskDescriptor copyIntoTexture;
        copyIntoTexture.blitFn = [source, texture](GVM::RHI::BlitPassEncoder encoder)
        {
            encoder->copyBufferToTexture(
                {.layout = {}, .buffer = source},
                {.texture = texture},
                {2, 2, 1});
        };
        queue->blitPass("CopyIntoTexture", eastl::move(copyIntoTexture))
            ->readTexture(texture, readback.data(), readback.size())
            ->submit();

        EXPECT_EQ(readback, passPixels);
        device->freeTexture(texture);
        device->freeBuffer(source);
    }

    /** Verifies that queue writes are hoisted ahead of queue reads regardless of call order. */
    TEST_F(RhiExecutionTest, HoistsQueueWritesAheadOfQueuedReads)
    {
        GVM::Core::DeviceProxy coreDevice(device);
        auto queue = coreDevice->graphicsQueue(0);
        auto buffer = device->createBuffer({
            .label = "QueueProxyReadBeforeWriteBuffer",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
            .size = sizeof(uint32_t),
        });
        ASSERT_FALSE(buffer.isNull());

        const uint32_t initialValue = 0u;
        const uint32_t hoistedValue = 0xc0ffee11u;
        queue->writeBuffer(GVM::RHI::BufferRange(buffer), &initialValue, sizeof(initialValue));
        queue->submit();

        uint32_t readback = 0u;
        queue->readBuffer(GVM::RHI::BufferRange(buffer), &readback, sizeof(readback))
            ->writeBuffer(GVM::RHI::BufferRange(buffer), &hoistedValue, sizeof(hoistedValue))
            ->submit();

        EXPECT_EQ(readback, hoistedValue);
        device->freeBuffer(buffer);
    }

    TEST_F(RhiExecutionTest, DiscardsPendingPassesWhenBlitRecordingThrows)
    {
        GVM::Core::DeviceProxy coreDevice(device);
        auto queue = coreDevice->graphicsQueue(0);
        constexpr auto usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc;
        auto source = device->createBuffer({.label = "QueueProxyDiscardBlitSource", .usage = usage, .size = sizeof(uint32_t)});
        auto destination = device->createBuffer({.label = "QueueProxyDiscardBlitDestination", .usage = usage, .size = sizeof(uint32_t)});
        ASSERT_FALSE(source.isNull());
        ASSERT_FALSE(destination.isNull());

        const uint32_t sentinel = 0xdeadbeefu;
        const uint32_t value = 0x12345678u;
        uint32_t readback = 0u;

        queue->writeBuffer(GVM::RHI::BufferRange(destination), &sentinel, sizeof(sentinel))
            ->writeBuffer(GVM::RHI::BufferRange(source), &value, sizeof(value))
            ->blitPass("DiscardedCopy", GVM::Core::copyBufferToBuffer(source, 0, destination, 0, sizeof(uint32_t)));

        EXPECT_THROW(
            queue->blitPass(
                "ThrowingBlitPass",
                GVM::Core::BlitPassTaskDescriptor{.blitFn = [](GVM::RHI::BlitPassEncoder) {
                    throw std::runtime_error("QueueProxy test blit failure");
                }}),
            std::runtime_error);

        queue->submit();
        queue->readBuffer(GVM::RHI::BufferRange(destination), &readback, sizeof(readback))->submit();
        EXPECT_EQ(readback, sentinel);

        queue->blitPass("RecoveredCopy", GVM::Core::copyBufferToBuffer(source, 0, destination, 0, sizeof(uint32_t)))->submit();
        queue->readBuffer(GVM::RHI::BufferRange(destination), &readback, sizeof(readback))->submit();
        EXPECT_EQ(readback, value);
        device->freeBuffer(destination);
        device->freeBuffer(source);
    }

    TEST_F(RhiExecutionTest, DiscardsPendingPassesWhenComputeRecordingThrows)
    {
        GVM::Core::DeviceProxy coreDevice(device);
        auto queue = coreDevice->graphicsQueue(0);
        constexpr auto usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc;
        auto source = device->createBuffer({.label = "QueueProxyDiscardComputeSource", .usage = usage, .size = sizeof(uint32_t)});
        auto destination = device->createBuffer({.label = "QueueProxyDiscardComputeDestination", .usage = usage, .size = sizeof(uint32_t)});
        ASSERT_FALSE(source.isNull());
        ASSERT_FALSE(destination.isNull());

        const uint32_t sentinel = 0xdeadbeefu;
        const uint32_t value = 0x12345678u;
        uint32_t readback = 0u;

        queue->writeBuffer(GVM::RHI::BufferRange(destination), &sentinel, sizeof(sentinel))
            ->writeBuffer(GVM::RHI::BufferRange(source), &value, sizeof(value))
            ->blitPass("DiscardedCopy", GVM::Core::copyBufferToBuffer(source, 0, destination, 0, sizeof(uint32_t)));

        EXPECT_THROW(
            queue->computePass(
                "ThrowingComputePass",
                GVM::Core::ComputePassTaskDescriptor{.dispatchFn = [](GVM::RHI::ComputePassEncoder) {
                    throw std::runtime_error("QueueProxy test compute failure");
                }}),
            std::runtime_error);

        queue->submit();
        queue->readBuffer(GVM::RHI::BufferRange(destination), &readback, sizeof(readback))->submit();
        EXPECT_EQ(readback, sentinel);

        queue->blitPass("RecoveredCopy", GVM::Core::copyBufferToBuffer(source, 0, destination, 0, sizeof(uint32_t)))->submit();
        queue->readBuffer(GVM::RHI::BufferRange(destination), &readback, sizeof(readback))->submit();
        EXPECT_EQ(readback, value);
        device->freeBuffer(destination);
        device->freeBuffer(source);
    }

    TEST_F(RhiExecutionTest, DiscardsPendingPassesWhenRenderRecordingThrows)
    {
        GVM::Core::DeviceProxy coreDevice(device);
        auto queue = coreDevice->graphicsQueue(0);
        constexpr auto usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc;
        auto source = device->createBuffer({.label = "QueueProxyDiscardRenderSource", .usage = usage, .size = sizeof(uint32_t)});
        auto destination = device->createBuffer({.label = "QueueProxyDiscardRenderDestination", .usage = usage, .size = sizeof(uint32_t)});
        auto target = device->createTexture({
            .label = "QueueProxyDiscardRenderTarget",
            .usage = GVM::RHI::TextureUsage::RenderAttachment,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {4, 4, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
        });
        ASSERT_FALSE(source.isNull());
        ASSERT_FALSE(destination.isNull());
        ASSERT_FALSE(target.isNull());
        const QueueOrderingRenderTarget framebuffer{.colorView = target->createView()};

        const uint32_t sentinel = 0xdeadbeefu;
        const uint32_t value = 0x12345678u;
        uint32_t readback = 0u;

        queue->writeBuffer(GVM::RHI::BufferRange(destination), &sentinel, sizeof(sentinel))
            ->writeBuffer(GVM::RHI::BufferRange(source), &value, sizeof(value))
            ->blitPass("DiscardedCopy", GVM::Core::copyBufferToBuffer(source, 0, destination, 0, sizeof(uint32_t)));

        EXPECT_THROW(
            queue->renderPass(
                "ThrowingRenderPass",
                framebuffer,
                GVM::Core::RenderPassTaskDescriptor{.drawFn = [](GVM::RHI::RenderPassEncoder) {
                    throw std::runtime_error("QueueProxy test render failure");
                }}),
            std::runtime_error);

        queue->submit();
        queue->readBuffer(GVM::RHI::BufferRange(destination), &readback, sizeof(readback))->submit();
        EXPECT_EQ(readback, sentinel);

        queue->blitPass("RecoveredCopy", GVM::Core::copyBufferToBuffer(source, 0, destination, 0, sizeof(uint32_t)))->submit();
        queue->readBuffer(GVM::RHI::BufferRange(destination), &readback, sizeof(readback))->submit();
        EXPECT_EQ(readback, value);
        device->freeTexture(target);
        device->freeBuffer(destination);
        device->freeBuffer(source);
    }

    TEST_F(RhiExecutionTest, KeepsPendingPassesWhenTransferValidationFails)
    {
        GVM::Core::DeviceProxy coreDevice(device);
        auto queue = coreDevice->graphicsQueue(0);
        constexpr auto usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc;
        auto source = device->createBuffer({.label = "QueueProxyKeepSource", .usage = usage, .size = sizeof(uint32_t)});
        auto destination = device->createBuffer({.label = "QueueProxyKeepDestination", .usage = usage, .size = sizeof(uint32_t)});
        ASSERT_FALSE(source.isNull());
        ASSERT_FALSE(destination.isNull());

        const uint32_t value = 0x12345678u;
        uint32_t readback = 0u;
        const std::array<uint32_t, 4> oversized = {};

        queue->writeBuffer(GVM::RHI::BufferRange(source), &value, sizeof(value))
            ->blitPass("KeptCopy", GVM::Core::copyBufferToBuffer(source, 0, destination, 0, sizeof(uint32_t)));

        EXPECT_THROW(
            queue->writeBuffer(GVM::RHI::BufferRange(destination), oversized.data(), sizeof(oversized)),
            std::out_of_range);

        queue->submit();
        queue->readBuffer(GVM::RHI::BufferRange(destination), &readback, sizeof(readback))->submit();
        EXPECT_EQ(readback, value);
        device->freeBuffer(destination);
        device->freeBuffer(source);
    }

    /// Verifies the fixed Metal buffer table keeps a bind group bound at index 0 visible
    /// to a later pipeline that declares a vertex buffer, without re-binding the group.
    /// The vertex stage reads the group at buffer(0) while vertex buffer slot 0 is bound
    /// to the same stage's table.
    TEST_F(RhiExecutionTest, MetalFixedBindGroupIndexSurvivesVertexBufferPipelineSwitch)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Metal)
        {
            GTEST_SKIP() << "Tests the fixed Metal per-stage buffer-table layout.";
        }

        constexpr uint32_t kExpectedValue = 200u;
        const std::array<float, 6> positions = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

        auto groupBuffer = device->createBuffer({
            .label = "PersistenceGroupData",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(uint32_t),
        });
        auto vertexBuffer = device->createBuffer({
            .label = "PersistenceVertexData",
            .usage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(float) * positions.size(),
        });
        auto target = device->createTexture({
            .label = "PersistenceTarget",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
            .size = {8, 8, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        ASSERT_FALSE(groupBuffer.isNull());
        ASSERT_FALSE(vertexBuffer.isNull());
        ASSERT_FALSE(target.isNull());

        auto *queue = device->getMainQueue();
        queue->writeBuffer(GVM::RHI::BufferRange(groupBuffer), &kExpectedValue, sizeof(kExpectedValue));
        queue->writeBuffer(GVM::RHI::BufferRange(vertexBuffer), positions.data(), sizeof(float) * positions.size());

        GVM::RHI::BindGroupLayoutDescriptor bindGroupLayoutDescriptor = {};
        bindGroupLayoutDescriptor.entries.resize(1);
        bindGroupLayoutDescriptor.entries[0].binding = 0;
        bindGroupLayoutDescriptor.entries[0].visibility = GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
        bindGroupLayoutDescriptor.entries[0].buffer.type = GVM::RHI::BufferBindingType::Storage;
        bindGroupLayoutDescriptor.entries[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
        auto bindGroupLayout = device->createBindGroupLayout(bindGroupLayoutDescriptor);
        ASSERT_TRUE(static_cast<bool>(bindGroupLayout));

        GVM::RHI::BindGroupEntry bindGroupEntry = {};
        bindGroupEntry.binding = 0;
        bindGroupEntry.buffer = GVM::RHI::BufferRange(groupBuffer);
        auto bindGroup = device->createBindGroup({.layout = bindGroupLayout, .entries = bindGroupEntry});
        ASSERT_TRUE(static_cast<bool>(bindGroup));

        auto pipelineLayout = device->createPipelineLayout({.bindGroupLayouts = {bindGroupLayout}});
        auto shaderModule = device->createShaderModule({.label = "PersistenceShader", .code = kBindGroupPersistenceShaderCode});
        ASSERT_TRUE(static_cast<bool>(shaderModule));

        GVM::RHI::RenderPipelineDescriptor proceduralDescriptor = {};
        proceduralDescriptor.label = "PersistenceProceduralPipeline";
        proceduralDescriptor.layout = pipelineLayout;
        proceduralDescriptor.vertex.module = shaderModule;
        proceduralDescriptor.vertex.entryPoint = "procedural_vertex_main";
        proceduralDescriptor.fragment.module = shaderModule;
        proceduralDescriptor.fragment.entryPoint = "write_zero_main";
        proceduralDescriptor.fragment.targets.push_back({.format = GVM::RHI::TextureFormat::RGBA8Unorm});
        proceduralDescriptor.primitive.topology = GVM::RHI::PrimitiveTopology::TriangleList;
        auto proceduralPipeline = device->createRenderPipeline(proceduralDescriptor);
        ASSERT_TRUE(static_cast<bool>(proceduralPipeline));

        GVM::RHI::RenderPipelineDescriptor bufferedDescriptor = proceduralDescriptor;
        bufferedDescriptor.label = "PersistenceBufferedPipeline";
        bufferedDescriptor.vertex.entryPoint = "buffered_group_vertex_main";
        bufferedDescriptor.vertex.buffers.push_back({.arrayStride = sizeof(float) * 2,
            .attributes = {{.format = GVM::RHI::VertexFormat::Float32x2, .offset = 0, .shaderLocation = 0}}});
        bufferedDescriptor.fragment.entryPoint = "write_vertex_group_main";
        auto bufferedPipeline = device->createRenderPipeline(bufferedDescriptor);
        ASSERT_TRUE(static_cast<bool>(bufferedPipeline));

        auto commandEncoder = queue->createCommandEncoder();
        auto renderPass = commandEncoder->beginRenderPass({.label = "PersistencePass",
            .colorAttachments = {{.view = target->createView(),
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store}}});
        renderPass->setViewport(0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 1.0f);
        renderPass->setBindGroup(bindGroup, 0);
        renderPass->setPipeline(proceduralPipeline);
        renderPass->draw(3, 1, 0, 0);
        renderPass->setPipeline(bufferedPipeline);
        renderPass->setVertexBuffer(GVM::RHI::BufferRange(vertexBuffer), 0);
        renderPass->draw(3, 1, 0, 0);
        renderPass->end();
        commandEncoder->end();
        submitAndWait({commandEncoder});

        std::array<uint8_t, 8 * 8 * 4> pixels = {};
        {
            GVM::Core::DeviceProxy coreDevice(device);
            coreDevice->graphicsQueue(0)->readTexture(target, pixels.data(), pixels.size())->submit();
        }
        for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
        {
            EXPECT_EQ(pixels[offset], 0u);
            EXPECT_EQ(pixels[offset + 1], kExpectedValue);
            EXPECT_EQ(pixels[offset + 2], 0u);
            EXPECT_EQ(pixels[offset + 3], 255u);
        }
        device->freeTexture(target);
        device->freeBuffer(vertexBuffer);
        device->freeBuffer(groupBuffer);
    }


    /// Verifies Metal indirect command buffers inherit the vertex buffer bound at its
    /// fixed high buffer-table index when executing a buffered pipeline.
    TEST_F(RhiExecutionTest, MetalIndirectDrawInheritsFixedVertexBufferBinding)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Metal)
        {
            GTEST_SKIP() << "Tests the fixed Metal per-stage buffer-table layout.";
        }

        const std::array<float, 6> positions = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        const std::array<uint32_t, 8> indirectArguments = {3u, 1u, 0u, 0u, 3u, 1u, 0u, 0u};

        auto vertexBuffer = device->createBuffer({
            .label = "IndirectVertexData",
            .usage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(float) * positions.size(),
        });
        auto indirectBuffer = device->createBuffer({
            .label = "IndirectDrawCommands",
            .usage = GVM::RHI::BufferUsage::Indirect | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(uint32_t) * indirectArguments.size(),
        });
        auto target = device->createTexture({
            .label = "IndirectTarget",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
            .size = {8, 8, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        ASSERT_FALSE(vertexBuffer.isNull());
        ASSERT_FALSE(indirectBuffer.isNull());
        ASSERT_FALSE(target.isNull());

        auto *queue = device->getMainQueue();
        queue->writeBuffer(GVM::RHI::BufferRange(vertexBuffer), positions.data(), sizeof(float) * positions.size());
        queue->writeBuffer(GVM::RHI::BufferRange(indirectBuffer), indirectArguments.data(), sizeof(uint32_t) * indirectArguments.size());

        auto shaderModule = device->createShaderModule({.label = "IndirectShader", .code = kBindGroupPersistenceShaderCode});
        ASSERT_TRUE(static_cast<bool>(shaderModule));

        GVM::RHI::RenderPipelineDescriptor pipelineDescriptor = {};
        pipelineDescriptor.label = "IndirectBufferedPipeline";
        pipelineDescriptor.layout = device->createPipelineLayout({.bindGroupLayouts = {}});
        pipelineDescriptor.vertex.module = shaderModule;
        pipelineDescriptor.vertex.entryPoint = "buffered_vertex_main";
        pipelineDescriptor.vertex.buffers.push_back({.arrayStride = sizeof(float) * 2,
            .attributes = {{.format = GVM::RHI::VertexFormat::Float32x2, .offset = 0, .shaderLocation = 0}}});
        pipelineDescriptor.fragment.module = shaderModule;
        pipelineDescriptor.fragment.entryPoint = "write_white_main";
        pipelineDescriptor.fragment.targets.push_back({.format = GVM::RHI::TextureFormat::RGBA8Unorm});
        pipelineDescriptor.primitive.topology = GVM::RHI::PrimitiveTopology::TriangleList;
        auto pipeline = device->createRenderPipeline(pipelineDescriptor);
        ASSERT_TRUE(static_cast<bool>(pipeline));

        auto commandEncoder = queue->createCommandEncoder();
        auto renderPass = commandEncoder->beginRenderPass({.label = "IndirectPass",
            .colorAttachments = {{.view = target->createView(),
                .loadOp = GVM::RHI::LoadOp::Clear, .storeOp = GVM::RHI::StoreOp::Store}}});
        renderPass->setViewport(0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 1.0f);
        renderPass->setPipeline(pipeline);
        renderPass->setVertexBuffer(GVM::RHI::BufferRange(vertexBuffer), 0);
        renderPass->drawIndirect(GVM::RHI::BufferRange(indirectBuffer), 2, sizeof(uint32_t) * 4);
        renderPass->end();
        commandEncoder->end();
        submitAndWait({commandEncoder});

        std::array<uint8_t, 8 * 8 * 4> pixels = {};
        {
            GVM::Core::DeviceProxy coreDevice(device);
            coreDevice->graphicsQueue(0)->readTexture(target, pixels.data(), pixels.size())->submit();
        }
        for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
        {
            EXPECT_EQ(pixels[offset], 255u);
            EXPECT_EQ(pixels[offset + 1], 255u);
            EXPECT_EQ(pixels[offset + 2], 255u);
            EXPECT_EQ(pixels[offset + 3], 255u);
        }
        device->freeTexture(target);
        device->freeBuffer(indirectBuffer);
        device->freeBuffer(vertexBuffer);
    }

    /// Verifies resubmitting an already-submitted encoder is rejected and the queue stays usable.
    TEST_F(RhiExecutionTest, RejectsAlreadySubmittedCommandEncoder)
    {
        auto *queue = device->getMainQueue();
        ASSERT_NE(queue, nullptr);
        auto encoder = queue->createCommandEncoder();
        ASSERT_NE(encoder, nullptr);
        encoder->end();
        queue->submit({encoder});
        EXPECT_THROW(queue->submit({encoder}), std::logic_error);

        const std::array<uint32_t, 4> expected = {5u, 6u, 7u, 8u};
        auto source = device->createBuffer({
            .label = "ResubmitUsableSource",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(expected),
        });
        auto destination = device->createBuffer({
            .label = "ResubmitUsableDestination",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(expected),
        });
        ASSERT_FALSE(source.isNull());
        ASSERT_FALSE(destination.isNull());
        queue->writeBuffer(GVM::RHI::BufferRange(source), expected.data(), sizeof(expected));
        auto copyEncoder = queue->createCommandEncoder();
        auto copyPass = copyEncoder->beginBlitPass({});
        copyPass->copyBufferToBuffer(GVM::RHI::BufferRange(source), GVM::RHI::BufferRange(destination));
        copyPass->end();
        auto readback = queue->readBufferAsync(GVM::RHI::BufferRange(destination));
        queue->submit({copyEncoder});
        readback->wait();
        EXPECT_EQ(std::memcmp(readback->getData().data(), expected.data(), sizeof(expected)), 0);
        device->freeBuffer(destination);
        device->freeBuffer(source);
    }

    /// Verifies listing one encoder twice in a submit is rejected and the queue stays usable.
    TEST_F(RhiExecutionTest, RejectsDuplicateCommandEncoderInSubmitList)
    {
        auto *queue = device->getMainQueue();
        ASSERT_NE(queue, nullptr);
        auto encoder = queue->createCommandEncoder();
        ASSERT_NE(encoder, nullptr);
        encoder->end();
        EXPECT_THROW(queue->submit({encoder, encoder}), std::logic_error);

        const std::array<uint32_t, 4> expected = {9u, 10u, 11u, 12u};
        auto source = device->createBuffer({
            .label = "DuplicateSubmitUsableSource",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(expected),
        });
        auto destination = device->createBuffer({
            .label = "DuplicateSubmitUsableDestination",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(expected),
        });
        ASSERT_FALSE(source.isNull());
        ASSERT_FALSE(destination.isNull());
        queue->writeBuffer(GVM::RHI::BufferRange(source), expected.data(), sizeof(expected));
        auto copyEncoder = queue->createCommandEncoder();
        auto copyPass = copyEncoder->beginBlitPass({});
        copyPass->copyBufferToBuffer(GVM::RHI::BufferRange(source), GVM::RHI::BufferRange(destination));
        copyPass->end();
        auto readback = queue->readBufferAsync(GVM::RHI::BufferRange(destination));
        queue->submit({copyEncoder});
        readback->wait();
        EXPECT_EQ(std::memcmp(readback->getData().data(), expected.data(), sizeof(expected)), 0);
        device->freeBuffer(destination);
        device->freeBuffer(source);
    }

} // namespace

    /// Verifies a queued buffer readback returns the written bytes after submit and wait.
    TEST_F(RhiExecutionTest, ReadBufferAsyncReturnsWrittenBytesAfterSubmit)
    {
        auto buffer = device->createBuffer({
            .label = "AsyncReadbackSource",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapWrite,
            .size = 32u,
        });
        ASSERT_FALSE(buffer.isNull());
        const std::array<uint32_t, 8u> expected = {10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u};
        auto *queue = device->getMainQueue();
        buffer->map();
        std::memcpy(buffer->getMappedRange(0u, 32u), expected.data(), 32u);
        buffer->unmap();
        queue->submit({});
        auto readback = queue->readBufferAsync(GVM::RHI::BufferRange(buffer, 0u, 32u));
        ASSERT_TRUE(readback != nullptr);
        queue->submit({});
        readback->wait();
        const auto bytes = readback->getData();
        ASSERT_EQ(bytes.size(), 32u);
        EXPECT_EQ(std::memcmp(bytes.data(), expected.data(), 32u), 0);
        device->freeBuffer(buffer);
    }

    /// Verifies isReady() is false before submission and true after the readback completes.
    TEST_F(RhiExecutionTest, ReadBufferAsyncIsReadyOnlyAfterSubmit)
    {
        auto buffer = device->createBuffer({
            .label = "AsyncReadbackReady",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapWrite,
            .size = 16u,
        });
        ASSERT_FALSE(buffer.isNull());
        auto *queue = device->getMainQueue();
        buffer->map();
        std::memset(buffer->getMappedRange(0u, 16u), 0xAB, 16u);
        buffer->unmap();
        queue->submit({});
        auto readback = queue->readBufferAsync(GVM::RHI::BufferRange(buffer, 0u, 16u));
        ASSERT_TRUE(readback != nullptr);
        EXPECT_FALSE(readback->isReady());
        queue->submit({});
        readback->wait();
        EXPECT_TRUE(readback->isReady());
        device->freeBuffer(buffer);
    }

    /// Verifies releasing a Readback before submission is safe; the tracker retains it until completion.
    TEST_F(RhiExecutionTest, ReadBufferAsyncReleaseBeforeSubmitDoesNotCrash)
    {
        auto buffer = device->createBuffer({
            .label = "AsyncReadbackRelease",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapWrite,
            .size = 16u,
        });
        ASSERT_FALSE(buffer.isNull());
        auto *queue = device->getMainQueue();
        buffer->map();
        std::memset(buffer->getMappedRange(0u, 16u), 0xCD, 16u);
        buffer->unmap();
        queue->submit({});
        {
            auto readback = queue->readBufferAsync(GVM::RHI::BufferRange(buffer, 0u, 16u));
            ASSERT_TRUE(readback != nullptr);
        }
        queue->submit({});
        device->freeBuffer(buffer);
    }

    /// Verifies multiple readbacks in flight at once each resolve to their own bytes.
    TEST_F(RhiExecutionTest, ReadBufferAsyncThreeInFlight)
    {
        auto buffers = std::array<GVM::RHI::Buffer, 3u>();
        const std::array<std::array<uint32_t, 4u>, 3u> patterns = {{
            {1u, 2u, 3u, 4u},
            {5u, 6u, 7u, 8u},
            {9u, 10u, 11u, 12u},
        }};
        auto *queue = device->getMainQueue();
        auto readbacks = std::array<GVM::RHI::Readback, 3u>();
        for (size_t index = 0u; index < 3u; ++index)
        {
            buffers[index] = device->createBuffer({
                .label = "AsyncReadbackInFlight",
                .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapWrite,
                .size = 16u,
            });
            ASSERT_FALSE(buffers[index].isNull());
            buffers[index]->map();
            std::memcpy(buffers[index]->getMappedRange(0u, 16u), patterns[index].data(), 16u);
            buffers[index]->unmap();
            readbacks[index] = queue->readBufferAsync(GVM::RHI::BufferRange(buffers[index], 0u, 16u));
            ASSERT_TRUE(readbacks[index] != nullptr);
        }
        queue->submit({});
        for (size_t index = 0u; index < 3u; ++index)
        {
            readbacks[index]->wait();
            EXPECT_EQ(std::memcmp(readbacks[index]->getData().data(), patterns[index].data(), 16u), 0);
            device->freeBuffer(buffers[index]);
        }
    }

    /// Verifies a texture readback preserves row-pitch layout across each row.
    TEST_F(RhiExecutionTest, ReadTextureAsyncPreservesRowPitchLayout)
    {
        const uint32_t width = 4u;
        const uint32_t height = 4u;
        const uint64_t bytesPerRow = width * 4u;
        auto texture = device->createTexture({
            .label = "AsyncReadbackTexture",
            .usage = GVM::RHI::TextureUsage::CopySrc | GVM::RHI::TextureUsage::CopyDst,
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .size = {width, height, 1u},
        });
        ASSERT_FALSE(texture.isNull());
        const std::array<uint8_t, 64u> sourceData = [] {
            std::array<uint8_t, 64u> data = {};
            for (size_t index = 0u; index < data.size(); ++index)
            {
                data[index] = static_cast<uint8_t>(index);
            }
            return data;
        }();
        auto *queue = device->getMainQueue();
        queue->writeTexture(
            GVM::RHI::ImageCopyTexture{.texture = texture, .mipLevel = 0u, .origin = {0u, 0u, 0u}},
            sourceData.data(),
            sourceData.size(),
            GVM::RHI::TextureDataLayout{},
            {width, height, 1u});
        queue->submit({});
        auto readback = queue->readTextureAsync(
            GVM::RHI::ImageCopyTexture{.texture = texture, .mipLevel = 0u, .origin = {0u, 0u, 0u}},
            GVM::RHI::TextureDataLayout{.offset = 0u, .bytesPerRow = width * 4u, .rowsPerImage = height},
            {width, height, 1u});
        ASSERT_TRUE(readback != nullptr);
        queue->submit({});
        readback->wait();
        const auto bytes = readback->getData();
        ASSERT_GE(bytes.size(), static_cast<size_t>(bytesPerRow * height));
        for (uint32_t row = 0u; row < height; ++row)
        {
            EXPECT_EQ(std::memcmp(bytes.data() + row * bytesPerRow, sourceData.data() + row * bytesPerRow, bytesPerRow), 0);
        }
        device->freeTexture(texture);
    }

    /// Verifies direct-mapped and staging readbacks both return correct bytes.
    TEST_F(RhiExecutionTest, ReadBufferAsyncMappableAndStagingSources)
    {
        auto mappable = device->createBuffer({
            .label = "AsyncReadbackMappable",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 16u,
        });
        auto staging = device->createBuffer({
            .label = "AsyncReadbackStaging",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = 16u,
        });
        ASSERT_FALSE(mappable.isNull());
        ASSERT_FALSE(staging.isNull());
        auto *queue = device->getMainQueue();
        mappable->map();
        std::memset(mappable->getMappedRange(0u, 16u), 0x11, 16u);
        mappable->unmap();
        queue->fillBuffer(GVM::RHI::BufferRange(staging, 0u, 16u), 0x22u);
        auto mappableReadback = queue->readBufferAsync(GVM::RHI::BufferRange(mappable, 0u, 16u));
        auto stagingReadback = queue->readBufferAsync(GVM::RHI::BufferRange(staging, 0u, 16u));
        ASSERT_TRUE(mappableReadback != nullptr);
        ASSERT_TRUE(stagingReadback != nullptr);
        queue->submit({});
        mappableReadback->wait();
        stagingReadback->wait();
        const auto mappableBytes = mappableReadback->getData();
        const auto stagingBytes = stagingReadback->getData();
        for (size_t index = 0u; index < 16u; ++index)
        {
            EXPECT_EQ(mappableBytes[index], 0x11u);
            EXPECT_EQ(stagingBytes[index], 0x22u);
        }
        device->freeBuffer(staging);
        device->freeBuffer(mappable);
    }

    /// Verifies a typed GpuReadback via QueueProxy returns the written value after submit and wait.
    TEST_F(RhiExecutionTest, QueueProxyReadBufferAsyncReturnsTypedValue)
    {
        using ReadbackValue = uint32_t[4];
        const ReadbackValue expected = {3u, 5u, 7u, 9u};
        auto buffer = device->createBuffer({
            .label = "GpuReadbackTyped",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Storage,
            .size = sizeof(expected),
        });
        ASSERT_FALSE(buffer.isNull());
        GVM::Core::DeviceProxy deviceProxy(device);
        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->writeBuffer(GVM::RHI::BufferRange(buffer), expected)->submit();
        auto readback = queue->readBufferAsync<ReadbackValue>(GVM::RHI::BufferRange(buffer));
        queue->submit();
        readback.wait();
        const ReadbackValue &actual = readback.get();
        for (uint32_t index = 0u; index < 4u; ++index)
        {
            EXPECT_EQ(actual[index], expected[index]);
        }
        device->freeBuffer(buffer);
    }
