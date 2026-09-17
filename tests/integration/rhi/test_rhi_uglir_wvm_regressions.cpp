#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMTestCommon.hpp"
#include "generate_result.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace
{
    /** Returns the lowercase backend name used in skip and failure diagnostics. */
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

    /** Reads one generated shader debug artifact from the current DSL output directory. */
    std::string readGeneratedDebugArtifact(const std::filesystem::path &relativePath)
    {
        return GVM::Tests::readTextFile(GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR") / relativePath);
    }

    /** Returns true when the generated host header keeps the experimental HLSL payload empty. */
    bool generatedHeaderHasEmptyHLSLPayload()
    {
        const std::string generatedHeader = readGeneratedDebugArtifact("generate_result.hpp");
        return generatedHeader.find("eastl::string{},") != std::string::npos;
    }

    /** Owns a headless device for WVM-inspired UGLIR regression tests. */
    class RhiUGLIRWVMRegressionTest : public ::testing::Test
    {
    protected:
        /** Creates a Metal or Vulkan device before each regression test. */
        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);

            const auto backend = instance->getBackend();
            if (backend != GVM::RHI::GraphicsBackend::Metal && backend != GVM::RHI::GraphicsBackend::Vulkan)
            {
                GTEST_SKIP() << "Experimental UGLIR WVM regression tests require Metal or Vulkan backend, got " << backendName(backend);
            }

            device = instance->createDevice();
            ASSERT_NE(device, nullptr);
            ASSERT_NE(device->getMainQueue(), nullptr);
        }

        /** Releases the test device and instance after each regression test. */
        void TearDown() override
        {
            device = nullptr;
            if (instance != nullptr)
            {
                GVM::RHI::destroyInstance(instance);
                instance = nullptr;
            }
        }

        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device device = nullptr;
    };

    TEST_F(RhiUGLIRWVMRegressionTest, DispatchesComputeIntrinsicsAndResourceAliases)
    {
        ASSERT_TRUE(generatedHeaderHasEmptyHLSLPayload());
        EXPECT_FALSE(std::filesystem::exists(GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR") / "hlsl"));

        const std::string rawComputeSPV = readGeneratedDebugArtifact("spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm");
        const std::string optimizedComputeSPV = readGeneratedDebugArtifact("spv/ExperimentalUGLIRWVMRegressionComputePass.spvasm");
        EXPECT_NE(rawComputeSPV.find(" Sin "), std::string::npos);
        EXPECT_NE(rawComputeSPV.find(" Cos "), std::string::npos);
        EXPECT_NE(rawComputeSPV.find("OpStore"), std::string::npos);
        EXPECT_NE(rawComputeSPV.find("OpAll"), std::string::npos);
        EXPECT_NE(rawComputeSPV.find("FSign"), std::string::npos);
        EXPECT_NE(rawComputeSPV.find("RoundEven"), std::string::npos);
        EXPECT_NE(rawComputeSPV.find("OpCapability Float16"), std::string::npos);
        EXPECT_NE(rawComputeSPV.find("OpTypeFloat 16"), std::string::npos);
        EXPECT_NE(rawComputeSPV.find("experimentalUGLIRWVMRegressionAutoNttpMarker__I1__M"), std::string::npos);
        EXPECT_NE(rawComputeSPV.find("ExperimentalUGLIRWVMRegressionAutoRecord__I1__M"), std::string::npos);
        EXPECT_FALSE(optimizedComputeSPV.empty());

        constexpr uint64_t kInputByteSize = sizeof(uint32_t) * 4u;
        constexpr uint64_t kOutputByteSize = sizeof(uint32_t) * 122u;
        const std::array<uint32_t, 4u> valuesA = {1u, 2u, 3u, 4u};
        const std::array<uint32_t, 4u> valuesB = {10u, 20u, 30u, 40u};
        std::array<uint32_t, 122u> output = {};
        output.fill(0xDEADBEEFu);

        auto valuesABuffer = device->createBuffer({
            .label = "UGLIRWVMValuesA",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = kInputByteSize,
        });
        auto valuesBBuffer = device->createBuffer({
            .label = "UGLIRWVMValuesB",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = kInputByteSize,
        });
        auto outputBuffer = device->createBuffer({
            .label = "UGLIRWVMOutput",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
            .size = kOutputByteSize,
        });
        ASSERT_FALSE(valuesABuffer.isNull());
        ASSERT_FALSE(valuesBBuffer.isNull());
        ASSERT_FALSE(outputBuffer.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue
            ->writeBuffer(GVM::RHI::BufferRange(valuesABuffer, 0u, kInputByteSize), valuesA.data(), kInputByteSize)
            ->writeBuffer(GVM::RHI::BufferRange(valuesBBuffer, 0u, kInputByteSize), valuesB.data(), kInputByteSize)
            ->writeBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, kOutputByteSize), output.data(), kOutputByteSize)
            ->submit();

        auto bindGroup = deviceProxy->createBindGroup<ExperimentalUGLIRWVMRegressionComputeBindGroup>(
            GVM::RHI::BufferRange(valuesABuffer, 0u, kInputByteSize),
            GVM::RHI::BufferRange(valuesBBuffer, 0u, kInputByteSize),
            GVM::RHI::BufferRange(outputBuffer, 0u, kOutputByteSize));
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        auto computePass = deviceProxy->createComputeClass<ExperimentalUGLIRWVMRegressionComputePass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(computePass));

        queue
            ->computePass("ExperimentalUGLIRWVMRegressionComputePass", computePass->run(1u, 1u, 1u))
            ->submit();

        queue
            ->readBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, kOutputByteSize), output.data(), kOutputByteSize)
            ->submit();

        EXPECT_EQ(output[0], 10u);
        EXPECT_EQ(output[1], 1u);
        EXPECT_EQ(output[2], 7u);
        EXPECT_EQ(output[3], 30u);
        EXPECT_EQ(output[4], 12u);
        EXPECT_EQ(output[5], 2u);
        EXPECT_EQ(output[6], 1u);
        EXPECT_EQ(output[7], 13u);
        EXPECT_EQ(output[8], 24u);
        EXPECT_EQ(output[9], 50u);
        EXPECT_EQ(output[10], 5u);
        EXPECT_EQ(output[11], 4u);
        EXPECT_EQ(output[12], 6u);
        EXPECT_EQ(output[13], 10u);
        EXPECT_EQ(output[14], 9u);
        EXPECT_EQ(output[15], 0u);
        EXPECT_EQ(output[16], 9u);
        EXPECT_EQ(output[17], 3u);
        EXPECT_EQ(output[18], 0u);
        EXPECT_EQ(output[19], 0u);
        EXPECT_EQ(output[20], 0u);
        EXPECT_EQ(output[21], 0u);
        EXPECT_EQ(output[22], 3u);
        EXPECT_EQ(output[23], 8u);
        EXPECT_EQ(output[24], 16777216u);
        EXPECT_EQ(output[25], 2147483644u);
        EXPECT_EQ(output[26], 11u);
        EXPECT_EQ(output[27], 33u);
        EXPECT_EQ(output[28], 41u);
        EXPECT_EQ(output[29], 1024u);
        EXPECT_EQ(output[30], 1024u);
        EXPECT_EQ(output[31], 15u);
        EXPECT_EQ(output[32], 2u);
        EXPECT_EQ(output[33], 8u);
        EXPECT_EQ(output[34], 10u);
        EXPECT_EQ(output[35], 6u);
        EXPECT_EQ(output[36], 6u);
        EXPECT_EQ(output[37], 40u);
        EXPECT_EQ(output[38], 6u);
        EXPECT_EQ(output[39], 37u);
        EXPECT_EQ(output[40], 39u);
        EXPECT_EQ(output[41], 38u);
        EXPECT_EQ(output[42], 40u);
        EXPECT_EQ(output[43], 11u);
        EXPECT_EQ(output[44], 11u);
        EXPECT_EQ(output[45], 8u);
        EXPECT_EQ(output[46], 11u);
        EXPECT_EQ(output[47], 4097u);
        EXPECT_EQ(output[48], 7u);
        EXPECT_EQ(output[49], 3u);
        EXPECT_EQ(output[50], 7u);
        EXPECT_EQ(output[51], 3u);
        EXPECT_EQ(output[52], 1u);
        EXPECT_EQ(output[53], 2u);
        EXPECT_EQ(output[54], 3u);
        EXPECT_EQ(output[55], 17u);
        EXPECT_EQ(output[56], 23u);
        EXPECT_EQ(output[57], 3u);
        EXPECT_EQ(output[58], 3u);
        EXPECT_EQ(output[59], 3u);
        EXPECT_EQ(output[60], 3u);
        EXPECT_EQ(output[61], 0u);
        EXPECT_EQ(output[62], 0u);
        EXPECT_EQ(output[63], 3u);
        EXPECT_EQ(output[64], 1u);
        EXPECT_EQ(output[65], 3u);
        EXPECT_EQ(output[66], 3u);
        EXPECT_EQ(output[67], 3u);
        EXPECT_EQ(output[68], 3u);
        EXPECT_EQ(output[69], 1u);
        EXPECT_EQ(output[70], 1u);
        EXPECT_EQ(output[71], 1u);
        EXPECT_EQ(output[72], 1u);
        EXPECT_EQ(output[73], 1u);
        EXPECT_EQ(output[74], 2u);
        EXPECT_EQ(output[75], 3u);
        EXPECT_EQ(output[76], 4u);
        EXPECT_EQ(output[77], 7u);
        EXPECT_EQ(output[78], 10u);
        EXPECT_EQ(output[79], 5u);
        EXPECT_EQ(output[80], 11u);
        EXPECT_EQ(output[81], 2u);
        EXPECT_EQ(output[82], 0u);
        EXPECT_EQ(output[83], 0u);
        EXPECT_EQ(output[84], 2u);
        EXPECT_EQ(output[85], 10u);
        EXPECT_EQ(output[86], 14u);
        EXPECT_EQ(output[87], 0u);
        EXPECT_EQ(output[88], 0u);
        EXPECT_EQ(output[89], 2u);
        EXPECT_EQ(output[90], 3u);
        EXPECT_EQ(output[91], 6u);
        EXPECT_EQ(output[92], 22u);
        EXPECT_EQ(output[93], 28u);
        EXPECT_EQ(output[94], 23u);
        EXPECT_EQ(output[95], 34u);
        EXPECT_EQ(output[96], 31u);
        EXPECT_EQ(output[97], 46u);
        EXPECT_EQ(output[98], 3u);
        EXPECT_EQ(output[99], 4u);
        EXPECT_EQ(output[100], 5u);
        EXPECT_EQ(output[101], 6u);
        EXPECT_EQ(output[102], 3u);
        EXPECT_EQ(output[103], 4u);
        EXPECT_EQ(output[104], 2u);
        // Strict binary16 intermediates produce zero. Permitted Vulkan contraction retains
        // the 2^-20 product residual (scaled to one); reassociating (2048 + 1) - 2048
        // also produces one. These are the only accepted alternatives, not a broad tolerance.
        const uint32_t maximumCancellation = instance->getBackend() == GVM::RHI::GraphicsBackend::Vulkan ? 1u : 0u;
        for (uint32_t index = 105u; index <= 113u; ++index)
        {
            EXPECT_LE(output[index], maximumCancellation) << "half cancellation output index=" << index;
        }
        EXPECT_EQ(output[114], 1u);
        EXPECT_LE(output[115], maximumCancellation);
        EXPECT_EQ(output[116], 1u);
        // Binary16 rounding of 5/3 is 1707/1024; 2^-15 is an exact half subnormal.
        EXPECT_EQ(output[117], 1707u);
        EXPECT_EQ(output[118], 1u);
        EXPECT_EQ(output[119], 2048u);
        EXPECT_EQ(output[120], 4u);
        EXPECT_EQ(output[121], 4u);


        device->freeBuffer(outputBuffer);
        device->freeBuffer(valuesBBuffer);
        device->freeBuffer(valuesABuffer);
    }

    TEST_F(RhiUGLIRWVMRegressionTest, PreservesBooleanAggregateFields)
    {
        const std::array<uint32_t, 2> input = {0u, 1u};
        std::array<uint32_t, 4> output = {};
        auto inputBuffer = device->createBuffer({
            .label = "BooleanAggregateInput",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(input),
        });
        auto outputBuffer = device->createBuffer({
            .label = "BooleanAggregateOutput",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc,
            .size = sizeof(output),
        });
        ASSERT_FALSE(inputBuffer.isNull());
        ASSERT_FALSE(outputBuffer.isNull());
        GVM::Core::DeviceProxy proxy(device);
        auto queue = proxy->graphicsQueue(0);
        queue->writeBuffer(GVM::RHI::BufferRange(inputBuffer, 0u, sizeof(input)), input.data(), sizeof(input))->submit();
        auto bindings = proxy->createBindGroup<ExperimentalUGLIRWVMRegressionComputeBindGroup>(
            GVM::RHI::BufferRange(inputBuffer, 0u, sizeof(input)),
            GVM::RHI::BufferRange(inputBuffer, 0u, sizeof(input)),
            GVM::RHI::BufferRange(outputBuffer, 0u, sizeof(output)));
        auto pass = proxy->createComputeClass<ExperimentalUGLIRBooleanAggregatePass>(bindings);
        queue->computePass("BooleanAggregate", pass->run(1u, 1u, 1u))->submit();
        queue->readBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, sizeof(output)), output.data(), sizeof(output))->submit();
        EXPECT_EQ(output[0], 0u);
        EXPECT_EQ(output[1], 1u);
        EXPECT_EQ(output[2], 1u);
        EXPECT_EQ(output[3], 0u);
        device->freeBuffer(outputBuffer);
        device->freeBuffer(inputBuffer);
    }

    TEST_F(RhiUGLIRWVMRegressionTest, RendersExplicitTextureSamplingAndDerivatives)
    {
        ASSERT_TRUE(generatedHeaderHasEmptyHLSLPayload());
        EXPECT_FALSE(std::filesystem::exists(GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR") / "hlsl"));

        const std::string rawFragmentSPV = readGeneratedDebugArtifact("spv/ExperimentalUGLIRWVMRegressionTexturePass__fragment.raw.spvasm");
        const std::string optimizedFragmentSPV = readGeneratedDebugArtifact("spv/ExperimentalUGLIRWVMRegressionTexturePass__fragment.spvasm");
        const std::string fragmentMSL = readGeneratedDebugArtifact("msl/ExperimentalUGLIRWVMRegressionTexturePass__fragment.msl");
        EXPECT_NE(rawFragmentSPV.find("OpDPdx"), std::string::npos);
        EXPECT_NE(rawFragmentSPV.find("OpDPdy"), std::string::npos);
        EXPECT_NE(rawFragmentSPV.find("OpImageSampleExplicitLod"), std::string::npos);
        EXPECT_NE(rawFragmentSPV.find("Lod"), std::string::npos);
        EXPECT_NE(rawFragmentSPV.find("Grad"), std::string::npos);
        EXPECT_FALSE(optimizedFragmentSPV.empty());
        EXPECT_NE(fragmentMSL.find("dfdx"), std::string::npos);
        EXPECT_NE(fragmentMSL.find("dfdy"), std::string::npos);
        EXPECT_NE(fragmentMSL.find("level("), std::string::npos);
        EXPECT_NE(fragmentMSL.find("gradient2d"), std::string::npos);

        constexpr uint32_t kSourceWidth = 2u;
        constexpr uint32_t kSourceHeight = 2u;
        constexpr uint32_t kTargetWidth = 4u;
        constexpr uint32_t kTargetHeight = 4u;
        constexpr uint32_t kBytesPerPixel = 4u;
        constexpr uint64_t kReadbackByteSize = kTargetWidth * kTargetHeight * kBytesPerPixel;
        const std::array<uint8_t, kSourceWidth * kSourceHeight * kBytesPerPixel> sourcePixels = {
            32u, 64u, 96u, 255u,
            192u, 48u, 80u, 255u,
            64u, 180u, 48u, 255u,
            220u, 220u, 120u, 255u,
        };

        auto sourceTexture = device->createTexture({
            .label = "UGLIRWVMSourceTexture",
            .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
            .size = {kSourceWidth, kSourceHeight, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        auto renderTarget = device->createTexture({
            .label = "UGLIRWVMRenderTarget",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
            .size = {kTargetWidth, kTargetHeight, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        auto sampler = device->createSampler({
            .label = "UGLIRWVMSampler",
            .addressModeU = GVM::RHI::AddressMode::ClampToEdge,
            .addressModeV = GVM::RHI::AddressMode::ClampToEdge,
            .addressModeW = GVM::RHI::AddressMode::ClampToEdge,
            .magFilter = GVM::RHI::FilterMode::Linear,
            .minFilter = GVM::RHI::FilterMode::Linear,
            .mipmapFilter = GVM::RHI::MipmapFilterMode::Linear,
            .lodMinClamp = 0.0,
            .lodMaxClamp = 1.0,
            .maxAnisotropy = 1,
        });
        ASSERT_FALSE(sourceTexture.isNull());
        ASSERT_FALSE(renderTarget.isNull());
        ASSERT_FALSE(sampler.isNull());

        auto sourceView = sourceTexture->createView();
        auto renderTargetView = renderTarget->createView();
        ASSERT_FALSE(sourceView.isNull());
        ASSERT_FALSE(renderTargetView.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue
            ->writeTexture(sourceTexture, sourcePixels.data(), sourcePixels.size())
            ->submit();

        auto bindGroup = deviceProxy->createBindGroup<ExperimentalUGLIRWVMRegressionTextureBindGroup>(sourceView, sampler);
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        auto renderPassClass = deviceProxy->createRenderClass<ExperimentalUGLIRWVMRegressionTexturePass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(renderPassClass));

        ExperimentalUGLIRWVMRegressionFrameBuffer framebuffer;
        framebuffer.color = renderTargetView;
        framebuffer.color.loadOp = GVM::RHI::LoadOp::Clear;
        framebuffer.color.storeOp = GVM::RHI::StoreOp::Store;
        framebuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        queue
            ->renderPass("ExperimentalUGLIRWVMRegressionTexturePass", framebuffer, renderPassClass->run(3u, 1u, 0u, 0u))
            ->submit();

        std::array<uint8_t, kReadbackByteSize> readback = {};
        queue
            ->readTexture(renderTarget, readback.data(), readback.size())
            ->submit();

        bool hasNonBlackPixel = false;
        bool hasDifferentColor = false;
        const std::array<uint8_t, 4u> firstPixel = {readback[0], readback[1], readback[2], readback[3]};
        for (uint32_t pixelIndex = 0u; pixelIndex < kTargetWidth * kTargetHeight; ++pixelIndex)
        {
            const uint32_t byteIndex = pixelIndex * kBytesPerPixel;
            hasNonBlackPixel = hasNonBlackPixel || readback[byteIndex] > 0u || readback[byteIndex + 1u] > 0u || readback[byteIndex + 2u] > 0u;
            hasDifferentColor = hasDifferentColor ||
                                readback[byteIndex] != firstPixel[0] ||
                                readback[byteIndex + 1u] != firstPixel[1] ||
                                readback[byteIndex + 2u] != firstPixel[2];
            EXPECT_EQ(readback[byteIndex + 3u], 255u);
        }
        EXPECT_TRUE(hasNonBlackPixel);
        EXPECT_TRUE(hasDifferentColor);

        device->freeTexture(renderTarget);
        device->freeTexture(sourceTexture);
        device->freeSampler(sampler);
    }
} // namespace
