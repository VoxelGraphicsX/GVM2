#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>
#include <GVMCore/Private/GQueue.hpp>
#include <GVMRHI/GVMRHI.hpp>
#include "MCommandEncoder.hpp"
#include "MDevice.hpp"
#include "MQueue.hpp"

#include <array>
#include <cstring>
#include <atomic>
#include <thread>

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
#if !GVM_DIAGNOSTICS_OVERLAY_ENABLED
        GTEST_SKIP() << "RHI diagnostics overlay is compiled out.";
#endif
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
#if !GVM_DIAGNOSTICS_OVERLAY_ENABLED
        GTEST_SKIP() << "RHI diagnostics overlay is compiled out.";
#endif
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
#if !GVM_DIAGNOSTICS_OVERLAY_ENABLED
        GTEST_SKIP() << "RHI diagnostics overlay is compiled out.";
#endif
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
#if !GVM_DIAGNOSTICS_OVERLAY_ENABLED
        GTEST_SKIP() << "RHI diagnostics overlay is compiled out.";
#endif
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

    /// Verifies queue preferences have stable identities and unsupported indices fall back to the graphics queue.
    TEST_F(RhiSmokeTest, PreferredQueuesAreStableAndUnsupportedIndicesFallBack)
    {
        using namespace GVM::RHI;
        const auto mainQueue = device->getMainQueue();
        EXPECT_EQ(device->getQueue({.prefer = QueuePreference::Graphics}), mainQueue);
        for (QueuePreference preference : {QueuePreference::Graphics, QueuePreference::Compute, QueuePreference::Transfer})
        {
            const auto preferred = device->getQueue({.prefer = preference});
            EXPECT_NE(preferred, nullptr);
            EXPECT_EQ(device->getQueue({.prefer = preference}), preferred);
            EXPECT_EQ(device->getQueue({.prefer = preference, .index = 4096}), mainQueue);
        }
        EXPECT_THROW(device->getQueue({.prefer = static_cast<QueuePreference>(255)}), std::invalid_argument);
    }

    /// Exercises implicit RAW, WAR and WAW ordering across real queues without flushing a producer's pending writes.
    TEST_F(RhiSmokeTest, CrossQueueCopiesPreserveAcceptedRangeHistory)
    {
        using namespace GVM::RHI;
        auto *first = device->getMainQueue();
        auto *second = device->getQueue({.prefer = QueuePreference::Compute});
        if (first == second)
        {
            GTEST_SKIP() << "Only one queue is exposed by this device.";
        }
        const std::array<uint32_t, 4> initial{3, 5, 7, 11};
        const std::array<uint32_t, 4> replacement{13, 17, 19, 23};
        auto source = device->createBuffer({.usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .size = sizeof(initial)});
        auto snapshot = device->createBuffer({.usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .size = sizeof(initial)});
        first->writeBuffer({source, 0, sizeof(initial)}, initial.data(), sizeof(initial));
        first->submit({});
        first->writeBuffer({source, 0, sizeof(initial)}, replacement.data(), sizeof(initial));
        auto encoder = second->createCommandEncoder();
        auto copy = encoder->beginBlitPass({});
        copy->copyBufferToBuffer({source, 4, 8}, {snapshot, 0, 8});
        copy->end();
        encoder->end();
        EXPECT_THROW(first->submit({encoder}), std::invalid_argument);
        EXPECT_THROW(first->submit({encoder}, {}), std::invalid_argument);
        second->submit({encoder});
        auto saved = first->readBufferAsync({snapshot, 0, 8});
        auto current = first->readBufferAsync({source, 0, sizeof(initial)});
        ASSERT_TRUE(first->submit({}, {}).wait(5'000'000'000ull));
        saved->wait();
        current->wait();
        const auto originalBytes = saved->getData();
        const auto updatedBytes = current->getData();
        ASSERT_EQ(originalBytes.size(), 8u);
        ASSERT_EQ(updatedBytes.size(), sizeof(initial));
        EXPECT_EQ(std::memcmp(originalBytes.data(), initial.data() + 1, 8), 0);
        EXPECT_EQ(std::memcmp(updatedBytes.data(), replacement.data(), sizeof(initial)), 0);
        device->freeBuffer(snapshot);
        device->freeBuffer(source);
    }

    /// Verifies an empty batch still produces a GPU completion and repeated waits remain valid.
    TEST_F(RhiSmokeTest, SubmissionEmptyBatchAndRepeatedDependenciesComplete)
    {
        auto *queue = device->getMainQueue();
        const auto first = queue->submit({}, {});
        const auto second = queue->submit({}, {first, first});
        const auto third = queue->submit({}, {first, second});
        ASSERT_TRUE(third.wait(5'000'000'000ull));
        EXPECT_TRUE(first.isComplete());
        EXPECT_TRUE(second.wait(0));
        EXPECT_TRUE(third.isComplete());
    }

    /// Verifies invalid handles and incomplete or duplicate encoders are rejected before commit.
    TEST_F(RhiSmokeTest, SubmissionValidationLeavesEncodersUsable)
    {
        using namespace GVM::RHI;
        auto *queue = device->getMainQueue();
        Submission unbound;
        EXPECT_THROW(unbound.isComplete(), std::logic_error);
        EXPECT_THROW(unbound.wait(0), std::logic_error);
        EXPECT_THROW(queue->submit({}, {unbound}), std::invalid_argument);
        auto encoder = queue->createCommandEncoder();
        EXPECT_THROW(queue->submit({encoder}, {}), std::invalid_argument);
        encoder->end();
        EXPECT_THROW(queue->submit({encoder, encoder}, {}), std::exception);
        EXPECT_THROW(queue->submit({CommandEncoder{}}, {}), std::invalid_argument);
        const auto completion = queue->submit({encoder}, {});
        ASSERT_TRUE(completion.wait(5'000'000'000ull));
        EXPECT_THROW(queue->submit({encoder}, {}), std::exception);
        EXPECT_TRUE(queue->submit({}, {completion}).wait(5'000'000'000ull));
    }

    /// Verifies a new submission preserves upload, user-copy and readback ordering through dependencies.
    TEST_F(RhiSmokeTest, SubmissionOrdersPendingTransfersAndUserCommands)
    {
        using namespace GVM::RHI;
        auto *queue = device->getMainQueue();
        const std::array<uint32_t, 4> expected = {17, 29, 43, 71};
        const auto source = device->createBuffer({.label = "SubmissionSource",
            .usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .size = sizeof(expected)});
        const auto target = device->createBuffer({.label = "SubmissionTarget",
            .usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .size = sizeof(expected)});
        queue->writeBuffer(BufferRange(source, 0, sizeof(expected)), expected.data(), sizeof(expected));
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginBlitPass({});
        pass->copyBufferToBuffer(BufferRange(source, 0, sizeof(expected)), BufferRange(target, 0, sizeof(expected)));
        pass->end();
        encoder->end();
        auto readback = queue->readBufferAsync(BufferRange(target, 0, sizeof(expected)));
        const auto copied = queue->submit({encoder}, {});
        const auto joined = queue->submit({}, {copied, copied});
        ASSERT_TRUE(joined.wait(5'000'000'000ull));
        readback->wait();
        const auto bytes = readback->getData();
        ASSERT_EQ(bytes.size(), sizeof(expected));
        EXPECT_EQ(std::memcmp(bytes.data(), expected.data(), sizeof(expected)), 0);
        device->freeBuffer(source);
        device->freeBuffer(target);
    }

    /// Verifies a completion's device identity cannot be reused on a different device instance.
    TEST_F(RhiSmokeTest, SubmissionRejectsForeignDeviceDependencies)
    {
        const auto completion = device->getMainQueue()->submit({}, {});
        auto *foreignInstance = GVM::Tests::createTestInstance();
        ASSERT_NE(foreignInstance, nullptr);
        auto *foreignDevice = foreignInstance->createDevice();
        ASSERT_NE(foreignDevice, nullptr);
        EXPECT_THROW(foreignDevice->getMainQueue()->submit({}, {completion}), std::invalid_argument);
        EXPECT_TRUE(foreignDevice->getMainQueue()->submit({}, {}).wait(5'000'000'000ull));
        GVM::RHI::destroyInstance(foreignInstance);
        EXPECT_TRUE(completion.wait(5'000'000'000ull));
    }

    /// Verifies copied completion handles allow concurrent host waits without queue access.
    TEST_F(RhiSmokeTest, SubmissionSupportsConcurrentHostWaiters)
    {
        const auto completion = device->getMainQueue()->submit({}, {});
        std::atomic<uint32_t> completed{0};
        eastl::vector<std::thread> waiters;
        for (uint32_t index = 0; index < 4; ++index)
        {
            waiters.emplace_back([completion, &completed]
            {
                if (completion.wait(5'000'000'000ull))
                {
                    completed.fetch_add(1);
                }
            });
        }
        for (auto &waiter : waiters)
        {
            waiter.join();
        }
        EXPECT_EQ(completed.load(), 4u);
    }

    /// Verifies device teardown drains accepted work and leaves retained completions queryable.
    TEST_F(RhiSmokeTest, SubmissionOutlivesDeviceAndQueueWrappers)
    {
        const auto completion = device->getMainQueue()->submit({}, {});
        GVM::RHI::destroyInstance(instance);
        instance = nullptr;
        device = nullptr;
        EXPECT_TRUE(completion.wait(0));
        EXPECT_TRUE(completion.isComplete());
    }

    /// Verifies a stalled Metal producer holds a different native queue without a host-side submit wait.
    TEST_F(RhiSmokeTest, SubmissionMetalCrossQueueWaitAndTimeout)
    {
        if (!inlineMetalShaderSupported())
        {
            GTEST_SKIP() << "This test exercises native Metal queues and a host-signaled Metal event.";
        }
        using namespace GVM::RHI;
        auto &metalDevice = *static_cast<Metal::MDevice *>(device);
        Metal::MQueue consumer;
        consumer.init(&metalDevice, "SubmissionConsumer");
        auto gate = NS::TransferPtr(metalDevice.getNativeDevice()->newSharedEvent());
        const auto source = device->createBuffer({.label = "SubmissionGatedSource",
            .usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .size = 16});
        const auto target = device->createBuffer({.label = "SubmissionGatedTarget",
            .usage = BufferUsage::CopySrc | BufferUsage::CopyDst, .size = 16});
        auto producerEncoder = device->getMainQueue()->createCommandEncoder();
        static_cast<Metal::MCommandEncoder *>(producerEncoder.get())->getNativeCommandEncoder()->encodeWait(gate.get(), 1);
        auto producerPass = producerEncoder->beginBlitPass({});
        producerPass->fillBuffer(BufferRange(source, 0, 16), 0x3a);
        producerPass->end();
        producerEncoder->end();
        try
        {
            const auto produced = device->getMainQueue()->submit({producerEncoder}, {});
            auto consumerEncoder = consumer.createCommandEncoder();
            auto consumerPass = consumerEncoder->beginBlitPass({});
            consumerPass->copyBufferToBuffer(BufferRange(source, 0, 16), BufferRange(target, 0, 16));
            consumerPass->end();
            consumerEncoder->end();
            auto readback = consumer.readBufferAsync(BufferRange(target, 0, 16));
            const auto consumed = consumer.submit({consumerEncoder}, {produced});
            EXPECT_FALSE(produced.wait(1'000'000));
            EXPECT_FALSE(consumed.wait(1'000'000));
            gate->setSignaledValue(1);
            EXPECT_TRUE(consumed.wait(5'000'000'000ull));
            readback->wait();
            const auto bytes = readback->getData();
            EXPECT_EQ(bytes.size(), 16u);
            for (const auto byte : bytes)
            {
                EXPECT_EQ(byte, 0x3a);
            }
        }
        catch (...)
        {
            gate->setSignaledValue(1);
            consumer.destroy();
            throw;
        }
        consumer.destroy();
        device->freeBuffer(source);
        device->freeBuffer(target);
    }

    /// Verifies diagnostics wrappers preserve the dependency-aware submission contract.
    TEST_F(RhiSmokeTest, SubmissionPassesThroughDiagnosticsOverlay)
    {
        auto descriptor = GVM::Tests::makeTestInstanceDescriptor();
        descriptor.diagnosticsOverlay.enabled = GVM::RHI::True;
        auto *overlayInstance = GVM::RHI::createInstance(descriptor);
        ASSERT_NE(overlayInstance, nullptr);
        auto *overlayDevice = overlayInstance->createDevice();
        ASSERT_NE(overlayDevice, nullptr);
        auto *queue = overlayDevice->getMainQueue();
        auto encoder = queue->createCommandEncoder();
        EXPECT_THROW(queue->submit({encoder}, {}), std::invalid_argument);
        encoder->end();
        const auto first = queue->submit({encoder}, {});
        EXPECT_TRUE(queue->submit({}, {first}).wait(5'000'000'000ull));
        GVM::RHI::destroyInstance(overlayInstance);
        EXPECT_TRUE(first.isComplete());
    }

    TEST_F(RhiSmokeTest, HealthyDeviceReportsEmptyLostInfoAndNoCallback)
    {
        bool callbackInvoked = false;
        GVM::RHI::InstanceDescriptor descriptor = GVM::Tests::makeTestInstanceDescriptor();
        descriptor.deviceLostCallback = [&callbackInvoked](const GVM::RHI::DeviceLostInfo &) { callbackInvoked = true; };
        GVM::RHI::Instance testInstance = GVM::RHI::createInstance(descriptor);
        ASSERT_NE(testInstance, nullptr);
        GVM::RHI::Device testDevice = testInstance->createDevice();
        ASSERT_NE(testDevice, nullptr);

        auto queue = testDevice->getMainQueue();
        auto buffer = testDevice->createBuffer({
            .label = "HealthyLostProbe",
            .usage = GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::CopySrc,
            .size = sizeof(uint32_t) * 4,
        });
        const uint32_t value = 0x42u;
        queue->writeBuffer(GVM::RHI::BufferRange(buffer, 0u, sizeof(value)), &value, sizeof(value));
        auto commandEncoder = queue->createCommandEncoder();
        eastl::vector<GVM::RHI::CommandEncoder> encoders;
        encoders.push_back(commandEncoder);
        queue->submit(encoders);
        eastl::vector<uint8_t> readback(sizeof(uint32_t) * 4);
        queue->readBuffer(GVM::RHI::BufferRange(buffer, 0u, readback.size()), readback.data(), readback.size());
        testDevice->freeBuffer(buffer);

        EXPECT_FALSE(testDevice->getLostInfo().has_value());
        EXPECT_FALSE(callbackInvoked);
        GVM::RHI::destroyInstance(testInstance);
    }

    /// Verifies the device reports a device-local heap budget and that heap usage follows a large allocation and its release.
    TEST_F(RhiSmokeTest, ReportsDeviceLocalMemoryHeapBudget)
    {
        const GVM::RHI::DeviceMemoryBudget initial = device->getMemoryBudget();
        ASSERT_FALSE(initial.heaps.empty());
        bool sawDeviceLocalBudget = false;
        for (const auto &heap : initial.heaps)
        {
            if (heap.deviceLocal && heap.budgetBytes > 0u)
            {
                sawDeviceLocalBudget = true;
            }
        }
        EXPECT_TRUE(sawDeviceLocalBudget);

        const auto deviceLocalUsage = [](const GVM::RHI::DeviceMemoryBudget &budget)
        {
            uint64_t usage = 0u;
            for (const auto &heap : budget.heaps)
            {
                if (heap.deviceLocal)
                {
                    usage += heap.usageBytes;
                }
            }
            return usage;
        };
        const uint64_t baselineUsage = deviceLocalUsage(initial);
        // Larger than half of VMA's 256 MiB preferred block size, so Vulkan gives the buffer a dedicated
        // allocation whose memory is returned immediately when the buffer is destroyed.
        constexpr uint64_t AllocationBytes = 192u * 1024u * 1024u;
        auto buffer = device->createBuffer({
            .label = "MemoryBudgetProbe",
            .usage = GVM::RHI::BufferUsage::Storage,
            .size = AllocationBytes,
        });
        ASSERT_FALSE(buffer.isNull());
        const uint64_t allocatedUsage = deviceLocalUsage(device->getMemoryBudget());
        EXPECT_GE(allocatedUsage, baselineUsage + AllocationBytes);
        device->freeBuffer(buffer);
        // Vulkan destroys freed resources during the next submit's completed-work cleanup.
        device->getMainQueue()->submit({});
        const uint64_t freedUsage = deviceLocalUsage(device->getMemoryBudget());
        EXPECT_LT(freedUsage, allocatedUsage);
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
        blitPass->fillBuffer(GVM::RHI::BufferRange(targetBuffer, 0u, kFillSize), 0x78u);
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
                      GVM::Core::fillBuffer(GVM::RHI::BufferRange(targetBuffer, 0u, kFillSize), 0x01u))
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

    // Exercises WVM-sized layouts above the ordinary per-stage descriptor limits.
    TEST_F(RhiSmokeTest, VulkanAcceptsLargeUpdateAfterBindLayouts)
    {
        if (instance->getBackend() != GVM::RHI::GraphicsBackend::Vulkan)
            GTEST_SKIP() << "Vulkan descriptor indexing regression.";

        using namespace GVM::RHI;
        BindGroupLayoutDescriptor storage = {.label = "StorageArrayLayout"};
        storage.entries.push_back({.binding = 0u, .maxCount = 52u,
            .visibility = ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
            .buffer = {.type = BufferBindingType::Storage, .access = StorageBufferAccess::ReadOnly}});
        BindGroupLayoutDescriptor images = {.label = "SampledArrayLayout"};
        for (uint32_t binding = 0u; binding < 5u; ++binding)
        {
            images.entries.push_back({.binding = binding, .maxCount = 257u,
                .visibility = ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute,
                .texture = {.sampleType = TextureSampleType::Float, .viewDimension = TextureViewDimension::e2D}});
        }
        auto storageLayout = device->createBindGroupLayout(storage);
        auto imageLayout = device->createBindGroupLayout(images);
        auto pipeline = device->createPipelineLayout({.label = "LargeDescriptorPipeline",
            .bindGroupLayouts = {storageLayout, imageLayout}});
        ASSERT_TRUE(static_cast<bool>(pipeline));
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
    /// Verifies creating a device with an enabled overlay throws std::runtime_error when the overlay is compiled out; skipped when it is compiled in.
    TEST(RhiDiagnosticsOverlayTests, EnabledOverlayRejectedWhenCompiledOut)
    {
#if GVM_DIAGNOSTICS_OVERLAY_ENABLED
        GTEST_SKIP() << "RHI diagnostics overlay is compiled in.";
#else
        GVM::RHI::InstanceDescriptor descriptor = GVM::Tests::makeTestInstanceDescriptor();
        descriptor.diagnosticsOverlay.enabled = GVM::RHI::True;
        GVM::RHI::Instance overlayInstance = GVM::RHI::createInstance(descriptor);
        ASSERT_NE(overlayInstance, nullptr);
        EXPECT_THROW(overlayInstance->createDevice(), std::runtime_error);
#endif
    }

} // namespace
