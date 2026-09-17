#define GLM_ENABLE_EXPERIMENTAL

#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMShaderReadbackTest.hpp"
#include "generate_result.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>


namespace
{
    /** Loads the generated direct SPIR-V disassembly for the experimental uniform-buffer runtime fixture. */
    std::string readGeneratedUniformSPIRVDisassembly()
    {
        const std::filesystem::path disassemblyPath =
            GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR") /
            "spv" /
            "ExperimentalUGLIRRuntimeUniformPass.raw.spvasm";
        return GVM::Tests::readTextFile(disassemblyPath);
    }

    /** Uses the explicit backend and device lifecycle shared by compiler readback fixtures. */
    using RhiUGLIRUniformBufferTest = GVM::Tests::ShaderReadbackTest;

    TEST_F(RhiUGLIRUniformBufferTest, DispatchesUniformBufferComputeThroughShaderArtifact)
    {
        const std::string disassembly = readGeneratedUniformSPIRVDisassembly();
        EXPECT_EQ(disassembly.find("OpDecorate %ExperimentalUGLIRRuntimeUniformParams Block"), std::string::npos);
        EXPECT_NE(disassembly.find("OpTypeRuntimeArray %ExperimentalUGLIRRuntimeUniformParams"), std::string::npos);
        ASSERT_NE(disassembly.find("OpAccessChain %_ptr_Uniform_uint %bindGroup_params %uint_0 %uint_0"), std::string::npos);

        constexpr uint32_t kElementCount = 4u;
        constexpr uint64_t kOutputByteSize = sizeof(uint32_t) * kElementCount;
        ExperimentalUGLIRRuntimeUniformParams params{.addend = 37u};

        auto paramsBuffer = device->createBuffer({
            .label = "ExperimentalUGLIRRuntimeUniformParams",
            .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(params),
        });
        auto outputBuffer = device->createBuffer({
            .label = "UGLIRRuntimeUniformOutput",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc,
            .size = kOutputByteSize,
        });
        ASSERT_FALSE(paramsBuffer.isNull());
        ASSERT_FALSE(outputBuffer.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto bindGroup = deviceProxy->createBindGroup<ExperimentalUGLIRRuntimeUniformBindGroup>(
            GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(params)),
            GVM::RHI::BufferRange(outputBuffer, 0u, kOutputByteSize));
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        auto uniformPass = deviceProxy->createComputeClass<ExperimentalUGLIRRuntimeUniformPass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(uniformPass));

        auto uploadQueue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(uploadQueue));
        uploadQueue
            ->writeBuffer(GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(params)), &params, sizeof(params))
            ->submit();

        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        uint32_t output[4] = {};
        queue->computePass("CompilerShaderReadback", uniformPass->run(kElementCount, 1u, 1u))->submit();
        queue->readBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, kOutputByteSize), output, sizeof(output))
            ->submit();
        EXPECT_EQ(output[0], 37u);
        EXPECT_EQ(output[1], 38u);
        EXPECT_EQ(output[2], 39u);
        EXPECT_EQ(output[3], 40u);


        device->freeBuffer(outputBuffer);
        device->freeBuffer(paramsBuffer);
    }

    TEST_F(RhiUGLIRUniformBufferTest, PreservesUniformMatrixThroughNestedAggregateReturn)
    {
        // Host matrix storage follows the GPU ABI: four consecutive DSL rows.
        const float params[20] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        uint32_t output[10] = {};
        auto paramsBuffer = device->createBuffer({
            .label = "UniformMatrixAggregateInput",
            .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(params),
        });
        auto outputBuffer = device->createBuffer({
            .label = "UniformMatrixAggregateOutput",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc,
            .size = sizeof(output),
        });
        ASSERT_FALSE(paramsBuffer.isNull());
        ASSERT_FALSE(outputBuffer.isNull());
        GVM::Core::DeviceProxy deviceProxy(device);
        auto bindGroup = deviceProxy->createBindGroup<ExperimentalUGLIRUniformMatrixBindGroup>(
            GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(params)),
            GVM::RHI::BufferRange(outputBuffer, 0u, sizeof(output)));
        auto pass = deviceProxy->createComputeClass<ExperimentalUGLIRUniformMatrixPass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(pass));
        auto queue = deviceProxy->graphicsQueue(0);
        queue->writeBuffer(GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(params)), params, sizeof(params))->submit();
        queue->computePass("UniformMatrixAggregate", pass->run(2u, 1u, 1u))->submit();
        queue->readBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, sizeof(output)), output, sizeof(output))->submit();
        const uint32_t expected[10] = {2u, 5u, 90u, 100u, 3u, 2u, 5u, 90u, 100u, 5u};
        for (uint32_t index = 0; index < 10u; ++index)
            EXPECT_EQ(output[index], expected[index]) << "observation " << index;
        device->freeBuffer(outputBuffer);
        device->freeBuffer(paramsBuffer);
    }
} // namespace
