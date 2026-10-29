#include <EASTL/array.h>
#include <bit>
#include <cstddef>
#include "GVMShaderReadbackTest.hpp"
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "generate_result.hpp"

namespace
{
    using RhiBufferLayoutSemanticsTest = GVM::Tests::ShaderReadbackTest;

    TEST_F(RhiBufferLayoutSemanticsTest, ReadsIndependentHostBytesAcrossStorageAndUniformLayouts)
    {
        static_assert(sizeof(BufferLayoutLeaf) == 16);
        static_assert(offsetof(BufferLayoutLeaf, marker) == 4);
        static_assert(offsetof(BufferLayoutLeaf, pair) == 8);
        static_assert(sizeof(BufferLayoutPayload) == 128);
        static_assert(offsetof(BufferLayoutPayload, leaves) == 16);
        static_assert(offsetof(BufferLayoutPayload, rectangle) == 48);
        static_assert(offsetof(BufferLayoutPayload, narrow) == 80);
        static_assert(offsetof(BufferLayoutPayload, trailing) == 112);
        static_assert(sizeof(BufferLayoutUniformPayload) == 144);
        static_assert(offsetof(BufferLayoutUniformPayload, rectangles) == 16);
        static_assert(offsetof(BufferLayoutUniformPayload, tall) == 80);
        static_assert(offsetof(BufferLayoutUniformPayload, marker) == 128);
        using GVM::Core::Math::ShaderHalf;
        EXPECT_EQ(ShaderHalf(1.0f).bits, 0x3c00u);
        EXPECT_EQ(float(std::numeric_limits<ShaderHalf>::max()), 65504.0f);
        EXPECT_EQ(float(glm::clamp(ShaderHalf(3.0f), ShaderHalf(0.0f), ShaderHalf(2.0f))), 2.0f);
        EXPECT_NE(std::numeric_limits<ShaderHalf>::quiet_NaN(), std::numeric_limits<ShaderHalf>::quiet_NaN());

        eastl::array<uint32_t, 64> storage = {};
        eastl::array<uint32_t, 36> uniform = {};
        uniform[0] = 0x49004880u;
        uniform[1] = 300u;
        uniform[2] = std::bit_cast<uint32_t>(11.0f);
        uniform[3] = std::bit_cast<uint32_t>(12.0f);
        for (uint32_t index = 0; index < 16u; ++index) { uniform[4u + index] = std::bit_cast<uint32_t>(float(101u + index)); }
        for (uint32_t index = 0; index < 12u; ++index) { uniform[20u + index] = std::bit_cast<uint32_t>(float(201u + index)); }
        uniform[32] = 999u;
        for (uint32_t lane = 0; lane < 2; ++lane)
        {
            const uint32_t base = lane * 32u;
            const float addend = float(lane * 1000u);
            for (uint32_t index = 0; index < 4; ++index) { storage[base + index] = std::bit_cast<uint32_t>(addend + float(index + 1u)); }
            storage[base + 4u] = 0x40003c00u;
            storage[base + 5u] = 100u + lane;
            storage[base + 6u] = std::bit_cast<uint32_t>(3.0f);
            storage[base + 7u] = std::bit_cast<uint32_t>(4.0f);
            storage[base + 8u] = 0x46004500u;
            storage[base + 9u] = 200u + lane;
            storage[base + 10u] = std::bit_cast<uint32_t>(7.0f);
            storage[base + 11u] = std::bit_cast<uint32_t>(8.0f);
            for (uint32_t index = 0; index < 8; ++index)
            {
                storage[base + 12u + index] = std::bit_cast<uint32_t>(addend + float(index + 11u));
                storage[base + 20u + index] = std::bit_cast<uint32_t>(addend + float(index + 21u));
            }
            for (uint32_t index = 0; index < 4; ++index) { storage[base + 28u + index] = std::bit_cast<uint32_t>(addend + float(index + 31u)); }
        }
        eastl::array<uint32_t, 42> output;
        output.fill(0xDEADBEEFu);
        auto storageBuffer = device->createBuffer({.label = "LayoutStorage", .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst, .size = sizeof(storage)});
        auto uniformBuffer = device->createBuffer({.label = "LayoutUniform", .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::CopyDst, .size = sizeof(uniform)});
        auto outputBuffer = device->createBuffer({.label = "LayoutOutput", .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst, .size = sizeof(output)});
        ASSERT_FALSE(storageBuffer.isNull());
        ASSERT_FALSE(uniformBuffer.isNull());
        ASSERT_FALSE(outputBuffer.isNull());
        GVM::Core::DeviceProxy deviceProxy(device);
        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->writeBuffer(GVM::RHI::BufferRange(storageBuffer, 0, sizeof(storage)), storage.data(), sizeof(storage))
            ->writeBuffer(GVM::RHI::BufferRange(uniformBuffer, 0, sizeof(uniform)), uniform.data(), sizeof(uniform))
            ->writeBuffer(GVM::RHI::BufferRange(outputBuffer, 0, sizeof(output)), output.data(), sizeof(output))->submit();
        auto bindGroup = deviceProxy->createBindGroup<BufferLayoutBindGroup>(
            GVM::RHI::BufferRange(storageBuffer, 0, sizeof(storage)), GVM::RHI::BufferRange(uniformBuffer, 0, sizeof(uniform)),
            GVM::RHI::BufferRange(outputBuffer, 0, sizeof(output)));
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        const auto creationStarted = std::chrono::steady_clock::now();
        auto pass = deviceProxy->createComputeClass<BufferLayoutPass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(pass));
        RecordProperty("compute_class_creation_us", std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - creationStarted).count()));
        GVM::RHI::GpuTimestampFrameProfiler profiler;
        if (GVM::Tests::shaderReadbackMeasureGpuTime) { profiler.init(device, 1u, "ReadbackPerformance"); }
        double gpuDurationNs = 0.0;
        uint64_t initialResourceBytes = 0;
        uint64_t finalResourceBytes = 0;
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + std::chrono::seconds(GVM::Tests::shaderReadbackDurationSeconds);
        uint64_t iterations = 0;
        do
        {
            output.fill(0xDEADBEEFu);
            queue->writeBuffer(GVM::RHI::BufferRange(outputBuffer, 0, sizeof(output)), output.data(), sizeof(output))->submit();
            if (GVM::Tests::shaderReadbackMeasureGpuTime)
            {
                profiler.reset();
                const auto scope = profiler.writePass("BufferLayoutSemantics");
                queue->computePass("BufferLayoutSemantics", scope, pass->run(2u, 1u, 1u))->resolveTimestampProfiler(profiler)->submit();
                eastl::vector<GVM::RHI::TimestampRawResult> timestamps;
                profiler.readbackBlocking(device->getMainQueue(), timestamps);
                const auto ranges = profiler.buildRangeResults(timestamps);
                ASSERT_EQ(ranges.size(), 1u);
                ASSERT_GT(ranges.front().durationNs, 0.0);
                gpuDurationNs += ranges.front().durationNs;
            }
            else { queue->computePass("BufferLayoutSemantics", pass->run(2u, 1u, 1u))->submit(); }
            queue->readBuffer(GVM::RHI::BufferRange(outputBuffer, 0, sizeof(output)), output.data(), sizeof(output))->submit();
            for (uint32_t lane = 0; lane < 2; ++lane)
            {
                const uint32_t addend = lane * 1000u;
                const eastl::array<uint32_t, 21> expected = {addend + 1u, 1u, 100u + lane, 4u, 6u, 200u + lane,
                    addend + 12u, addend + 18u, addend + 34u, 9u, 300u, 12u, addend + 26u, addend + 27u,
                    102u, 116u, 212u, 999u, 107u + lane * 8u, 100u + lane * 101u, lane + 1u};
                for (uint32_t index = 0; index < expected.size(); ++index)
                {
                    EXPECT_EQ(output[lane * 21u + index], expected[index]) << "lane=" << lane << " field=" << index;
                }
            }
            if (iterations == 0) { initialResourceBytes = device->getDiagnosticsResourceSnapshot().totalEstimatedBytes; }
            ++iterations;
        } while (!HasFailure() && std::chrono::steady_clock::now() < deadline);
        finalResourceBytes = device->getDiagnosticsResourceSnapshot().totalEstimatedBytes;
        EXPECT_LE(finalResourceBytes, initialResourceBytes) << "Tracked GPU resources grew during repeated readback.";
        RecordProperty("initial_resource_bytes", std::to_string(initialResourceBytes));
        RecordProperty("final_resource_bytes", std::to_string(finalResourceBytes));
        if (GVM::Tests::shaderReadbackMeasureGpuTime) { RecordProperty("mean_gpu_pass_ns", std::to_string(gpuDurationNs / double(iterations))); }
        RecordProperty("readback_iterations", std::to_string(iterations));
        RecordProperty("readback_duration_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()));
        pass = nullptr;
        bindGroup = nullptr;
        device->freeBuffer(outputBuffer);
        device->freeBuffer(uniformBuffer);
        device->freeBuffer(storageBuffer);
    }
}
