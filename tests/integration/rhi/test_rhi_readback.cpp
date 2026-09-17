#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>
#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/VulkanTestHooks.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "MCommandEncoder.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace GVM::RHI::Vulkan
{
    struct VKSubmissionCompletionState;
    using VKSubmissionCompletion = eastl::shared_ptr<VKSubmissionCompletionState>;

    class VKQueue : public QueueImpl
    {
    public:
        [[nodiscard]] VKSubmissionCompletion getMostRecentSubmissionCompletion() const;
        void waitForSubmission(const VKSubmissionCompletion &completion, const char *waitSource = "unspecified");
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
                const auto completion = vulkanQueue->getMostRecentSubmissionCompletion();
                if (completion)
                {
                    vulkanQueue->waitForSubmission(completion);
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
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 16,
        });
        auto dstBuffer = device->createBuffer({
            .label = "MultiRegionDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 16,
        });
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionDescriptor",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
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
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 32,
        });
        auto dstBuffer = device->createBuffer({
            .label = "MultiRegionFastPathDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 32,
        });
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionFastPathDescriptor",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
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

    TEST_F(RhiExecutionTest, CopyBufferMultipleRegionRejectsNonAlignedOffsets)
    {
        auto srcBuffer = device->createBuffer({
            .label = "MultiRegionByteFallbackSrc",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 24,
        });
        auto dstBuffer = device->createBuffer({
            .label = "MultiRegionByteFallbackDst",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = 24,
        });
        auto regionBuffer = device->createBuffer({
            .label = "MultiRegionByteFallbackDescriptor",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
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

    TEST_F(RhiExecutionTest, VulkanQueueReadbackConsumesOutstandingWriteAfterFirstRead)
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
        EXPECT_FALSE(stateAfterFirstRead.lastWriteInitialized);
        EXPECT_EQ(stateAfterFirstRead.lastWriteStageMaskBits, 0u);
        EXPECT_EQ(stateAfterFirstRead.lastWriteAccessMaskBits, 0u);

        queue->readBuffer(GVM::RHI::BufferRange(buffer), secondReadback.data(), sizeof(secondReadback));
        queue->submit(encoders);
        EXPECT_EQ(secondReadback, expected);

        const auto stateAfterSecondRead = GVM::RHI::Vulkan::Testing::getCurrentBufferState(queue, buffer);
        EXPECT_TRUE(stateAfterSecondRead.initialized);
        EXPECT_FALSE(stateAfterSecondRead.lastWriteInitialized);
        EXPECT_EQ(stateAfterSecondRead.lastWriteStageMaskBits, 0u);
        EXPECT_EQ(stateAfterSecondRead.lastWriteAccessMaskBits, 0u);

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
} // namespace
