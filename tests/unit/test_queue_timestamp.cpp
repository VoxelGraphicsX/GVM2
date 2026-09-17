#include <gtest/gtest.h>

#include <GShader.hpp>
#include <GQueue.hpp>

#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace
{
    struct QueueTimestampStats
    {
        eastl::vector<GVM::RHI::RenderPassDescriptor> renderPassDescriptors;
        eastl::vector<GVM::RHI::ComputePassDescriptor> computePassDescriptors;
        eastl::vector<GVM::RHI::BlitPassDescriptor> blitPassDescriptors;
        size_t commandEncodersCreated = 0u;
        size_t commandEncodersEnded = 0u;
        size_t submittedEncoders = 0u;
        size_t renderPassEnds = 0u;
        size_t computeDispatchCalls = 0u;
        size_t nextPixelLocalPassCalls = 0u;
        size_t ordinaryRenderTaskCalls = 0u;
        size_t pixelLocalTaskCalls = 0u;
        size_t queueWriteBufferCalls = 0u;
        size_t buffersCreated = 0u;
        size_t buffersFreed = 0u;
        size_t shaderModulesCreated = 0u;
        size_t pipelineLayoutsCreated = 0u;
        size_t renderPipelinesCreated = 0u;
        size_t renderSetPipelineCalls = 0u;
        size_t renderSetVertexBufferCalls = 0u;
        size_t renderDrawCalls = 0u;
        uint32_t lastDrawVertexCount = 0u;
        uint32_t lastIndexedIndirectCommandCount = 0u;
        uint32_t lastIndexedIndirectStride = 0u;
        uint32_t lastComputeDispatchX = 0u;
        uint32_t lastComputeDispatchY = 0u;
        uint32_t lastComputeDispatchZ = 0u;
    };

    class FakeQuerySet final : public GVM::RHI::QuerySetImpl
    {
    public:
        explicit FakeQuerySet(uint32_t count)
            : mCount(count)
        {
        }

        GVM::RHI::QueryType getType() const override
        {
            return GVM::RHI::QueryType::Timestamp;
        }

        uint32_t getCount() const override
        {
            return mCount;
        }

    private:
        uint32_t mCount = 0u;
    };

    class FakeBlitPassEncoder final : public GVM::RHI::BlitPassEncoderImpl
    {
    public:
        void copyBufferToBuffer(GVM::RHI::BufferRange, GVM::RHI::BufferRange) override {}
        void copyBufferToTexture(const GVM::RHI::ImageCopyBuffer &, const GVM::RHI::ImageCopyTexture &, const GVM::RHI::Extent3D &) override {}
        void copyTextureToBuffer(const GVM::RHI::ImageCopyTexture &, const GVM::RHI::ImageCopyBuffer &, const GVM::RHI::Extent3D &) override {}
        void fillBuffer(GVM::RHI::BufferRange, uint32_t) override {}
        void end() override {}
    };

    class FakeComputePassEncoder final : public GVM::RHI::ComputePassEncoderImpl
    {
    public:
        explicit FakeComputePassEncoder(QueueTimestampStats *stats = nullptr)
            : mStats(stats)
        {
        }

        void setPipeline(GVM::RHI::ComputePipeline) override {}
        void setBindGroup(GVM::RHI::BindGroup, uint32_t) override {}
        void dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z) override
        {
            if (mStats == nullptr)
                return;
            mStats->computeDispatchCalls++;
            mStats->lastComputeDispatchX = x;
            mStats->lastComputeDispatchY = y;
            mStats->lastComputeDispatchZ = z;
        }
        void dispatchWorkgroupsIndirect(GVM::RHI::BufferRange) override {}
        void end() override {}

    private:
        QueueTimestampStats *mStats = nullptr;
    };

    class FakeRenderPassEncoder final : public GVM::RHI::RenderPassEncoderImpl
    {
    public:
        explicit FakeRenderPassEncoder(QueueTimestampStats *stats)
            : mStats(stats)
        {
        }

        void setScissorRect(uint32_t, uint32_t, uint32_t, uint32_t) override {}
        void setViewport(float, float, float, float, float, float) override {}
        void drawFullscreenTexture(GVM::RHI::Texture, const GVM::RHI::RenderToSwapchainDescriptor &) override {}
        void setPipeline(GVM::RHI::RenderPipeline) override
        {
            mStats->renderSetPipelineCalls++;
        }
        void setBindGroup(GVM::RHI::BindGroup, uint32_t) override {}
        void setVertexBuffer(GVM::RHI::BufferRange, uint32_t) override
        {
            mStats->renderSetVertexBufferCalls++;
        }
        void setIndexBuffer(GVM::RHI::BufferRange, GVM::RHI::IndexFormat) override {}
        void draw(uint32_t vertexCount, uint32_t, uint32_t, uint32_t) override
        {
            mStats->renderDrawCalls++;
            mStats->lastDrawVertexCount = vertexCount;
        }
        void drawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
        void drawIndirect(GVM::RHI::BufferRange, uint32_t, uint32_t) override {}
        void drawIndexedIndirect(GVM::RHI::BufferRange, uint32_t commandCount, uint32_t stride) override
        {
            mStats->lastIndexedIndirectCommandCount = commandCount;
            mStats->lastIndexedIndirectStride = stride;
        }
        void drawPixels() override {}
        void nextPixelLocalPass() override
        {
            mStats->nextPixelLocalPassCalls++;
        }
        void end() override
        {
            mStats->renderPassEnds++;
        }

    private:
        QueueTimestampStats *mStats = nullptr;
    };

    /** Exposes a controlled RenderSet binding for parameterless RenderClass task tests. */
    class RenderSetTaskTestRenderClass final : public GVM::Core::IRenderClass
    {
    public:
        /** Binds a non-null RenderSet while suppressing resource lookups unrelated to indirect stride. */
        void bindRenderSet(const eastl::intrusive_ptr<GVM::Core::RenderSet> &renderSet)
        {
            mRenderSet = renderSet;
            setRenderSetVertexBufferBindingEnabled(false);
            mSetIndexBuffer = true;
        }
    };

    /** Exposes local workgroup metadata for testing total-thread dispatch rounding. */
    class ComputeClassTaskTestComputeClass final : public GVM::Core::IComputeClass
    {
    public:
        /** Sets the shader's declared local workgroup dimensions for the test. */
        void setLocalWorkgroupSize(uint32_t x, uint32_t y, uint32_t z)
        {
            workGroupX = x;
            workGroupY = y;
            workGroupZ = z;
        }
    };

    class FakeCommandEncoder final : public GVM::RHI::CommandEncoderImpl
    {
    public:
        explicit FakeCommandEncoder(QueueTimestampStats *stats)
            : mStats(stats)
        {
        }

        void begin() override {}

        GVM::RHI::RenderPassEncoder beginRenderPass(const GVM::RHI::RenderPassDescriptor &pass) override
        {
            mStats->renderPassDescriptors.push_back(pass);
            return new FakeRenderPassEncoder(mStats);
        }

        GVM::RHI::BlitPassEncoder beginBlitPass(const GVM::RHI::BlitPassDescriptor &pass) override
        {
            mStats->blitPassDescriptors.push_back(pass);
            return new FakeBlitPassEncoder();
        }

        GVM::RHI::ComputePassEncoder beginComputePass(const GVM::RHI::ComputePassDescriptor &pass) override
        {
            mStats->computePassDescriptors.push_back(pass);
            return new FakeComputePassEncoder(mStats);
        }

        void resolveQuerySet(GVM::RHI::QuerySet, uint32_t, uint32_t, GVM::RHI::BufferRange) override {}

        void end() override
        {
            mStats->commandEncodersEnded++;
        }

    private:
        QueueTimestampStats *mStats = nullptr;
    };

    class FakeQueue final : public GVM::RHI::QueueImpl
    {
    public:
        explicit FakeQueue(QueueTimestampStats *stats)
            : mStats(stats)
        {
        }

        GVM::RHI::CommandEncoder createCommandEncoder() override
        {
            mStats->commandEncodersCreated++;
            GVM::RHI::CommandEncoder encoder = new FakeCommandEncoder(mStats);
            encoder->begin();
            return encoder;
        }

        void writeBuffer(GVM::RHI::BufferRange, void const *, uint64_t) override
        {
            mStats->queueWriteBufferCalls++;
        }
        void readBuffer(GVM::RHI::BufferRange, void *, uint64_t) override {}
        void writeTexture(const GVM::RHI::ImageCopyTexture &, void const *, uint64_t, const GVM::RHI::TextureDataLayout &, const GVM::RHI::Extent3D &) override {}
        void readTexture(const GVM::RHI::ImageCopyTexture &, void *, uint64_t, const GVM::RHI::TextureDataLayout &, const GVM::RHI::Extent3D &) override {}
        void uploadTexture(GVM::RHI::Texture, void const *, uint64_t, const eastl::vector<uint64_t> &) override {}
        void uploadTexture(GVM::RHI::Texture, GVM::RHI::BufferRange, const eastl::vector<uint64_t> &) override {}
        void copyBufferToBuffer(GVM::RHI::BufferRange, GVM::RHI::BufferRange) override {}
        void copyBufferToTexture(const GVM::RHI::ImageCopyBuffer &, const GVM::RHI::ImageCopyTexture &, const GVM::RHI::Extent3D &) override {}
        void copyTextureToBuffer(const GVM::RHI::ImageCopyTexture &, const GVM::RHI::ImageCopyBuffer &, const GVM::RHI::Extent3D &) override {}
        void copyBufferToBufferMultipleRegion(GVM::RHI::Buffer, GVM::RHI::Buffer, GVM::RHI::Buffer, uint32_t) override {}
        void fillBuffer(GVM::RHI::BufferRange, uint32_t) override {}

        void submit(const eastl::vector<GVM::RHI::CommandEncoder> &encoders) override
        {
            mStats->submittedEncoders += encoders.size();
        }

        void destroy() override {}

    private:
        QueueTimestampStats *mStats = nullptr;
    };

    /// Provides a fake buffer resource for overlay vertex-buffer allocation tests.
    class FakeOverlayBuffer final : public GVM::RHI::BufferImpl
    {
    public:
        /// Creates a fake buffer with the requested size and label.
        explicit FakeOverlayBuffer(const GVM::RHI::BufferDescriptor &descriptor)
            : mStorage(static_cast<size_t>(descriptor.size), uint8_t{0})
        {
            mLabelName = descriptor.label;
        }

        /// Returns the configured storage size so BufferRange validation can run.
        uint64_t getStorageSize() const override
        {
            return static_cast<uint64_t>(mStorage.size());
        }

        /// Marks the fake buffer as mapped so overlay code can upload vertex data.
        void map() override
        {
            mMapped = true;
        }

        /// Returns a readable mapped range for tests that inspect uploaded data.
        void const *getConstMappedRange(uint64_t offset, uint64_t size) const override
        {
            validateMappedRange(offset, size);
            return mStorage.data() + static_cast<size_t>(offset);
        }

        /// Returns a writable mapped range for overlay vertex-buffer uploads.
        void *getMappedRange(uint64_t offset, uint64_t size) const override
        {
            validateMappedRange(offset, size);
            return const_cast<uint8_t *>(mStorage.data() + static_cast<size_t>(offset));
        }

        /// Marks the fake buffer as unmapped after an upload completes.
        void unmap() override
        {
            mMapped = false;
        }

        /// Destroys the fake buffer without native backend work.
        void destroy() override {}

    private:
        /// Validates that a requested mapped range is available in the fake storage.
        void validateMappedRange(uint64_t offset, uint64_t size) const
        {
            if (!mMapped || offset > mStorage.size() || size > mStorage.size() - offset)
            {
                throw std::out_of_range("FakeOverlayBuffer mapped range is invalid");
            }
        }

        eastl::vector<uint8_t> mStorage;
        bool mMapped = false;
    };

    /// Provides a fake shader module handle for overlay pipeline creation tests.
    class FakeOverlayShaderModule final : public GVM::RHI::ShaderModuleImpl
    {
    public:
        /// Creates a fake shader module with a visible diagnostics label.
        explicit FakeOverlayShaderModule(eastl::string label)
        {
            mLabelName = eastl::move(label);
        }
    };

    /// Provides a fake pipeline layout handle for overlay pipeline creation tests.
    class FakeOverlayPipelineLayout final : public GVM::RHI::PipelineLayoutImpl
    {
    public:
        /// Creates a fake pipeline layout with a visible diagnostics label.
        explicit FakeOverlayPipelineLayout(eastl::string label)
        {
            mLabelName = eastl::move(label);
        }
    };

    /// Provides a fake render pipeline handle for overlay pipeline creation tests.
    class FakeOverlayRenderPipeline final : public GVM::RHI::RenderPipelineImpl
    {
    public:
        /// Creates a fake render pipeline with a visible diagnostics label.
        explicit FakeOverlayRenderPipeline(eastl::string label)
        {
            mLabelName = eastl::move(label);
        }
    };

    /// Provides a fake texture view with fixed metadata for overlay target selection tests.
    class FakeOverlayTextureView final : public GVM::RHI::TextureViewImpl
    {
    public:
        /// Creates a fake texture view that reports a fixed render-target format and size.
        FakeOverlayTextureView(eastl::string label, GVM::RHI::TextureFormat format, uint32_t width, uint32_t height)
            : mFormat(format)
            , mWidth(width)
            , mHeight(height)
        {
            mLabelName = eastl::move(label);
        }

        /// Returns the configured render-target format.
        GVM::RHI::TextureFormat getFormat() const override
        {
            return mFormat;
        }

        /// Returns the configured render-target width.
        uint32_t getWidth() const override
        {
            return mWidth;
        }

        /// Returns the configured render-target height.
        uint32_t getHeight() const override
        {
            return mHeight;
        }

        /// Destroys the fake texture view without native backend work.
        void destroy() override {}

    private:
        GVM::RHI::TextureFormat mFormat = GVM::RHI::TextureFormat::Undefined;
        uint32_t mWidth = 0u;
        uint32_t mHeight = 0u;
    };

    /// Provides a minimal fake device that can create the overlay resources used by QueueProxy tests.
    class FakeDiagnosticsDevice final : public GVM::RHI::DeviceImpl
    {
    public:
        /// Creates a fake diagnostics device backed by the supplied queue and overlay configuration.
        FakeDiagnosticsDevice(FakeQueue *queue, QueueTimestampStats *stats, GVM::RHI::RuntimeDiagnosticsOverlayConfig config)
            : mQueue(queue)
            , mStats(stats)
            , mConfig(config)
        {
            mBufferPool.init();
        }

        /// Returns the fake main queue used by the overlay controller for vertex uploads.
        GVM::RHI::Queue getMainQueue() const override
        {
            return mQueue;
        }

        /// Reports timestamp queries as unsupported unless a test explicitly extends this fake.
        GVM::RHI::TimestampQuerySupport getTimestampQuerySupport() const override
        {
            return {};
        }

        /// Returns default memory properties because queue overlay tests do not select memory paths.
        GVM::RHI::DeviceMemoryProperties getMemoryProperties() const override
        {
            return {};
        }

        /// Returns the overlay configuration supplied by the test case.
        GVM::RHI::RuntimeDiagnosticsOverlayConfig getDiagnosticsOverlayConfig() const override
        {
            return mConfig;
        }

        /// Returns an empty resource snapshot so overlay text rendering remains deterministic in tests.
        GVM::RHI::DiagnosticsResourceSnapshot getDiagnosticsResourceSnapshot() const override
        {
            return {};
        }

        /// Allocates fake buffers for overlay glyph vertices and timestamp resolve buffers.
        GVM::RHI::Buffer createBuffer(const GVM::RHI::BufferDescriptor &descriptor) override
        {
            mStats->buffersCreated++;
            return mBufferPool.alloc(new FakeOverlayBuffer(descriptor));
        }

        /// Returns null because this fake test path disables overlay timestamp pass timing.
        GVM::RHI::QuerySet createQuerySet(const GVM::RHI::QuerySetDescriptor &) override
        {
            return nullptr;
        }

        /// Returns null because this fake test path never creates textures through the device.
        GVM::RHI::Texture createTexture(const GVM::RHI::TextureDescriptor &) override
        {
            return {};
        }

        /// Returns null because this fake test path never creates compute pipelines.
        GVM::RHI::ComputePipeline createComputePipeline(const GVM::RHI::ComputePipelineDescriptor &) override
        {
            return nullptr;
        }

        /// Creates a fake render pipeline for the overlay draw.
        GVM::RHI::RenderPipeline createRenderPipeline(const GVM::RHI::RenderPipelineDescriptor &descriptor) override
        {
            mStats->renderPipelinesCreated++;
            return new FakeOverlayRenderPipeline(descriptor.label);
        }

        /// Creates a fake shader module for the overlay pipeline.
        GVM::RHI::ShaderModule createShaderModule(const GVM::RHI::ShaderModuleDescriptor &descriptor) override
        {
            mStats->shaderModulesCreated++;
            return new FakeOverlayShaderModule(descriptor.label);
        }

        /// Returns null because the overlay text v1 pipeline does not need bind group layouts.
        GVM::RHI::BindGroupLayout createBindGroupLayout(const GVM::RHI::BindGroupLayoutDescriptor &) override
        {
            return nullptr;
        }

        /// Creates a fake pipeline layout for the overlay pipeline.
        GVM::RHI::PipelineLayout createPipelineLayout(const GVM::RHI::PipelineLayoutDescriptor &descriptor) override
        {
            mStats->pipelineLayoutsCreated++;
            return new FakeOverlayPipelineLayout(descriptor.label);
        }

        /// Returns null because the overlay text v1 draw does not bind resource groups.
        GVM::RHI::BindGroup createBindGroup(const GVM::RHI::BindGroupDescriptor &) override
        {
            return nullptr;
        }

        /// Returns a null sampler because the overlay text v1 draw does not sample textures.
        GVM::RHI::Sampler createSampler(const GVM::RHI::SamplerDescriptor &) override
        {
            return {};
        }

        /// Returns no logger so tests do not depend on logging providers.
        GVM::RHI::Logger getLogger() const override
        {
            return {};
        }

        /// Frees a fake buffer handle and records the destruction request.
        void freeBuffer(GVM::RHI::Buffer buffer) override
        {
            if (buffer.isNull())
            {
                return;
            }
            mStats->buffersFreed++;
            buffer->destroy();
            mBufferPool.freeByHandle(buffer);
        }

        /// Ignores texture destruction because this fake test path never creates textures.
        void freeTexture(GVM::RHI::Texture) override {}

        /// Ignores sampler destruction because this fake test path never creates samplers.
        void freeSampler(GVM::RHI::Sampler) override {}

        /// Destroys the fake diagnostics device without native backend work.
        void destroy() override {}

    private:
        FakeQueue *mQueue = nullptr;
        QueueTimestampStats *mStats = nullptr;
        GVM::RHI::RuntimeDiagnosticsOverlayConfig mConfig = {};
        GVM::RHI::BufferPool mBufferPool;
    };

    /// Provides a framebuffer descriptor with two color attachments for overlay target selection tests.
    struct FakeOverlayFrameBuffer
    {
        GVM::RHI::TextureView firstColor;
        GVM::RHI::TextureView secondColor;

        /// Builds a render pass descriptor that exposes two color attachments in a stable order.
        GVM::RHI::RenderPassDescriptor getRenderPassDescriptor() const
        {
            GVM::RHI::RenderPassDescriptor descriptor = {};
            descriptor.colorAttachments.push_back({
                .view = firstColor,
                .loadOp = GVM::RHI::LoadOp::Clear,
                .storeOp = GVM::RHI::StoreOp::Store,
                .clearValue = {0.0, 0.0, 0.0, 1.0},
            });
            descriptor.colorAttachments.push_back({
                .view = secondColor,
                .loadOp = GVM::RHI::LoadOp::Clear,
                .storeOp = GVM::RHI::StoreOp::Store,
                .clearValue = {0.0, 0.0, 0.0, 1.0},
            });
            return descriptor;
        }
    };

    struct FakeFrameBuffer
    {
        GVM::RHI::RenderPassDescriptor getRenderPassDescriptor() const
        {
            return {};
        }
    };

    GVM::RHI::GpuTimestampFrameProfiler::Scope makeTimestampScope(GVM::RHI::QuerySet querySet)
    {
        GVM::RHI::GpuTimestampFrameProfiler::Scope scope = {};
        scope.timestampWrites.querySet = querySet;
        scope.timestampWrites.beginningOfPassWriteIndex = 2u;
        scope.timestampWrites.endOfPassWriteIndex = 3u;
        return scope;
    }
} // namespace

TEST(RuntimeDiagnosticsOverlayConfigTests, DefaultsAreDisabledAndBounded)
{
    const GVM::RHI::InstanceDescriptor descriptor = {};

    EXPECT_EQ(descriptor.diagnosticsOverlay.enabled, GVM::RHI::False);
    EXPECT_EQ(descriptor.diagnosticsOverlay.showFps, GVM::RHI::True);
    EXPECT_EQ(descriptor.diagnosticsOverlay.showPassTimes, GVM::RHI::True);
    EXPECT_EQ(descriptor.diagnosticsOverlay.showResources, GVM::RHI::True);
    EXPECT_EQ(descriptor.diagnosticsOverlay.maxPassScopes, 128u);
    EXPECT_EQ(descriptor.diagnosticsOverlay.maxResourceRows, 256u);
    EXPECT_EQ(descriptor.diagnosticsOverlay.maxTextGlyphs, 4096u);
    EXPECT_EQ(descriptor.diagnosticsOverlay.timestampReadbackLatencyFrames, 2u);
}

TEST(QueueProxyTimestampTests, DefaultPassOverloadsDoNotAttachTimestampWrites)
{
    QueueTimestampStats stats;
    FakeQueue fakeQueue(&stats);
    GVM::Core::QueueProxyImpl queue(&fakeQueue);
    FakeFrameBuffer framebuffer;

    queue.computePass("DefaultCompute")
        ->blitPass("DefaultBlit")
        ->renderPass("DefaultRender", framebuffer)
        ->submit();

    ASSERT_EQ(stats.computePassDescriptors.size(), 1u);
    ASSERT_EQ(stats.blitPassDescriptors.size(), 1u);
    ASSERT_EQ(stats.renderPassDescriptors.size(), 1u);
    EXPECT_EQ(stats.computePassDescriptors[0].timestampWrites.querySet, nullptr);
    EXPECT_EQ(stats.blitPassDescriptors[0].timestampWrites.querySet, nullptr);
    EXPECT_EQ(stats.renderPassDescriptors[0].timestampWrites.querySet, nullptr);
    EXPECT_EQ(stats.commandEncodersCreated, 1u);
    EXPECT_EQ(stats.commandEncodersEnded, 1u);
    EXPECT_EQ(stats.submittedEncoders, 1u);
}

TEST(QueueProxyTimestampTests, TimestampScopeOverloadsForwardPassWrites)
{
    QueueTimestampStats stats;
    FakeQueue fakeQueue(&stats);
    GVM::Core::QueueProxyImpl queue(&fakeQueue);
    FakeFrameBuffer framebuffer;
    GVM::RHI::QuerySet querySet = new FakeQuerySet(8u);
    const auto scope = makeTimestampScope(querySet);

    queue.computePass("TimestampCompute", scope)
        ->blitPass("TimestampBlit", scope)
        ->renderPass("TimestampRender", framebuffer, scope)
        ->submit();

    ASSERT_EQ(stats.computePassDescriptors.size(), 1u);
    ASSERT_EQ(stats.blitPassDescriptors.size(), 1u);
    ASSERT_EQ(stats.renderPassDescriptors.size(), 1u);

    EXPECT_EQ(stats.computePassDescriptors[0].timestampWrites.querySet.get(), querySet.get());
    EXPECT_EQ(stats.blitPassDescriptors[0].timestampWrites.querySet.get(), querySet.get());
    EXPECT_EQ(stats.renderPassDescriptors[0].timestampWrites.querySet.get(), querySet.get());
    EXPECT_EQ(stats.computePassDescriptors[0].timestampWrites.beginningOfPassWriteIndex, 2u);
    EXPECT_EQ(stats.blitPassDescriptors[0].timestampWrites.beginningOfPassWriteIndex, 2u);
    EXPECT_EQ(stats.renderPassDescriptors[0].timestampWrites.beginningOfPassWriteIndex, 2u);
    EXPECT_EQ(stats.computePassDescriptors[0].timestampWrites.endOfPassWriteIndex, 3u);
    EXPECT_EQ(stats.blitPassDescriptors[0].timestampWrites.endOfPassWriteIndex, 3u);
    EXPECT_EQ(stats.renderPassDescriptors[0].timestampWrites.endOfPassWriteIndex, 3u);
}

TEST(QueueProxyPixelLocalPhaseTests, MixedOrdinaryAndPixelLocalPhasesAdvanceExplicitBoundaries)
{
    QueueTimestampStats stats;
    FakeQueue fakeQueue(&stats);
    GVM::Core::QueueProxyImpl queue(&fakeQueue);
    FakeFrameBuffer framebuffer;

    auto ordinaryTask = [&stats]() {
        return GVM::Core::RenderPassTaskDescriptor{
            .drawFn = [&stats](GVM::RHI::RenderPassEncoder) {
                stats.ordinaryRenderTaskCalls++;
            },
        };
    };
    auto pixelLocalTask = [&stats]() {
        return GVM::Core::RenderPassTaskDescriptor{
            .drawFn = [&stats](GVM::RHI::RenderPassEncoder) {
                stats.pixelLocalTaskCalls++;
            },
            .phaseRequirement = GVM::Core::RenderPassPhaseRequirement::PixelLocalOnly,
        };
    };

    queue.renderPass(
             "MixedPixelLocalPhases",
             framebuffer,
             ordinaryTask(),
             ordinaryTask(),
             GVM::Core::pixelLocalPass(pixelLocalTask(), GVM::Core::nextPixelLocalPass(), pixelLocalTask()),
             ordinaryTask())
        ->submit();

    ASSERT_EQ(stats.renderPassDescriptors.size(), 1u);
    EXPECT_TRUE(stats.renderPassDescriptors[0].pixelLocal.enabled);
    EXPECT_EQ(stats.renderPassDescriptors[0].pixelLocal.passCount, 4u);
    EXPECT_EQ(stats.nextPixelLocalPassCalls, 3u);
    EXPECT_EQ(stats.ordinaryRenderTaskCalls, 3u);
    EXPECT_EQ(stats.pixelLocalTaskCalls, 2u);
    EXPECT_EQ(stats.renderPassEnds, 1u);
}

TEST(QueueProxyPixelLocalPhaseTests, PixelLocalOnlyTaskCannotRunAsTopLevelOrdinaryPhase)
{
    QueueTimestampStats stats;
    FakeQueue fakeQueue(&stats);
    GVM::Core::QueueProxyImpl queue(&fakeQueue);
    FakeFrameBuffer framebuffer;

    GVM::Core::RenderPassTaskDescriptor pixelLocalOnlyTask = {
        .drawFn = [](GVM::RHI::RenderPassEncoder) {},
        .phaseRequirement = GVM::Core::RenderPassPhaseRequirement::PixelLocalOnly,
    };

    EXPECT_THROW(
        queue.renderPass("InvalidTopLevelPixelLocalTask", framebuffer, pixelLocalOnlyTask),
        std::invalid_argument);
}

TEST(RenderClassTaskTests, DeprecatedParameterlessRunRequiresRenderSet)
{
    GVM::Core::IRenderClass renderClass;
    GVM::Core::RenderPassTaskDescriptor task = renderClass.run();
    ASSERT_TRUE(static_cast<bool>(task.drawFn));

    QueueTimestampStats stats;
    GVM::RHI::RenderPassEncoder encoder = new FakeRenderPassEncoder(&stats);

    try
    {
        task.drawFn(encoder);
        FAIL() << "Expected IRenderClass::run() without a RenderSet to throw.";
    }
    catch (const std::logic_error &error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("deprecated"), std::string::npos);
        EXPECT_NE(message.find("RenderSet-only"), std::string::npos);
        EXPECT_NE(message.find("requires a bound RenderSet"), std::string::npos);
    }
}

TEST(RenderClassTaskTests, ParameterlessRenderSetRunUsesEntityInfoRecordStride)
{
    QueueTimestampStats stats;
    FakeQueue fakeQueue(&stats);
    FakeDiagnosticsDevice fakeDevice(
        &fakeQueue,
        &stats,
        GVM::RHI::RuntimeDiagnosticsOverlayConfig{});
    eastl::intrusive_ptr<GVM::Core::RenderSet> renderSet = new GVM::Core::RenderSet();
    GVM::Core::RenderSetCreateInfo createInfo;
    createInfo.renderSetName = "StrideTestRenderSet";
    createInfo.vertexComponentName = "vertices";
    createInfo.indexComponentName = "indices";
    createInfo.componentNameList = {"vertices", "indices"};
    createInfo.componentInfos.emplace(0, GVM::Core::RenderComponentCreateInfo{
                                             .type = GVM::Core::RenderComponentType::BufferComponent,
                                             .dataElementStorageSize = sizeof(uint32_t),
                                             .componentName = "vertices",
                                         });
    createInfo.componentInfos.emplace(1, GVM::Core::RenderComponentCreateInfo{
                                             .type = GVM::Core::RenderComponentType::BufferComponent,
                                             .dataElementStorageSize = sizeof(uint32_t),
                                             .componentName = "indices",
                                         });
    renderSet->create(&fakeDevice, createInfo);

    RenderSetTaskTestRenderClass renderClass;
    renderClass.bindRenderSet(renderSet);
    GVM::Core::RenderPassTaskDescriptor task = renderClass.run();
    ASSERT_TRUE(static_cast<bool>(task.drawFn));

    GVM::RHI::RenderPassEncoder encoder = new FakeRenderPassEncoder(&stats);
    task.drawFn(encoder);

    EXPECT_EQ(stats.lastIndexedIndirectCommandCount, 0u);
    EXPECT_EQ(stats.lastIndexedIndirectStride, sizeof(GVM::Core::RenderEntityInfo));
    renderSet->destroy();
}

TEST(ComputeClassTaskTests, RunTreatsArgumentsAsTotalThreadsAndRoundsToWorkgroups)
{
    QueueTimestampStats stats;
    ComputeClassTaskTestComputeClass computeClass;
    computeClass.setLocalWorkgroupSize(8u, 4u, 2u);

    GVM::Core::ComputePassTaskDescriptor task =
        computeClass.run(17u, 9u, 3u);
    ASSERT_TRUE(static_cast<bool>(task.dispatchFn));

    GVM::RHI::ComputePassEncoder encoder = new FakeComputePassEncoder(&stats);
    task.dispatchFn(encoder);

    EXPECT_EQ(stats.computeDispatchCalls, 1u);
    EXPECT_EQ(stats.lastComputeDispatchX, 3u);
    EXPECT_EQ(stats.lastComputeDispatchY, 3u);
    EXPECT_EQ(stats.lastComputeDispatchZ, 2u);
}
