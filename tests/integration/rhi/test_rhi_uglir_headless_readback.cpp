#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMTestCommon.hpp"
#include "generate_result.hpp"

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

    /** Reads one generated debug artifact from the current DSL output directory. */
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

    /** Computes the CPU reference expected from the experimental headless readback shader. */
    std::array<uint32_t, 7u> makeExpectedOutput()
    {
        return {
            24u,
            22u,
            18u,
            16u,
            60u,
            117u,
            9u,
        };
    }

    /** Owns a headless Metal or Vulkan device for UGLIR readback validation. */
    class RhiUGLIRHeadlessReadbackTest : public ::testing::Test
    {
    protected:
        /** Creates a Metal or Vulkan device before each readback test. */
        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);

            const auto backend = instance->getBackend();
            if (backend != GVM::RHI::GraphicsBackend::Metal && backend != GVM::RHI::GraphicsBackend::Vulkan)
            {
                GTEST_SKIP() << "Experimental UGLIR headless readback requires Metal or Vulkan backend, got " << backendName(backend);
            }

            device = instance->createDevice();
            ASSERT_NE(device, nullptr);
            ASSERT_NE(device->getMainQueue(), nullptr);
        }

        /** Releases the test device and instance after each readback test. */
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

    TEST_F(RhiUGLIRHeadlessReadbackTest, DispatchesComputeReadbackAgainstCPUReference)
    {
        ASSERT_TRUE(generatedHeaderHasEmptyHLSLPayload());
        EXPECT_FALSE(std::filesystem::exists(GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR") / "hlsl"));

        const std::string rawSPIRV = readGeneratedDebugArtifact("spv/ExperimentalUGLIRHeadlessReadbackPass.raw.spvasm");
        const std::string optimizedSPIRV = readGeneratedDebugArtifact("spv/ExperimentalUGLIRHeadlessReadbackPass.spvasm");
        EXPECT_NE(rawSPIRV.find("OpAtomicIAdd"), std::string::npos);
        EXPECT_NE(rawSPIRV.find("OpControlBarrier"), std::string::npos);
        EXPECT_NE(rawSPIRV.find("FSign"), std::string::npos);
        EXPECT_NE(rawSPIRV.find("OpAll"), std::string::npos);
        EXPECT_NE(rawSPIRV.find(" Sin "), std::string::npos);
        EXPECT_NE(rawSPIRV.find(" Cos "), std::string::npos);
        EXPECT_NE(rawSPIRV.find("OpBitcast"), std::string::npos);
        EXPECT_FALSE(optimizedSPIRV.empty());

        constexpr uint64_t kInputByteSize = sizeof(uint32_t) * 4u;
        constexpr uint64_t kOutputByteSize = sizeof(uint32_t) * 7u;
        constexpr uint64_t kCounterByteSize = sizeof(uint32_t);
        const ExperimentalUGLIRHeadlessReadbackParams params{
            .addend = 5u,
            .selectSecondBuffer = 1u,
        };
        const std::array<uint32_t, 4u> valuesA = {2u, 4u, 6u, 8u};
        const std::array<uint32_t, 4u> valuesB = {11u, 13u, 17u, 19u};
        const uint32_t zeroCounter = 0u;

        auto paramsBuffer = device->createBuffer({
            .label = "UGLIRHeadlessParams",
            .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::CopyDst,
            .size = sizeof(params),
        });
        auto valuesABuffer = device->createBuffer({
            .label = "UGLIRHeadlessValuesA",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = kInputByteSize,
        });
        auto valuesBBuffer = device->createBuffer({
            .label = "UGLIRHeadlessValuesB",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = kInputByteSize,
        });
        auto outputBuffer = device->createBuffer({
            .label = "UGLIRHeadlessOutput",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc,
            .size = kOutputByteSize,
        });
        auto counterBuffer = device->createBuffer({
            .label = "UGLIRHeadlessCounter",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = kCounterByteSize,
        });
        ASSERT_FALSE(paramsBuffer.isNull());
        ASSERT_FALSE(valuesABuffer.isNull());
        ASSERT_FALSE(valuesBBuffer.isNull());
        ASSERT_FALSE(outputBuffer.isNull());
        ASSERT_FALSE(counterBuffer.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue
            ->writeBuffer(GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(params)), &params, sizeof(params))
            ->writeBuffer(GVM::RHI::BufferRange(valuesABuffer, 0u, kInputByteSize), valuesA.data(), kInputByteSize)
            ->writeBuffer(GVM::RHI::BufferRange(valuesBBuffer, 0u, kInputByteSize), valuesB.data(), kInputByteSize)
            ->writeBuffer(GVM::RHI::BufferRange(counterBuffer, 0u, kCounterByteSize), &zeroCounter, kCounterByteSize)
            ->submit();

        auto bindGroup = deviceProxy->createBindGroup<ExperimentalUGLIRHeadlessReadbackBindGroup>(
            GVM::RHI::BufferRange(paramsBuffer, 0u, sizeof(params)),
            GVM::RHI::BufferRange(valuesABuffer, 0u, kInputByteSize),
            GVM::RHI::BufferRange(valuesBBuffer, 0u, kInputByteSize),
            GVM::RHI::BufferRange(outputBuffer, 0u, kOutputByteSize),
            GVM::RHI::BufferRange(counterBuffer, 0u, kCounterByteSize));
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        auto pass = deviceProxy->createComputeClass<ExperimentalUGLIRHeadlessReadbackPass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(pass));

        queue
            ->computePass("ExperimentalUGLIRHeadlessReadbackPass", pass->run(4u, 1u, 1u))
            ->submit();

        std::array<uint32_t, 7u> output = {};
        queue
            ->readBuffer(GVM::RHI::BufferRange(outputBuffer, 0u, kOutputByteSize), output.data(), kOutputByteSize)
            ->submit();

        const std::array<uint32_t, 7u> expected = makeExpectedOutput();
        for (size_t index = 0u; index < expected.size(); ++index)
        {
            EXPECT_EQ(output[index], expected[index]) << "output[" << index << "]";
        }

        pass = nullptr;
        bindGroup = nullptr;
        device->freeBuffer(counterBuffer);
        device->freeBuffer(outputBuffer);
        device->freeBuffer(valuesBBuffer);
        device->freeBuffer(valuesABuffer);
        device->freeBuffer(paramsBuffer);
    }
} // namespace
