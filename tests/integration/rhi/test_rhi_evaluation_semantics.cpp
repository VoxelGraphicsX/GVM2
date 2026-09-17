#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <EASTL/array.h>
#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMShaderReadbackTest.hpp"
#include "generate_result.hpp"

namespace
{
    /** Computes exact observations from language semantics, independently of either emitter. */
    eastl::array<uint32_t, 71> expectedEvaluationObservations(bool condition)
    {
        const uint32_t predicate = condition ? 1u : 0u;
        const float scalarNumerator = condition ? 3.5f : -3.5f;
        const float scalarDivisor = condition ? -2.0f : 2.0f;
        const float vectorNumeratorX = condition ? 3.5f : -3.5f;
        const float vectorNumeratorY = condition ? -3.5f : 3.5f;
        const float vectorDivisorX = condition ? -2.0f : 2.0f;
        const float vectorDivisorY = condition ? 2.0f : -2.0f;
        return {0u, 0u, predicate, 1u - predicate, 1u, 101u, 101u, 1u,
                101u, 201u, 101u, condition ? 10u : 30u, 40u, 50u, 7u, 10u,
                506u, 5001u, 1u, 123u, 1u, 127u, 1u, 1u,
                1u, condition ? 111u : 122u, predicate, condition ? 1u : 2u,
                3u, 2u, condition ? 2u : 1u, predicate, 2u, 2u, 100u, 102u,
                5u, 5u, 15u, condition ? 7u : 13u,
                7u, 7u, 10u, 10u, 6u, 1u, 1324u, 1324u, 1u, 7u, 7u, 999u, 6u, 5u,
                107u + predicate, 20u + predicate, 7u + predicate, predicate,
                std::bit_cast<uint32_t>(std::fmod(scalarNumerator, scalarDivisor)),
                std::bit_cast<uint32_t>(std::fmod(vectorNumeratorX, vectorDivisorX)),
                std::bit_cast<uint32_t>(std::fmod(vectorNumeratorY, vectorDivisorY)),
                50u + predicate,
                52u + predicate,
                (52u + predicate) * 100u + 53u + predicate,
                (54u + predicate) * 100u + 54u + predicate,
                1101u,
                7u,
                7u,
                14u,
                8u + 2u * predicate,
                23u + predicate};
    }

    using RhiEvaluationSemanticsTest = GVM::Tests::ShaderReadbackTest;

    TEST_F(RhiEvaluationSemanticsTest, PreservesEvaluationAgainstIndependentCPUExpectations)
    {
        const eastl::array<uint32_t, 2> conditions = {0u, 1u};
        const eastl::array<float, 16> fmodInputs = {
            -3.5f, 2.0f, -3.5f, 3.5f,
            2.0f, -2.0f, 0.0f, 0.0f,
            3.5f, -2.0f, 3.5f, -3.5f,
            -2.0f, 2.0f, 0.0f, 0.0f};
        eastl::array<uint32_t, 142> output;
        output.fill(0xDEADBEEFu);
        auto inputBuffer = device->createBuffer({
            .label = "EvaluationConditions",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(conditions),
        });
        auto fmodInputBuffer = device->createBuffer({
            .label = "EvaluationFmodInputs",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(fmodInputs),
        });
        auto outputBuffer = device->createBuffer({
            .label = "EvaluationObservations",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(output),
        });
        ASSERT_FALSE(inputBuffer.isNull());
        ASSERT_FALSE(fmodInputBuffer.isNull());
        ASSERT_FALSE(outputBuffer.isNull());
        GVM::Core::DeviceProxy deviceProxy(device);
        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->writeBuffer(GVM::RHI::BufferRange(inputBuffer, 0u, sizeof(conditions)), conditions.data(), sizeof(conditions))
            ->writeBuffer(GVM::RHI::BufferRange(fmodInputBuffer, 0u, sizeof(fmodInputs)), fmodInputs.data(), sizeof(fmodInputs))
            ->writeBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, sizeof(output)), output.data(), sizeof(output))
            ->submit();
        auto bindGroup = deviceProxy->createBindGroup<EvaluationSemanticsBindGroup>(
            GVM::RHI::BufferRange(inputBuffer, 0u, sizeof(conditions)),
            GVM::RHI::BufferRange(outputBuffer, 0u, sizeof(output)),
            GVM::RHI::BufferRange(fmodInputBuffer, 0u, sizeof(fmodInputs)));
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        const auto creationStarted = std::chrono::steady_clock::now();
        auto pass = deviceProxy->createComputeClass<EvaluationSemanticsPass>(bindGroup);
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
            queue->writeBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, sizeof(output)), output.data(), sizeof(output))->submit();
            if (GVM::Tests::shaderReadbackMeasureGpuTime)
            {
                profiler.reset();
                const auto scope = profiler.writePass("EvaluationSemantics");
                queue->computePass("EvaluationSemantics", scope, pass->run(2u, 1u, 1u))->resolveTimestampProfiler(profiler)->submit();
                eastl::vector<GVM::RHI::TimestampRawResult> timestamps;
                profiler.readbackBlocking(device->getMainQueue(), timestamps);
                const auto ranges = profiler.buildRangeResults(timestamps);
                ASSERT_EQ(ranges.size(), 1u);
                ASSERT_GT(ranges.front().durationNs, 0.0);
                gpuDurationNs += ranges.front().durationNs;
            }
            else { queue->computePass("EvaluationSemantics", pass->run(2u, 1u, 1u))->submit(); }
            queue->readBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, sizeof(output)), output.data(), sizeof(output))->submit();
            for (uint32_t lane = 0u; lane < 2u; ++lane)
            {
                const auto expected = expectedEvaluationObservations(lane != 0u);
                for (uint32_t index = 0u; index < expected.size(); ++index)
                {
                    EXPECT_EQ(output[lane * expected.size() + index], expected[index]) << "lane=" << lane << " observation=" << index;
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
        device->freeBuffer(fmodInputBuffer);
        device->freeBuffer(inputBuffer);
    }
}
