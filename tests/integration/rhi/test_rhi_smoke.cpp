#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>
#include <GVMCore/Private/GQueue.hpp>
#include <GVMRHI/GVMRHI.hpp>

#include <array>
#include <cstring>

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

    constexpr const char *kComputeShaderCode = R"(
#include <metal_stdlib>
using namespace metal;

kernel void test_main(uint3 gid [[thread_position_in_grid]])
{
}
)";

    class RhiSmokeTest : public ::testing::Test
    {
    protected:
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

        GVM::RHI::Instance instance = nullptr;
        GVM::RHI::Device device = nullptr;

        [[nodiscard]]
        bool inlineMetalShaderSupported() const
        {
            return instance != nullptr && instance->getBackend() == GVM::RHI::GraphicsBackend::Metal;
        }
    };

    struct BufferMapCase
    {
        const char *name;
        GVM::RHI::BufferUsageFlags usage;
        bool requiresWritePointer;
        bool requiresReadPointer;
    };

    /// Provides a one-color-attachment framebuffer descriptor for diagnostics overlay render tests.
    struct DiagnosticsOverlayRenderTarget
    {
        GVM::RHI::TextureView colorView;
        GVM::RHI::Color clearValue = {0.0, 0.0, 0.0, 1.0};

        /// Builds a render pass descriptor that clears the target before the overlay pass is appended.
        GVM::RHI::RenderPassDescriptor getRenderPassDescriptor() const
        {
            GVM::RHI::RenderPassDescriptor descriptor = {};
            descriptor.colorAttachments.push_back({
                .view = colorView,
                .loadOp = GVM::RHI::LoadOp::Clear,
                .storeOp = GVM::RHI::StoreOp::Store,
                .clearValue = clearValue,
            });
            return descriptor;
        }
    };

    /// Describes one diagnostics overlay visibility combination that should still draw text.
    struct DiagnosticsOverlayConfigCase
    {
        const char *name;
        bool showFps;
        bool showPassTimes;
        bool showResources;
    };

    /// Counts pixels with any color channel above the supplied threshold.
    uint32_t countBrightPixels(const uint8_t *data, size_t dataSize, uint32_t bytesPerPixel, uint8_t threshold)
    {
        uint32_t count = 0u;
        for (size_t byteIndex = 0u; byteIndex + 3u < dataSize; byteIndex += bytesPerPixel)
        {
            if (data[byteIndex + 0u] > threshold || data[byteIndex + 1u] > threshold || data[byteIndex + 2u] > threshold)
            {
                ++count;
            }
        }
        return count;
    }

    /// Counts pixels with all color channels below the supplied threshold.
    uint32_t countDarkPixels(const uint8_t *data, size_t dataSize, uint32_t bytesPerPixel, uint8_t threshold)
    {
        uint32_t count = 0u;
        for (size_t byteIndex = 0u; byteIndex + 3u < dataSize; byteIndex += bytesPerPixel)
        {
            if (data[byteIndex + 0u] < threshold && data[byteIndex + 1u] < threshold && data[byteIndex + 2u] < threshold)
            {
                ++count;
            }
        }
        return count;
    }

    /// Returns true when a diagnostics resource snapshot contains an entry with the requested label.
    bool snapshotContainsLabel(const GVM::RHI::DiagnosticsResourceSnapshot &snapshot, const char *label)
    {
        for (const GVM::RHI::DiagnosticsResourceSnapshotEntry &entry : snapshot.entries)
        {
            if (entry.label == label)
            {
                return true;
            }
        }
        return false;
    }

    /// Returns the first diagnostics resource snapshot entry matching the requested label.
    const GVM::RHI::DiagnosticsResourceSnapshotEntry *findSnapshotEntry(const GVM::RHI::DiagnosticsResourceSnapshot &snapshot, const char *label)
    {
        for (const GVM::RHI::DiagnosticsResourceSnapshotEntry &entry : snapshot.entries)
        {
            if (entry.label == label)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    TEST(RhiDiagnosticsOverlayTests, BackendDeviceExposesEnabledOverlayConfig)
    {
        GVM::RHI::InstanceDescriptor descriptor = GVM::Tests::makeTestInstanceDescriptor();
        descriptor.diagnosticsOverlay.enabled = GVM::RHI::True;
        descriptor.diagnosticsOverlay.showFps = GVM::RHI::True;
        descriptor.diagnosticsOverlay.showPassTimes = GVM::RHI::False;
        descriptor.diagnosticsOverlay.showResources = GVM::RHI::True;
        descriptor.diagnosticsOverlay.maxPassScopes = 17u;
        descriptor.diagnosticsOverlay.maxResourceRows = 19u;
        descriptor.diagnosticsOverlay.maxTextGlyphs = 23u;
        descriptor.diagnosticsOverlay.timestampReadbackLatencyFrames = 3u;

        GVM::RHI::Instance overlayInstance = GVM::RHI::createInstance(descriptor);
        ASSERT_NE(overlayInstance, nullptr);
        GVM::RHI::Device overlayDevice = overlayInstance->createDevice();
        ASSERT_NE(overlayDevice, nullptr);

        const GVM::RHI::RuntimeDiagnosticsOverlayConfig config = overlayDevice->getDiagnosticsOverlayConfig();
        EXPECT_EQ(config.enabled, GVM::RHI::True);
        EXPECT_EQ(config.showFps, GVM::RHI::True);
        EXPECT_EQ(config.showPassTimes, GVM::RHI::False);
        EXPECT_EQ(config.showResources, GVM::RHI::True);
        EXPECT_EQ(config.maxPassScopes, 17u);
        EXPECT_EQ(config.maxResourceRows, 19u);
        EXPECT_EQ(config.maxTextGlyphs, 23u);
        EXPECT_EQ(config.timestampReadbackLatencyFrames, 3u);

        GVM::RHI::destroyInstance(overlayInstance);
    }

    TEST(RhiDiagnosticsOverlayTests, QueueProxyEnabledOverlayWritesTextIntoRenderTarget)
    {
        constexpr uint32_t width = 256u;
        constexpr uint32_t height = 128u;
        constexpr uint32_t bytesPerPixel = 4u;

        GVM::RHI::InstanceDescriptor descriptor = GVM::Tests::makeTestInstanceDescriptor();
        descriptor.diagnosticsOverlay.enabled = GVM::RHI::True;
        descriptor.diagnosticsOverlay.showFps = GVM::RHI::True;
        descriptor.diagnosticsOverlay.showPassTimes = GVM::RHI::False;
        descriptor.diagnosticsOverlay.showResources = GVM::RHI::False;
        descriptor.diagnosticsOverlay.maxTextGlyphs = 512u;

        GVM::RHI::Instance overlayInstance = GVM::RHI::createInstance(descriptor);
        ASSERT_NE(overlayInstance, nullptr);
        GVM::RHI::Device overlayDevice = overlayInstance->createDevice();
        ASSERT_NE(overlayDevice, nullptr);

        GVM::RHI::Texture renderTarget = overlayDevice->createTexture({
            .label = "DiagnosticsOverlayReadbackTarget",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
            .size = {width, height, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        ASSERT_FALSE(renderTarget.isNull());
        GVM::RHI::TextureView renderTargetView = renderTarget->createView();
        ASSERT_FALSE(renderTargetView.isNull());

        {
            GVM::Core::QueueProxyImpl queue(overlayDevice, overlayDevice->getMainQueue());
            queue.renderPass("DiagnosticsOverlayTargetClear", DiagnosticsOverlayRenderTarget{.colorView = renderTargetView})->submit();
        }

        std::array<uint8_t, width * height * bytesPerPixel> readback = {};
        {
            GVM::Core::QueueProxyImpl readbackQueue(overlayDevice, overlayDevice->getMainQueue());
            readbackQueue.readTexture(renderTarget, readback.data(), readback.size())->submit();
        }

        EXPECT_GT(countBrightPixels(readback.data(), readback.size(), bytesPerPixel, 128u), 0u);

        overlayDevice->freeTexture(renderTarget);
        GVM::RHI::destroyInstance(overlayInstance);
    }

    TEST(RhiDiagnosticsOverlayTests, EnabledOverlayWritesTextForConfigCombinations)
    {
        constexpr uint32_t width = 256u;
        constexpr uint32_t height = 128u;
        constexpr uint32_t bytesPerPixel = 4u;

        const DiagnosticsOverlayConfigCase cases[] = {
            {"fps-only", true, false, false},
            {"pass-only", false, true, false},
            {"resources-only", false, false, true},
        };

        for (const DiagnosticsOverlayConfigCase &testCase : cases)
        {
            GVM::RHI::InstanceDescriptor descriptor = GVM::Tests::makeTestInstanceDescriptor();
            descriptor.diagnosticsOverlay.enabled = GVM::RHI::True;
            descriptor.diagnosticsOverlay.showFps = testCase.showFps ? GVM::RHI::True : GVM::RHI::False;
            descriptor.diagnosticsOverlay.showPassTimes = testCase.showPassTimes ? GVM::RHI::True : GVM::RHI::False;
            descriptor.diagnosticsOverlay.showResources = testCase.showResources ? GVM::RHI::True : GVM::RHI::False;
            descriptor.diagnosticsOverlay.maxTextGlyphs = 512u;

            GVM::RHI::Instance overlayInstance = GVM::RHI::createInstance(descriptor);
            ASSERT_NE(overlayInstance, nullptr) << testCase.name;
            GVM::RHI::Device overlayDevice = overlayInstance->createDevice();
            ASSERT_NE(overlayDevice, nullptr) << testCase.name;

            GVM::RHI::Texture renderTarget = overlayDevice->createTexture({
                .label = "DiagnosticsOverlayConfigTarget",
                .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
                .size = {width, height, 1u},
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            });
            ASSERT_FALSE(renderTarget.isNull()) << testCase.name;
            GVM::RHI::TextureView renderTargetView = renderTarget->createView();
            ASSERT_FALSE(renderTargetView.isNull()) << testCase.name;

            {
                GVM::Core::QueueProxyImpl queue(overlayDevice, overlayDevice->getMainQueue());
                queue.renderPass("DiagnosticsOverlayConfigClear", DiagnosticsOverlayRenderTarget{.colorView = renderTargetView})->submit();
            }

            std::array<uint8_t, width * height * bytesPerPixel> readback = {};
            {
                GVM::Core::QueueProxyImpl readbackQueue(overlayDevice, overlayDevice->getMainQueue());
                readbackQueue.readTexture(renderTarget, readback.data(), readback.size())->submit();
            }

            EXPECT_GT(countBrightPixels(readback.data(), readback.size(), bytesPerPixel, 128u), 0u) << testCase.name;

            overlayDevice->freeTexture(renderTarget);
            GVM::RHI::destroyInstance(overlayInstance);
        }
    }

    TEST(RhiDiagnosticsOverlayTests, EnabledOverlayDrawsReadableBackground)
    {
        constexpr uint32_t width = 256u;
        constexpr uint32_t height = 128u;
        constexpr uint32_t bytesPerPixel = 4u;

        GVM::RHI::InstanceDescriptor descriptor = GVM::Tests::makeTestInstanceDescriptor();
        descriptor.diagnosticsOverlay.enabled = GVM::RHI::True;
        descriptor.diagnosticsOverlay.showFps = GVM::RHI::True;
        descriptor.diagnosticsOverlay.showPassTimes = GVM::RHI::False;
        descriptor.diagnosticsOverlay.showResources = GVM::RHI::False;
        descriptor.diagnosticsOverlay.maxTextGlyphs = 512u;

        GVM::RHI::Instance overlayInstance = GVM::RHI::createInstance(descriptor);
        ASSERT_NE(overlayInstance, nullptr);
        GVM::RHI::Device overlayDevice = overlayInstance->createDevice();
        ASSERT_NE(overlayDevice, nullptr);

        GVM::RHI::Texture renderTarget = overlayDevice->createTexture({
            .label = "DiagnosticsOverlayBackgroundTarget",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
            .size = {width, height, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        ASSERT_FALSE(renderTarget.isNull());
        GVM::RHI::TextureView renderTargetView = renderTarget->createView();
        ASSERT_FALSE(renderTargetView.isNull());

        {
            GVM::Core::QueueProxyImpl queue(overlayDevice, overlayDevice->getMainQueue());
            queue.renderPass(
                     "DiagnosticsOverlayBackgroundClear",
                     DiagnosticsOverlayRenderTarget{
                         .colorView = renderTargetView,
                         .clearValue = {1.0, 1.0, 1.0, 1.0},
                     })
                ->submit();
        }

        std::array<uint8_t, width * height * bytesPerPixel> readback = {};
        {
            GVM::Core::QueueProxyImpl readbackQueue(overlayDevice, overlayDevice->getMainQueue());
            readbackQueue.readTexture(renderTarget, readback.data(), readback.size())->submit();
        }

        EXPECT_GT(countDarkPixels(readback.data(), readback.size(), bytesPerPixel, 220u), 0u);

        overlayDevice->freeTexture(renderTarget);
        GVM::RHI::destroyInstance(overlayInstance);
    }

    TEST_F(RhiSmokeTest, CreatesMappableBufferObject)
    {
        auto buffer = device->createBuffer({
            .label = "SmokeMappedBuffer",
            .usage = GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = sizeof(uint32_t) * 4,
        });
        ASSERT_FALSE(buffer.isNull());
        EXPECT_EQ(buffer->getStorageSize(), sizeof(uint32_t) * 4);

        device->freeBuffer(buffer);
    }

    TEST_F(RhiSmokeTest, DiagnosticsResourceSnapshotTracksBufferAndTextureLabels)
    {
        auto buffer = device->createBuffer({
            .label = "DiagnosticsSnapshotBuffer",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst,
            .size = 1024u,
        });
        ASSERT_FALSE(buffer.isNull());

        auto texture = device->createTexture({
            .label = "DiagnosticsSnapshotTexture",
            .usage = GVM::RHI::TextureUsage::RenderAttachment | GVM::RHI::TextureUsage::CopySrc,
            .size = {16u, 8u, 1u},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
        });
        ASSERT_FALSE(texture.isNull());

        const GVM::RHI::DiagnosticsResourceSnapshot snapshot = device->getDiagnosticsResourceSnapshot();
        const GVM::RHI::DiagnosticsResourceSnapshotEntry *bufferEntry = findSnapshotEntry(snapshot, "DiagnosticsSnapshotBuffer");
        const GVM::RHI::DiagnosticsResourceSnapshotEntry *textureEntry = findSnapshotEntry(snapshot, "DiagnosticsSnapshotTexture");
        ASSERT_NE(bufferEntry, nullptr);
        ASSERT_NE(textureEntry, nullptr);

        EXPECT_EQ(bufferEntry->kind, GVM::RHI::DiagnosticsResourceKind::Buffer);
        EXPECT_EQ(bufferEntry->estimatedBytes, 1024u);
        EXPECT_EQ(textureEntry->kind, GVM::RHI::DiagnosticsResourceKind::Texture);
        EXPECT_EQ(textureEntry->width, 16u);
        EXPECT_EQ(textureEntry->height, 8u);
        EXPECT_EQ(textureEntry->depth, 1u);
        EXPECT_EQ(textureEntry->format, GVM::RHI::TextureFormat::RGBA8Unorm);
        EXPECT_GE(textureEntry->estimatedBytes, 16u * 8u * 4u);
        EXPECT_GE(snapshot.totalEstimatedBytes, bufferEntry->estimatedBytes + textureEntry->estimatedBytes);

        device->freeBuffer(buffer);
        device->freeTexture(texture);

        const GVM::RHI::DiagnosticsResourceSnapshot afterFreeSnapshot = device->getDiagnosticsResourceSnapshot();
        EXPECT_FALSE(snapshotContainsLabel(afterFreeSnapshot, "DiagnosticsSnapshotBuffer"));
        EXPECT_FALSE(snapshotContainsLabel(afterFreeSnapshot, "DiagnosticsSnapshotTexture"));
    }

    TEST_F(RhiSmokeTest, Sample09StyleVertexBufferExposesDirectRangesInHeadlessMode)
    {
        struct VertexLike
        {
            float pos[4];
            float color[4];
        };

        auto buffer = device->createBuffer({
            .label = "SmokeSample09StyleVertexBuffer",
            .usage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = sizeof(VertexLike) * 3u,
        });
        ASSERT_FALSE(buffer.isNull());

        buffer->map();
        auto *writePtr = static_cast<VertexLike *>(buffer->getMappedRange(0u, sizeof(VertexLike) * 3u));
        ASSERT_NE(writePtr, nullptr);
        if (writePtr != nullptr)
        {
            std::memset(writePtr, 0, sizeof(VertexLike) * 3u);
            writePtr[0].pos[0] = 0.25f;
            writePtr[0].color[1] = 1.0f;
        }

        const auto *readPtr = static_cast<const VertexLike *>(buffer->getConstMappedRange(0u, sizeof(VertexLike) * 3u));
        ASSERT_NE(readPtr, nullptr);
        if (readPtr != nullptr)
        {
            EXPECT_FLOAT_EQ(readPtr[0].pos[0], 0.25f);
            EXPECT_FLOAT_EQ(readPtr[0].color[1], 1.0f);
        }
        buffer->unmap();

        device->freeBuffer(buffer);
    }

    TEST_F(RhiSmokeTest, MappableBufferExposesNonNullWriteAndReadRanges)
    {
        auto buffer = device->createBuffer({
            .label = "SmokeMappedPointerBuffer",
            .usage = GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
            .size = sizeof(uint32_t) * 4,
        });
        ASSERT_FALSE(buffer.isNull());

        buffer->map();
        auto *writePtr = static_cast<uint32_t *>(buffer->getMappedRange(0u, sizeof(uint32_t) * 4));
        ASSERT_NE(writePtr, nullptr);
        if (writePtr != nullptr)
        {
            writePtr[0] = 11u;
            writePtr[1] = 22u;
            writePtr[2] = 33u;
            writePtr[3] = 44u;
        }
        buffer->unmap();

        buffer->map();
        const auto *readPtr = static_cast<const uint32_t *>(buffer->getConstMappedRange(0u, sizeof(uint32_t) * 4));
        ASSERT_NE(readPtr, nullptr);
        if (readPtr != nullptr)
        {
            EXPECT_EQ(readPtr[0], 11u);
            EXPECT_EQ(readPtr[1], 22u);
            EXPECT_EQ(readPtr[2], 33u);
            EXPECT_EQ(readPtr[3], 44u);
        }
        buffer->unmap();

        device->freeBuffer(buffer);
    }

    TEST_F(RhiSmokeTest, UniformUploadBufferExposesNonNullWriteRange)
    {
        auto buffer = device->createBuffer({
            .label = "SmokeUniformUploadBuffer",
            .usage = GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::MapWrite,
            .size = sizeof(uint32_t) * 4,
        });
        ASSERT_FALSE(buffer.isNull());

        buffer->map();
        auto *writePtr = static_cast<uint32_t *>(buffer->getMappedRange(0u, sizeof(uint32_t) * 4));
        ASSERT_NE(writePtr, nullptr);
        if (writePtr != nullptr)
        {
            writePtr[0] = 101u;
            writePtr[1] = 202u;
            writePtr[2] = 303u;
            writePtr[3] = 404u;
        }
        buffer->unmap();

        device->freeBuffer(buffer);
    }

    TEST_F(RhiSmokeTest, Sample10StyleCompletionBufferSupportsCpuPollingAndResetThroughMappedRanges)
    {
        auto buffer = device->createBuffer({
            .label = "SmokeSample10CompletionBuffer",
            .usage = GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead,
            .size = sizeof(uint32_t),
        });
        ASSERT_FALSE(buffer.isNull());

        buffer->map();
        auto *writePtr = static_cast<volatile uint32_t *>(buffer->getMappedRange(0u, sizeof(uint32_t)));
        const auto *readPtr = static_cast<const volatile uint32_t *>(buffer->getConstMappedRange(0u, sizeof(uint32_t)));
        ASSERT_NE(writePtr, nullptr);
        ASSERT_NE(readPtr, nullptr);

        if (writePtr != nullptr)
        {
            *writePtr = 0u;
        }
        if (readPtr != nullptr)
        {
            EXPECT_EQ(*readPtr, 0u);
        }

        buffer->unmap();
        device->freeBuffer(buffer);
    }

    TEST_F(RhiSmokeTest, ReportsRequiredMappedPointersAcrossCommonBufferUsageCombinations)
    {
        const std::array<BufferMapCase, 13> cases = {{
            {"map_read_only", GVM::RHI::BufferUsage::MapRead, false, true},
            {"map_write_only", GVM::RHI::BufferUsage::MapWrite, true, false},
            {"map_read_write", GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite, true, true},
            {"copy_src_map_write", GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapWrite, true, false},
            {"copy_dst_map_read", GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead, false, true},
            {"copy_src_copy_dst_map_write", GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapWrite, true, false},
            {"copy_src_copy_dst_map_read_write",
                GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead |
                    GVM::RHI::BufferUsage::MapWrite,
                true,
                true},
            {"uniform_map_write", GVM::RHI::BufferUsage::Uniform | GVM::RHI::BufferUsage::MapWrite, true, false},
            {"storage_map_read", GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapRead, false, true},
            {"storage_map_write", GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::MapWrite, true, false},
            {"storage_copy_dst_map_read", GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::MapRead, false, true},
            {"storage_copy_src_map_write", GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapWrite, true, false},
            {"storage_copy_src_copy_dst_map_read_write",
                GVM::RHI::BufferUsage::Storage | GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::CopyDst |
                    GVM::RHI::BufferUsage::MapRead | GVM::RHI::BufferUsage::MapWrite,
                true,
                true},
        }};

        for (const auto &testCase : cases)
        {
            SCOPED_TRACE(testCase.name);

            auto buffer = device->createBuffer({
                .label = testCase.name,
                .usage = testCase.usage,
                .size = sizeof(uint32_t) * 4,
            });
            ASSERT_FALSE(buffer.isNull());

            buffer->map();

            if (testCase.requiresWritePointer)
            {
                auto *writePtr = static_cast<uint32_t *>(buffer->getMappedRange(0u, sizeof(uint32_t) * 4));
                EXPECT_NE(writePtr, nullptr);
                if (writePtr != nullptr)
                {
                    writePtr[0] = 1u;
                    writePtr[1] = 2u;
                    writePtr[2] = 3u;
                    writePtr[3] = 4u;
                }
            }

            if (testCase.requiresReadPointer)
            {
                const auto *readPtr = static_cast<const uint32_t *>(buffer->getConstMappedRange(0u, sizeof(uint32_t) * 4));
                EXPECT_NE(readPtr, nullptr);
            }

            buffer->unmap();
            device->freeBuffer(buffer);
        }
    }

    TEST_F(RhiSmokeTest, CreatesTextureAndSamplerObjects)
    {
        auto texture = device->createTexture({
            .label = "SmokeTexture",
            .usage = GVM::RHI::TextureUsage::TextureBinding | GVM::RHI::TextureUsage::CopyDst,
            .dimension = GVM::RHI::TextureDimension::e2D,
            .size = {4, 4, 1},
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
        });
        ASSERT_FALSE(texture.isNull());
        EXPECT_EQ(texture->getWidth(), 4u);
        EXPECT_EQ(texture->getHeight(), 4u);

        auto sampler = device->createSampler({
            .label = "SmokeSampler",
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
        ASSERT_FALSE(sampler.isNull());

        device->freeSampler(sampler);
        device->freeTexture(texture);
    }

    TEST_F(RhiSmokeTest, ResolvesTimestampQueriesForBlitPass)
    {
        const auto support = device->getTimestampQuerySupport();
        if (support.supported == GVM::RHI::False || support.passTimestampWritesSupported == GVM::RHI::False)
        {
            GTEST_SKIP() << "Timestamp pass writes are unsupported on backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }

        constexpr uint64_t kFillSize = 4u * 1024u * 1024u;
        auto targetBuffer = device->createBuffer({
            .label = "SmokeTimestampBlitTarget",
            .usage = GVM::RHI::BufferUsage::CopyDst,
            .size = kFillSize,
        });
        ASSERT_FALSE(targetBuffer.isNull());

        GVM::RHI::GpuTimestampFrameProfiler profiler;
        profiler.init(device, 1u, "SmokeTimestampProfiler");
        const auto scope = profiler.writePass("SmokeBlitFill");

        GVM::RHI::BlitPassDescriptor blitDescriptor = {};
        blitDescriptor.label = "SmokeTimestampBlitPass";
        blitDescriptor.timestampWrites = scope.timestampWrites;

        auto queue = device->getMainQueue();
        auto commandEncoder = queue->createCommandEncoder();
        ASSERT_TRUE(static_cast<bool>(commandEncoder));

        auto blitPass = commandEncoder->beginBlitPass(blitDescriptor);
        ASSERT_TRUE(static_cast<bool>(blitPass));
        blitPass->fillBuffer(GVM::RHI::BufferRange(targetBuffer, 0u, kFillSize), 0x12345678u);
        blitPass->end();

        profiler.resolve(commandEncoder);
        commandEncoder->end();

        eastl::vector<GVM::RHI::CommandEncoder> encoders(1);
        encoders[0] = commandEncoder;
        queue->submit(encoders);

        eastl::vector<GVM::RHI::TimestampRawResult> rawResults;
        profiler.readbackBlocking(queue, rawResults);
        ASSERT_EQ(rawResults.size(), 2u);

        const auto ranges = profiler.buildRangeResults(rawResults);
        ASSERT_EQ(ranges.size(), 1u);
        EXPECT_EQ(ranges[0].beginIndex, 0u);
        EXPECT_EQ(ranges[0].endIndex, 1u);
        EXPECT_GT(ranges[0].durationNs, 0.0);

        device->freeBuffer(targetBuffer);
    }

    TEST_F(RhiSmokeTest, ResolvesTimestampQueriesThroughQueueProxyBlitPass)
    {
        const auto support = device->getTimestampQuerySupport();
        if (support.supported == GVM::RHI::False || support.passTimestampWritesSupported == GVM::RHI::False)
        {
            GTEST_SKIP() << "Timestamp pass writes are unsupported on backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }

        constexpr uint64_t kFillSize = 4u * 1024u * 1024u;
        auto targetBuffer = device->createBuffer({
            .label = "SmokeQueueProxyTimestampBlitTarget",
            .usage = GVM::RHI::BufferUsage::CopyDst,
            .size = kFillSize,
        });
        ASSERT_FALSE(targetBuffer.isNull());

        GVM::Core::DeviceProxy deviceProxy(device);
        auto profiler = deviceProxy->createTimestampFrameProfiler(1u, "SmokeQueueProxyTimestampProfiler");
        const auto scope = profiler.writePass("SmokeQueueProxyBlitFill");

        auto queueProxy = deviceProxy->graphicsQueue(0);
        ASSERT_TRUE(static_cast<bool>(queueProxy));
        queueProxy->blitPass(
                      "SmokeQueueProxyTimestampBlitPass",
                      scope,
                      GVM::Core::fillBuffer(GVM::RHI::BufferRange(targetBuffer, 0u, kFillSize), 0xabcdef01u))
            ->resolveTimestampProfiler(profiler)
            ->submit();

        eastl::vector<GVM::RHI::TimestampRawResult> rawResults;
        profiler.readbackBlocking(device->getMainQueue(), rawResults);
        ASSERT_EQ(rawResults.size(), 2u);

        const auto ranges = profiler.buildRangeResults(rawResults);
        ASSERT_EQ(ranges.size(), 1u);
        EXPECT_EQ(ranges[0].beginIndex, 0u);
        EXPECT_EQ(ranges[0].endIndex, 1u);
        EXPECT_GT(ranges[0].durationNs, 0.0);

        device->freeBuffer(targetBuffer);
    }

    TEST_F(RhiSmokeTest, CreatesShaderModuleAndComputePipeline)
    {
        if (!inlineMetalShaderSupported())
        {
            GTEST_SKIP() << "This smoke case currently validates inline MSL shader-module creation and is skipped on backend="
                         << backendName(instance != nullptr ? instance->getBackend() : GVM::RHI::GraphicsBackend::Undefined);
        }

        auto shaderModule = device->createShaderModule({
            .label = "SmokeComputeShader",
            .code = kComputeShaderCode,
        });
        ASSERT_TRUE(static_cast<bool>(shaderModule));

        auto pipelineLayout = device->createPipelineLayout({
            .label = "SmokePipelineLayout",
            .bindGroupLayouts = {},
        });
        ASSERT_TRUE(static_cast<bool>(pipelineLayout));

        auto computePipeline = device->createComputePipeline({
            .label = "SmokeComputePipeline",
            .layout = pipelineLayout,
            .compute = {
                .module = shaderModule,
                .entryPoint = "test_main",
                .workgroupX = 1,
                .workgroupY = 1,
                .workgroupZ = 1,
            },
        });

        ASSERT_TRUE(static_cast<bool>(computePipeline));
    }
} // namespace
