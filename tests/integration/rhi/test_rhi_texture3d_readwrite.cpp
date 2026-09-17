#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMTestCommon.hpp"
#include "generate_result.hpp"

#include <cstdint>
#include <vector>

namespace
{
    /**
     * Owns a headless RHI device for the Texture3D DSL runtime validation.
     * The same generated shader source is used on every backend selected by the test runner.
     */
    class RhiTexture3DReadWriteTest : public ::testing::Test
    {
    protected:
        /** Creates the test instance and skips unsupported backends before each test body runs. */
        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);

            const auto backend = instance->getBackend();
            if (backend != GVM::RHI::GraphicsBackend::Metal && backend != GVM::RHI::GraphicsBackend::Vulkan)
            {
                GTEST_SKIP() << "Texture3D runtime validation requires Metal or Vulkan backend.";
            }

            device = instance->createDevice();
            ASSERT_NE(device, nullptr);
            ASSERT_NE(device->getMainQueue(), nullptr);
        }

        /** Releases the RHI device and instance after each test body finishes. */
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

    TEST_F(RhiTexture3DReadWriteTest, DispatchesTexture3DReadIntoRWTexture3DAndReadsBackVolume)
    {
        constexpr uint32_t kWidth = 2u;
        constexpr uint32_t kHeight = 2u;
        constexpr uint32_t kDepth = 2u;
        constexpr uint64_t kByteSize = static_cast<uint64_t>(kWidth) * kHeight * kDepth * 4u;

        std::vector<std::uint8_t> inputBytes(kByteSize);
        for (uint64_t voxel = 0u; voxel < kWidth * kHeight * kDepth; ++voxel)
        {
            const uint64_t base = voxel * 4u;
            inputBytes[base + 0u] = (voxel & 1u) != 0u ? 255u : 0u;
            inputBytes[base + 1u] = (voxel & 2u) != 0u ? 255u : 0u;
            inputBytes[base + 2u] = (voxel & 4u) != 0u ? 255u : 0u;
            inputBytes[base + 3u] = 255u;
        }
        std::vector<std::uint8_t> outputBytes(kByteSize, 0u);

        auto sourceTexture = device->createTexture({
            .label = "Texture3DReadWriteSource",
            .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
            .dimension = GVM::RHI::TextureDimension::e3D,
            .size = {kWidth, kHeight, kDepth},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1u,
            .arrayLayerCount = 1u,
        });
        auto outputTexture = device->createTexture({
            .label = "Texture3DReadWriteOutput",
            .usage = GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::CopySrc,
            .dimension = GVM::RHI::TextureDimension::e3D,
            .size = {kWidth, kHeight, kDepth},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1u,
            .arrayLayerCount = 1u,
        });
        auto storageInputTexture = device->createTexture({
            .label = "Texture3DReadWriteStorageInput",
            .usage = GVM::RHI::TextureUsage::StorageBinding | GVM::RHI::TextureUsage::CopyDst,
            .dimension = GVM::RHI::TextureDimension::e3D,
            .size = {kWidth, kHeight, kDepth},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1u,
            .arrayLayerCount = 1u,
        });
        ASSERT_FALSE(sourceTexture.isNull());
        ASSERT_FALSE(storageInputTexture.isNull());
        ASSERT_FALSE(outputTexture.isNull());

        GVM::RHI::TextureViewDescriptor sourceViewDescriptor = {};
        sourceViewDescriptor.label = "Texture3DReadWriteSourceView";
        sourceViewDescriptor.format = GVM::RHI::TextureFormat::RGBA8Unorm;
        sourceViewDescriptor.dimension = GVM::RHI::TextureViewDimension::e3D;
        sourceViewDescriptor.baseMipLevel = 0u;
        sourceViewDescriptor.mipLevelCount = 1u;
        sourceViewDescriptor.baseArrayLayer = 0u;
        sourceViewDescriptor.arrayLayerCount = 1u;

        GVM::RHI::TextureViewDescriptor outputViewDescriptor = sourceViewDescriptor;
        outputViewDescriptor.label = "Texture3DReadWriteOutputView";
        GVM::RHI::TextureViewDescriptor storageInputViewDescriptor = sourceViewDescriptor;
        storageInputViewDescriptor.label = "Texture3DReadWriteStorageInputView";

        auto sourceView = sourceTexture->createView(sourceViewDescriptor);
        auto storageInputView = storageInputTexture->createView(storageInputViewDescriptor);
        auto outputView = outputTexture->createView(outputViewDescriptor);
        ASSERT_FALSE(sourceView.isNull());
        ASSERT_FALSE(storageInputView.isNull());
        ASSERT_FALSE(outputView.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto bindGroup = deviceProxy->createBindGroup<Texture3DReadWriteTest::Texture3DReadWriteBindGroup>(sourceView, storageInputView, outputView);
        ASSERT_TRUE(static_cast<bool>(bindGroup));
        auto pass = deviceProxy->createComputeClass<Texture3DReadWriteTest::Texture3DReadWritePass>(bindGroup);
        ASSERT_TRUE(static_cast<bool>(pass));

        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->writeTexture(sourceTexture, inputBytes.data(), inputBytes.size())->submit();
        queue->writeTexture(storageInputTexture, inputBytes.data(), inputBytes.size())->submit();
        queue->computePass("Texture3DReadWritePass", pass->run(kWidth, kHeight, kDepth))->submit();
        queue->readTexture(outputTexture, outputBytes.data(), outputBytes.size())->submit();

        EXPECT_EQ(outputBytes, inputBytes);

        pass = nullptr;
        bindGroup = nullptr;
        device->freeTexture(outputTexture);
        device->freeTexture(storageInputTexture);
        device->freeTexture(sourceTexture);
    }
} // namespace
