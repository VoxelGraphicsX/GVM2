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
    /** Loads the generated direct SPIR-V disassembly for the experimental render uniform fixture. */
    std::string readGeneratedRenderUniformSPIRVDisassembly()
    {
        const std::filesystem::path disassemblyPath =
            GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR") /
            "spv" /
            "ExperimentalUGLIRRuntimeRenderUniformPass__fragment.raw.spvasm";
        return GVM::Tests::readTextFile(disassemblyPath);
    }

    /** Uses the explicit backend and device lifecycle shared by compiler readback fixtures. */
    using RhiUGLIRUniformRenderTest = GVM::Tests::ShaderReadbackTest;

    TEST_F(RhiUGLIRUniformRenderTest, DrawsUniformTintThroughShaderArtifact)
    {
        const std::string disassembly = readGeneratedRenderUniformSPIRVDisassembly();
        ASSERT_NE(disassembly.find("OpDecorate %UniformBlock_ExperimentalUGLIRRuntimeRenderUniformParams Block"), std::string::npos);
        ASSERT_NE(disassembly.find("OpVariable %_ptr_Uniform_UniformBlock_ExperimentalUGLIRRuntimeRenderUniformParams Uniform"), std::string::npos);
        ASSERT_NE(disassembly.find("OpAccessChain"), std::string::npos);
        ASSERT_NE(disassembly.find("OpLoad"), std::string::npos);

        constexpr uint32_t kWidth = 4u;
        constexpr uint32_t kHeight = 4u;
        constexpr uint32_t kBytesPerPixel = 4u;
        constexpr uint64_t kReadbackByteSize = kWidth * kHeight * kBytesPerPixel;
        ExperimentalUGLIRRuntimeRenderUniformParams params{.tint = float4(0.25f, 0.5f, 0.75f, 1.0f)};

        auto paramsBuffer = device->createBuffer({
            .label = "ExperimentalUGLIRRuntimeRenderUniformParams",
            .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(params),
        });
        auto renderTarget = device->createTexture({
            .label = "UGLIRRuntimeRenderUniformTarget",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
            .size = {kWidth, kHeight, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        ASSERT_FALSE(paramsBuffer.isNull());
        ASSERT_FALSE(renderTarget.isNull());
        auto renderTargetView = renderTarget->createView();
        ASSERT_FALSE(renderTargetView.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto bindGroup = deviceProxy->createBindGroup<ExperimentalUGLIRRuntimeRenderUniformBindGroup>(
            GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(params)));
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        auto renderPassClass = deviceProxy->createRenderClass<ExperimentalUGLIRRuntimeRenderUniformPass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(renderPassClass));

        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue
            ->writeBuffer(GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(params)), &params, sizeof(params))
            ->submit();

        ExperimentalUGLIRRuntimeRenderUniformFrameBuffer framebuffer;
        framebuffer.color = renderTargetView;
        framebuffer.color.loadOp = GVM::RHI::LoadOp::Clear;
        framebuffer.color.storeOp = GVM::RHI::StoreOp::Store;
        framebuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        queue
            ->renderPass("UGLIRRenderUniformPass", framebuffer, renderPassClass->run(3u, 1u, 0u, 0u))
            ->submit();

        std::array<uint8_t, kReadbackByteSize> readback = {};
        queue
            ->readTexture(renderTarget, readback.data(), readback.size())
            ->submit();

        const size_t centerPixel = ((kHeight / 2u) * kWidth + (kWidth / 2u)) * kBytesPerPixel;
        EXPECT_NEAR(static_cast<int>(readback[centerPixel + 0u]), 64, 2);
        EXPECT_NEAR(static_cast<int>(readback[centerPixel + 1u]), 128, 2);
        EXPECT_NEAR(static_cast<int>(readback[centerPixel + 2u]), 191, 2);
        EXPECT_EQ(readback[centerPixel + 3u], 255u);

        device->freeTexture(renderTarget);
        device->freeBuffer(paramsBuffer);
    }
} // namespace
