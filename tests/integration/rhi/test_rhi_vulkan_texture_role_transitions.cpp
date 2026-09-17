#include <gtest/gtest.h>

#include <GVMRHI/GVMRHI.hpp>
#include "GVMCore/Private/GDeviceProxy.hpp"
#include "GVMTestCommon.hpp"
#include "generate_result.hpp"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 2u;
    constexpr uint32_t kHeight = 2u;
    constexpr uint64_t kByteSize = static_cast<uint64_t>(kWidth) * kHeight * 4u;

    /** Builds the source texel pattern used to verify sampled-to-storage copies. */
    std::vector<std::uint8_t> makeInitialBytes()
    {
        return {
            255u, 255u, 0u, 255u,
            0u, 255u, 255u, 255u,
            255u, 0u, 255u, 255u,
            0u, 0u, 0u, 255u,
        };
    }

    /** Builds the exact RGBA8 pattern written by WritePatternPass. */
    std::vector<std::uint8_t> makePatternBytes()
    {
        return {
            255u, 0u, 0u, 255u,
            0u, 255u, 0u, 255u,
            0u, 0u, 255u, 255u,
            255u, 255u, 255u, 255u,
        };
    }

    /** Creates a 2D texture with all usages needed by the sampled/storage transition tests. */
    GVM::RHI::Texture createTransitionTexture(GVM::RHI::Device device, const char *label)
    {
        return device->createTexture({
            .label = label,
            .usage = GVM::RHI::TextureUsage::TextureBinding |
                     GVM::RHI::TextureUsage::StorageBinding |
                     GVM::RHI::TextureUsage::CopyDst |
                     GVM::RHI::TextureUsage::CopySrc,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {kWidth, kHeight, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1u,
            .arrayLayerCount = 1u,
        });
    }

    /** Creates the single-mip 2D view used by both sampled and storage bind group layouts. */
    GVM::RHI::TextureView createTransitionTextureView(GVM::RHI::Texture texture, const char *label)
    {
        GVM::RHI::TextureViewDescriptor descriptor = {};
        descriptor.label = label;
        descriptor.format = GVM::RHI::TextureFormat::RGBA8Unorm;
        descriptor.dimension = GVM::RHI::TextureViewDimension::e2D;
        descriptor.baseMipLevel = 0u;
        descriptor.mipLevelCount = 1u;
        descriptor.baseArrayLayer = 0u;
        descriptor.arrayLayerCount = 1u;
        return texture->createView(descriptor);
    }

    /** Owns a headless device for texture visibility checks and backend-specific conflict diagnostics. */
    class RhiVulkanTextureRoleTransitionTest : public ::testing::Test
    {
    protected:
        /** Creates the selected backend so valid producer/consumer transitions execute on both platforms. */
        void SetUp() override
        {
            instance = GVM::Tests::createTestInstance();
            ASSERT_NE(instance, nullptr);
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

    TEST_F(
        RhiVulkanTextureRoleTransitionTest,
        MakesRenderAttachmentWritesVisibleToVertexTextureReadsWithinOneSubmission)
    {
        auto producerTexture = device->createTexture({
            .label = "RenderAttachmentVertexSampleProducer",
            .usage = GVM::RHI::TextureUsage::TextureBinding |
                     GVM::RHI::TextureUsage::RenderAttachment |
                     GVM::RHI::TextureUsage::CopyDst,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {kWidth, kHeight, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1u,
            .arrayLayerCount = 1u,
        });
        auto outputTexture = device->createTexture({
            .label = "RenderAttachmentVertexSampleOutput",
            .usage = GVM::RHI::TextureUsage::RenderAttachment |
                     GVM::RHI::TextureUsage::CopySrc,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {kWidth, kHeight, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1u,
            .arrayLayerCount = 1u,
        });
        ASSERT_FALSE(producerTexture.isNull());
        ASSERT_FALSE(outputTexture.isNull());

        auto producerView = createTransitionTextureView(
            producerTexture,
            "RenderAttachmentVertexSampleProducerView");
        auto outputView = createTransitionTextureView(
            outputTexture,
            "RenderAttachmentVertexSampleOutputView");
        ASSERT_FALSE(producerView.isNull());
        ASSERT_FALSE(outputView.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto sampledProducerGroup =
            deviceProxy->createBindGroup<
                VulkanTextureRoleTransitionTest::
                    RenderAttachmentTransitionBindGroup>(producerView);
        auto producerPass =
            deviceProxy->createRenderClass<
                VulkanTextureRoleTransitionTest::
                    RenderAttachmentWritePass>();
        auto consumerPass =
            deviceProxy->createRenderClass<
                VulkanTextureRoleTransitionTest::
                    VertexSampledTextureReadPass>(sampledProducerGroup);
        ASSERT_TRUE(static_cast<bool>(sampledProducerGroup));
        ASSERT_TRUE(static_cast<bool>(producerPass));
        ASSERT_TRUE(static_cast<bool>(consumerPass));

        std::vector<std::uint8_t> initialProducerBytes(kByteSize, 0u);
        for (uint32_t pixelIndex = 0u;
             pixelIndex < kWidth * kHeight;
             ++pixelIndex)
        {
            initialProducerBytes[pixelIndex * 4u + 3u] = 255u;
        }

        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue
            ->writeTexture(
                producerTexture,
                initialProducerBytes.data(),
                initialProducerBytes.size())
            ->submit();

        VulkanTextureRoleTransitionTest::
            RenderAttachmentTransitionFrameBuffer producerFramebuffer;
        producerFramebuffer.color = producerView;
        producerFramebuffer.color.loadOp = GVM::RHI::LoadOp::Clear;
        producerFramebuffer.color.storeOp = GVM::RHI::StoreOp::Store;
        producerFramebuffer.color.clearValue = {0.0, 0.0, 0.0, 1.0};

        VulkanTextureRoleTransitionTest::
            RenderAttachmentTransitionFrameBuffer outputFramebuffer;
        outputFramebuffer.color = outputView;
        outputFramebuffer.color.loadOp = GVM::RHI::LoadOp::Clear;
        outputFramebuffer.color.storeOp = GVM::RHI::StoreOp::Store;
        outputFramebuffer.color.clearValue = {0.0, 0.0, 1.0, 1.0};

        queue
            ->renderPass(
                "RenderAttachmentVertexSampleProducer",
                producerFramebuffer,
                producerPass->run(3u, 1u, 0u, 0u))
            ->renderPass(
                "RenderAttachmentVertexSampleConsumer",
                outputFramebuffer,
                consumerPass->run(3u, 1u, 0u, 0u))
            ->submit();

        std::vector<std::uint8_t> outputBytes(kByteSize, 0u);
        queue
            ->readTexture(
                outputTexture,
                outputBytes.data(),
                outputBytes.size())
            ->submit();

        for (uint32_t pixelIndex = 0u;
             pixelIndex < kWidth * kHeight;
             ++pixelIndex)
        {
            const uint32_t byteIndex = pixelIndex * 4u;
            EXPECT_EQ(outputBytes[byteIndex], 0u);
            EXPECT_EQ(outputBytes[byteIndex + 1u], 255u);
            EXPECT_EQ(outputBytes[byteIndex + 2u], 0u);
            EXPECT_EQ(outputBytes[byteIndex + 3u], 255u);
        }

        consumerPass = nullptr;
        producerPass = nullptr;
        sampledProducerGroup = nullptr;
        device->freeTexture(outputTexture);
        device->freeTexture(producerTexture);
    }

    TEST_F(RhiVulkanTextureRoleTransitionTest, AllowsSampledThenStorageUseAcrossOrderedDispatches)
    {
        auto sharedTexture = createTransitionTexture(device, "TextureRoleSharedSampledThenStorage");
        auto outputTexture = createTransitionTexture(device, "TextureRoleOutputSampledThenStorage");
        ASSERT_FALSE(sharedTexture.isNull());
        ASSERT_FALSE(outputTexture.isNull());

        auto sharedView = createTransitionTextureView(sharedTexture, "TextureRoleSharedSampledThenStorageView");
        auto outputView = createTransitionTextureView(outputTexture, "TextureRoleOutputSampledThenStorageView");
        ASSERT_FALSE(sharedView.isNull());
        ASSERT_FALSE(outputView.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto sampledSharedGroup = deviceProxy->createBindGroup<VulkanTextureRoleTransitionTest::SampledTextureBindGroup>(sharedView);
        auto storageSharedGroup = deviceProxy->createBindGroup<VulkanTextureRoleTransitionTest::StorageTextureBindGroup>(sharedView);
        auto storageOutputGroup = deviceProxy->createBindGroup<VulkanTextureRoleTransitionTest::StorageTextureBindGroup>(outputView);
        ASSERT_TRUE(static_cast<bool>(sampledSharedGroup));
        ASSERT_TRUE(static_cast<bool>(storageSharedGroup));
        ASSERT_TRUE(static_cast<bool>(storageOutputGroup));

        auto copyPass = deviceProxy->createComputeClass<VulkanTextureRoleTransitionTest::SampledToStoragePass>(sampledSharedGroup, storageOutputGroup);
        auto writePass = deviceProxy->createComputeClass<VulkanTextureRoleTransitionTest::WritePatternPass>(storageSharedGroup);
        ASSERT_TRUE(static_cast<bool>(copyPass));
        ASSERT_TRUE(static_cast<bool>(writePass));

        std::vector<std::uint8_t> outputBytes(kByteSize, 0u);
        std::vector<std::uint8_t> sharedBytes(kByteSize, 0u);
        const std::vector<std::uint8_t> initialBytes = makeInitialBytes();
        const std::vector<std::uint8_t> patternBytes = makePatternBytes();

        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->writeTexture(sharedTexture, initialBytes.data(), initialBytes.size())->submit();
        queue->computePass("TextureRoleSampledThenStorage", copyPass->run(kWidth, kHeight, 1u), writePass->run(kWidth, kHeight, 1u))->submit();
        queue->readTexture(outputTexture, outputBytes.data(), outputBytes.size())->submit();
        queue->readTexture(sharedTexture, sharedBytes.data(), sharedBytes.size())->submit();

        EXPECT_EQ(outputBytes, initialBytes);
        EXPECT_EQ(sharedBytes, patternBytes);

        writePass = nullptr;
        copyPass = nullptr;
        storageOutputGroup = nullptr;
        storageSharedGroup = nullptr;
        sampledSharedGroup = nullptr;
        device->freeTexture(outputTexture);
        device->freeTexture(sharedTexture);
    }

    TEST_F(RhiVulkanTextureRoleTransitionTest, AllowsStorageThenSampledUseAcrossOrderedDispatches)
    {
        auto sharedTexture = createTransitionTexture(device, "TextureRoleSharedStorageThenSampled");
        auto outputTexture = createTransitionTexture(device, "TextureRoleOutputStorageThenSampled");
        ASSERT_FALSE(sharedTexture.isNull());
        ASSERT_FALSE(outputTexture.isNull());

        auto sharedView = createTransitionTextureView(sharedTexture, "TextureRoleSharedStorageThenSampledView");
        auto outputView = createTransitionTextureView(outputTexture, "TextureRoleOutputStorageThenSampledView");
        ASSERT_FALSE(sharedView.isNull());
        ASSERT_FALSE(outputView.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto storageSharedGroup = deviceProxy->createBindGroup<VulkanTextureRoleTransitionTest::StorageTextureBindGroup>(sharedView);
        auto sampledSharedGroup = deviceProxy->createBindGroup<VulkanTextureRoleTransitionTest::SampledTextureBindGroup>(sharedView);
        auto storageOutputGroup = deviceProxy->createBindGroup<VulkanTextureRoleTransitionTest::StorageTextureBindGroup>(outputView);
        ASSERT_TRUE(static_cast<bool>(storageSharedGroup));
        ASSERT_TRUE(static_cast<bool>(sampledSharedGroup));
        ASSERT_TRUE(static_cast<bool>(storageOutputGroup));

        auto writePass = deviceProxy->createComputeClass<VulkanTextureRoleTransitionTest::WritePatternPass>(storageSharedGroup);
        auto copyPass = deviceProxy->createComputeClass<VulkanTextureRoleTransitionTest::SampledToStoragePass>(sampledSharedGroup, storageOutputGroup);
        ASSERT_TRUE(static_cast<bool>(writePass));
        ASSERT_TRUE(static_cast<bool>(copyPass));

        std::vector<std::uint8_t> outputBytes(kByteSize, 0u);
        const std::vector<std::uint8_t> patternBytes = makePatternBytes();

        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        queue->computePass("TextureRoleStorageThenSampled", writePass->run(kWidth, kHeight, 1u), copyPass->run(kWidth, kHeight, 1u))->submit();
        queue->readTexture(outputTexture, outputBytes.data(), outputBytes.size())->submit();

        EXPECT_EQ(outputBytes, patternBytes);

        copyPass = nullptr;
        writePass = nullptr;
        storageOutputGroup = nullptr;
        sampledSharedGroup = nullptr;
        storageSharedGroup = nullptr;
        device->freeTexture(outputTexture);
        device->freeTexture(sharedTexture);
    }

    TEST_F(RhiVulkanTextureRoleTransitionTest, RejectsSameDispatchSampledAndStorageUseOfSameTexture)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
        {
            GTEST_SKIP() << "Same-dispatch layout conflict rejection is a Vulkan backend policy.";
        }
        auto sharedTexture = createTransitionTexture(device, "TextureRoleSameDispatchConflict");
        ASSERT_FALSE(sharedTexture.isNull());

        auto sharedView = createTransitionTextureView(sharedTexture, "TextureRoleSameDispatchConflictView");
        ASSERT_FALSE(sharedView.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto sampledSharedGroup = deviceProxy->createBindGroup<VulkanTextureRoleTransitionTest::SampledTextureBindGroup>(sharedView);
        auto storageSharedGroup = deviceProxy->createBindGroup<VulkanTextureRoleTransitionTest::StorageTextureBindGroup>(sharedView);
        ASSERT_TRUE(static_cast<bool>(sampledSharedGroup));
        ASSERT_TRUE(static_cast<bool>(storageSharedGroup));

        auto conflictPass = deviceProxy->createComputeClass<VulkanTextureRoleTransitionTest::SampledToStoragePass>(sampledSharedGroup, storageSharedGroup);
        ASSERT_TRUE(static_cast<bool>(conflictPass));

        auto queue = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queue));
        EXPECT_THROW(queue->computePass("TextureRoleSameDispatchConflict", conflictPass->run(kWidth, kHeight, 1u)), std::logic_error);

        conflictPass = nullptr;
        storageSharedGroup = nullptr;
        sampledSharedGroup = nullptr;
        device->freeTexture(sharedTexture);
    }

    TEST_F(RhiVulkanTextureRoleTransitionTest, RejectsSameBindGroupSampledAndStorageUseOfSameTexture)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
        {
            GTEST_SKIP() << "Same-bind-group layout conflict rejection is a Vulkan backend policy.";
        }
        auto sharedTexture = createTransitionTexture(device, "TextureRoleSameBindGroupConflict");
        ASSERT_FALSE(sharedTexture.isNull());

        auto sharedView = createTransitionTextureView(sharedTexture, "TextureRoleSameBindGroupConflictView");
        ASSERT_FALSE(sharedView.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        try
        {
            auto conflictGroup = deviceProxy->createBindGroup<VulkanTextureRoleTransitionTest::DualRoleTextureBindGroup>(sharedView, sharedView);
            (void)conflictGroup;
            FAIL() << "Expected same-bind-group sampled/storage texture conflict.";
        }
        catch (const std::exception &error)
        {
            const std::string message = error.what();
            EXPECT_NE(message.find("TextureRoleSameBindGroupConflict"), std::string::npos);
            EXPECT_NE(message.find("Sampled binding 0"), std::string::npos);
            EXPECT_NE(message.find("storage binding 1"), std::string::npos);
        }

        device->freeTexture(sharedTexture);
    }
} // namespace
