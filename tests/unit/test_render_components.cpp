#include <gtest/gtest.h>

#include <GRenderBufferComponent.hpp>
#include <GRenderComponentBatchedCopyCommand.hpp>
#include <GRenderSet.hpp>
#include <GRenderTextureComponent.hpp>
#include <GStagingLinearAllocator.hpp>
#include <GGPUVector.hpp>
#include <GGPUVectorFactory.hpp>
#include <GStagingCopyGPUVector.hpp>
#include <GUnifiedMemoryGPUVector.hpp>

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    struct FakeRhiStats
    {
        size_t destroyedBuffers = 0;
        size_t destroyedTextures = 0;
        size_t destroyedTextureViews = 0;
        size_t uploadTextureCalls = 0;
        size_t copyBufferToTextureCalls = 0;
        size_t copyBufferToBufferCalls = 0;
        size_t copyBufferToBufferMultipleRegionCalls = 0;
        size_t copiedBufferRegionCount = 0;
        size_t writeBufferCalls = 0;
        size_t mapBufferCalls = 0;
        size_t unmapBufferCalls = 0;
        eastl::vector<eastl::string> uploadedTextureLabels;
        eastl::vector<uint64_t> uploadedTextureBufferOffsets;
        eastl::vector<uint64_t> uploadedTextureBufferSizes;
        eastl::vector<eastl::vector<uint64_t>> uploadedTextureMipOffsets;
        eastl::vector<eastl::string> copiedTextureLabels;
        eastl::vector<GVM::RHI::Origin3D> copiedTextureOrigins;
        eastl::vector<uint64_t> copiedTextureBufferOffsets;
        eastl::vector<eastl::string> copiedBufferDestinationLabels;
        eastl::vector<uint64_t> copiedBufferBytes;
        eastl::vector<eastl::string> writtenBufferLabels;
        eastl::vector<uint64_t> writtenBufferSizes;
        eastl::vector<eastl::string> createdBufferLabels;
        eastl::vector<GVM::RHI::BufferUsageFlags> createdBufferUsages;
        eastl::vector<eastl::string> destroyedTextureLabels;
    };

    class FakeTextureView final : public GVM::RHI::TextureViewImpl
    {
    public:
        FakeTextureView(FakeRhiStats *stats, eastl::string label, GVM::RHI::TextureFormat format, uint32_t width, uint32_t height)
            : mStats(stats)
            , mFormat(format)
            , mWidth(width)
            , mHeight(height)
            , mDestroyed(false)
        {
            mLabelName = eastl::move(label);
        }

        /** Returns the fake attachment format configured by the parent fake texture. */
        GVM::RHI::TextureFormat getFormat() const override
        {
            return mFormat;
        }

        /** Returns the fake attachment width configured by the parent fake texture. */
        uint32_t getWidth() const override
        {
            return mWidth;
        }

        /** Returns the fake attachment height configured by the parent fake texture. */
        uint32_t getHeight() const override
        {
            return mHeight;
        }

        void destroy() override
        {
            if (mDestroyed)
            {
                return;
            }
            mDestroyed = true;
            mStats->destroyedTextureViews++;
        }

    private:
        FakeRhiStats *mStats = nullptr;
        GVM::RHI::TextureFormat mFormat = GVM::RHI::TextureFormat::Undefined;
        uint32_t mWidth = 0u;
        uint32_t mHeight = 0u;
        bool mDestroyed = false;
    };

    class FakeDevice;

    class FakeBuffer final : public GVM::RHI::BufferImpl
    {
    public:
        FakeBuffer(FakeRhiStats *stats, const GVM::RHI::BufferDescriptor &descriptor)
            : mStats(stats)
            , mStorage(descriptor.size, 0u)
            , mDestroyed(false)
        {
            mLabelName = descriptor.label;
        }

        uint64_t getStorageSize() const override
        {
            return mStorage.size();
        }

        /** Writes a byte span into this fake buffer and fails on out-of-bounds test copies. */
        void writeBytes(uint64_t offset, const void *source, uint64_t size)
        {
            validateRange(offset, size, "FakeBuffer::writeBytes");
            if (size == 0u)
            {
                return;
            }
            if (source == nullptr)
            {
                throw std::invalid_argument("FakeBuffer::writeBytes requires a non-null source for non-empty writes.");
            }
            std::memcpy(mStorage.data() + offset, source, static_cast<size_t>(size));
        }

        /** Copies a byte span from another fake buffer and fails on out-of-bounds test copies. */
        void copyFrom(const FakeBuffer &source, uint64_t sourceOffset, uint64_t destinationOffset, uint64_t size)
        {
            source.validateRange(sourceOffset, size, "FakeBuffer::copyFrom source");
            validateRange(destinationOffset, size, "FakeBuffer::copyFrom destination");
            if (size == 0u)
            {
                return;
            }
            std::memmove(mStorage.data() + destinationOffset, source.mStorage.data() + sourceOffset, static_cast<size_t>(size));
        }

        void map() override
        {
            mStats->mapBufferCalls++;
        }

        void const *getConstMappedRange(uint64_t offset, uint64_t size) const override
        {
            validateRange(offset, size, "FakeBuffer::getConstMappedRange");
            return mStorage.data() + offset;
        }

        void *getMappedRange(uint64_t offset, uint64_t size) const override
        {
            validateRange(offset, size, "FakeBuffer::getMappedRange");
            return mStorage.data() + offset;
        }

        void unmap() override
        {
            mStats->unmapBufferCalls++;
        }

        void destroy() override
        {
            if (mDestroyed)
            {
                return;
            }
            mDestroyed = true;
            mStats->destroyedBuffers++;
        }

    private:
        /** Validates that a test read or write range fits inside the fake buffer storage. */
        void validateRange(uint64_t offset, uint64_t size, const char *operation) const
        {
            if (offset > mStorage.size() || size > (mStorage.size() - offset))
            {
                throw std::out_of_range(operation == nullptr ? "FakeBuffer range is out of bounds." : operation);
            }
        }

        FakeRhiStats *mStats = nullptr;
        mutable std::vector<uint8_t> mStorage;
        bool mDestroyed = false;
    };

    class FakeTexture final : public GVM::RHI::TextureImpl
    {
    public:
        FakeTexture(FakeDevice *device, FakeRhiStats *stats, const GVM::RHI::TextureDescriptor &descriptor);

        GVM::RHI::TextureView createView(const GVM::RHI::TextureViewDescriptor &descriptor) override;
        GVM::RHI::TextureView createView() override;

        uint32_t getWidth() const override
        {
            return mDescriptor.size.width;
        }

        uint32_t getHeight() const override
        {
            return mDescriptor.size.height;
        }

        uint32_t getDepth() const override
        {
            return mDescriptor.size.depth;
        }

        uint32_t getMipLevelCount() const override
        {
            return mDescriptor.mipLevelCount;
        }

        uint32_t getArrayLayerCount() const override
        {
            return mDescriptor.arrayLayerCount;
        }

        GVM::RHI::TextureFormat getFormat() const override
        {
            return mDescriptor.format;
        }

        void destroy() override;

    private:
        GVM::RHI::TextureView ensureDefaultView(const eastl::string &label);

        FakeDevice *mDevice = nullptr;
        FakeRhiStats *mStats = nullptr;
        GVM::RHI::TextureDescriptor mDescriptor = {};
        GVM::RHI::TextureView mDefaultView;
        bool mDestroyed = false;
    };

    class FakeQueue final : public GVM::RHI::QueueImpl
    {
    public:
        explicit FakeQueue(FakeRhiStats *stats)
            : mStats(stats)
        {
        }

        GVM::RHI::CommandEncoder createCommandEncoder() override
        {
            return nullptr;
        }

        void writeBuffer(GVM::RHI::BufferRange buffer, void const *data, uint64_t size) override
        {
            mStats->writeBufferCalls++;
            if (!buffer.buffer.isNull())
            {
                mStats->writtenBufferLabels.push_back(buffer.buffer->getLabelName());
                mStats->writtenBufferSizes.push_back(size);
                requireFakeBuffer(buffer.buffer, "FakeQueue::writeBuffer").writeBytes(buffer.offset, data, size);
            }
        }
        void readBuffer(GVM::RHI::BufferRange, void *, uint64_t) override {}
        void writeTexture(const GVM::RHI::ImageCopyTexture &, void const *, uint64_t, const GVM::RHI::TextureDataLayout &, const GVM::RHI::Extent3D &) override {}
        void readTexture(const GVM::RHI::ImageCopyTexture &, void *, uint64_t, const GVM::RHI::TextureDataLayout &, const GVM::RHI::Extent3D &) override {}

        void uploadTexture(GVM::RHI::Texture destination, void const *, uint64_t, const eastl::vector<uint64_t> &) override
        {
            if (!destination.isNull())
            {
                mStats->uploadTextureCalls++;
                mStats->uploadedTextureLabels.push_back(destination->getLabelName());
                mStats->uploadedTextureBufferOffsets.push_back(0u);
                mStats->uploadedTextureBufferSizes.push_back(0u);
                mStats->uploadedTextureMipOffsets.push_back({});
            }
        }

        void uploadTexture(GVM::RHI::Texture destination, GVM::RHI::BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes) override
        {
            if (!destination.isNull())
            {
                mStats->uploadTextureCalls++;
                mStats->uploadedTextureLabels.push_back(destination->getLabelName());
                mStats->uploadedTextureBufferOffsets.push_back(bufferRange.offset);
                mStats->uploadedTextureBufferSizes.push_back(bufferRange.size);
                mStats->uploadedTextureMipOffsets.push_back(mipmapOffsetBytes);
            }
        }

        void copyBufferToBuffer(GVM::RHI::BufferRange source, GVM::RHI::BufferRange destination) override
        {
            mStats->copyBufferToBufferCalls++;
            auto &sourceBuffer = requireFakeBuffer(source.buffer, "FakeQueue::copyBufferToBuffer source");
            auto &destinationBuffer = requireFakeBuffer(destination.buffer, "FakeQueue::copyBufferToBuffer destination");
            destinationBuffer.copyFrom(sourceBuffer, source.offset, destination.offset, source.size);
            if (!destination.buffer.isNull())
            {
                mStats->copiedBufferDestinationLabels.push_back(destination.buffer->getLabelName());
                mStats->copiedBufferBytes.push_back(source.size);
            }
        }
        void copyBufferToTexture(const GVM::RHI::ImageCopyBuffer &source, const GVM::RHI::ImageCopyTexture &destination, const GVM::RHI::Extent3D &) override
        {
            mStats->copyBufferToTextureCalls++;
            if (!destination.texture.isNull())
            {
                mStats->copiedTextureLabels.push_back(destination.texture->getLabelName());
                mStats->copiedTextureOrigins.push_back(destination.origin);
                mStats->copiedTextureBufferOffsets.push_back(source.layout.offset);
            }
        }
        void copyTextureToBuffer(const GVM::RHI::ImageCopyTexture &, const GVM::RHI::ImageCopyBuffer &, const GVM::RHI::Extent3D &) override {}
        void copyBufferToBufferMultipleRegion(GVM::RHI::Buffer source, GVM::RHI::Buffer destination, GVM::RHI::Buffer commandBuffer, uint32_t regionCount) override
        {
            mStats->copyBufferToBufferMultipleRegionCalls++;
            mStats->copiedBufferRegionCount += regionCount;
            auto &sourceBuffer = requireFakeBuffer(source, "FakeQueue::copyBufferToBufferMultipleRegion source");
            auto &destinationBuffer = requireFakeBuffer(destination, "FakeQueue::copyBufferToBufferMultipleRegion destination");
            auto &regionBuffer = requireFakeBuffer(commandBuffer, "FakeQueue::copyBufferToBufferMultipleRegion command");
            const auto *regions = static_cast<const GVM::RHI::BufferCopyRegion *>(regionBuffer.getConstMappedRange(0u, regionCount * sizeof(GVM::RHI::BufferCopyRegion)));
            uint64_t copiedBytes = 0u;
            for (uint32_t regionIndex = 0; regionIndex < regionCount; ++regionIndex)
            {
                destinationBuffer.copyFrom(sourceBuffer, regions[regionIndex].srcOffset, regions[regionIndex].dstOffset, regions[regionIndex].size);
                copiedBytes += regions[regionIndex].size;
            }
            if (!destination.isNull())
            {
                mStats->copiedBufferDestinationLabels.push_back(destination->getLabelName());
                mStats->copiedBufferBytes.push_back(copiedBytes);
            }
        }
        void fillBuffer(GVM::RHI::BufferRange, uint32_t) override {}
        void submit(const eastl::vector<GVM::RHI::CommandEncoder> &) override {}
        void destroy() override {}

    private:
        /** Returns the FakeBuffer backing a test buffer handle and reports invalid test setup explicitly. */
        static FakeBuffer &requireFakeBuffer(GVM::RHI::Buffer buffer, const char *operation)
        {
            if (buffer.isNull())
            {
                throw std::invalid_argument(operation == nullptr ? "FakeQueue requires a non-null buffer." : operation);
            }
            auto *fakeBuffer = dynamic_cast<FakeBuffer *>(buffer.get());
            if (fakeBuffer == nullptr)
            {
                throw std::invalid_argument(operation == nullptr ? "FakeQueue expected a FakeBuffer." : operation);
            }
            return *fakeBuffer;
        }

        FakeRhiStats *mStats = nullptr;
    };

    class FakeDevice final : public GVM::RHI::DeviceImpl
    {
    public:
        explicit FakeDevice(GVM::RHI::DeviceMemoryProperties memoryProperties = {})
            : mMemoryProperties(memoryProperties)
            , mQueue(&mStats)
        {
            mBufferPool.init();
            mTexturePool.init();
            mTextureViewPool.init();
        }

        GVM::RHI::Queue getMainQueue() const override
        {
            return const_cast<FakeQueue *>(&mQueue);
        }

        GVM::RHI::TimestampQuerySupport getTimestampQuerySupport() const override
        {
            return {};
        }

        /** Returns the fake memory topology configured by the test case. */
        GVM::RHI::DeviceMemoryProperties getMemoryProperties() const override
        {
            return mMemoryProperties;
        }

        /** Returns the default disabled diagnostics overlay configuration for unit fakes. */
        GVM::RHI::RuntimeDiagnosticsOverlayConfig getDiagnosticsOverlayConfig() const override
        {
            return {};
        }

        /** Returns an empty diagnostics snapshot because these unit fakes do not track live resources globally. */
        GVM::RHI::DiagnosticsResourceSnapshot getDiagnosticsResourceSnapshot() const override
        {
            return {};
        }

        GVM::RHI::Buffer createBuffer(const GVM::RHI::BufferDescriptor &descriptor) override
        {
            mStats.createdBufferLabels.push_back(descriptor.label);
            mStats.createdBufferUsages.push_back(descriptor.usage);
            return mBufferPool.alloc(new FakeBuffer(&mStats, descriptor));
        }

        GVM::RHI::QuerySet createQuerySet(const GVM::RHI::QuerySetDescriptor &) override
        {
            return nullptr;
        }

        GVM::RHI::Texture createTexture(const GVM::RHI::TextureDescriptor &descriptor) override
        {
            return mTexturePool.alloc(new FakeTexture(this, &mStats, descriptor));
        }

        GVM::RHI::ComputePipeline createComputePipeline(const GVM::RHI::ComputePipelineDescriptor &) override
        {
            return nullptr;
        }

        GVM::RHI::RenderPipeline createRenderPipeline(const GVM::RHI::RenderPipelineDescriptor &) override
        {
            return nullptr;
        }

        GVM::RHI::ShaderModule createShaderModule(const GVM::RHI::ShaderModuleDescriptor &) override
        {
            return nullptr;
        }

        GVM::RHI::BindGroupLayout createBindGroupLayout(const GVM::RHI::BindGroupLayoutDescriptor &) override
        {
            return nullptr;
        }

        GVM::RHI::PipelineLayout createPipelineLayout(const GVM::RHI::PipelineLayoutDescriptor &) override
        {
            return nullptr;
        }

        GVM::RHI::BindGroup createBindGroup(const GVM::RHI::BindGroupDescriptor &) override
        {
            return nullptr;
        }

        GVM::RHI::Sampler createSampler(const GVM::RHI::SamplerDescriptor &) override
        {
            return {};
        }

        GVM::RHI::Logger getLogger() const override
        {
            return {};
        }

        void freeBuffer(GVM::RHI::Buffer buffer) override
        {
            if (buffer.isNull())
            {
                return;
            }
            buffer->destroy();
            mBufferPool.freeByHandle(buffer);
        }

        void freeTexture(GVM::RHI::Texture texture) override
        {
            if (texture.isNull())
            {
                return;
            }
            texture->destroy();
            mTexturePool.freeByHandle(texture);
        }

        void freeSampler(GVM::RHI::Sampler) override {}

        void destroy() override {}

        GVM::RHI::TextureViewPool mTextureViewPool;
        FakeRhiStats mStats;

    private:
        GVM::RHI::DeviceMemoryProperties mMemoryProperties = {};
        GVM::RHI::BufferPool mBufferPool;
        GVM::RHI::TexturePool mTexturePool;
        mutable FakeQueue mQueue;
    };

    FakeTexture::FakeTexture(FakeDevice *device, FakeRhiStats *stats, const GVM::RHI::TextureDescriptor &descriptor)
        : mDevice(device)
        , mStats(stats)
        , mDescriptor(descriptor)
        , mDestroyed(false)
    {
        mLabelName = descriptor.label;
    }

    GVM::RHI::TextureView FakeTexture::ensureDefaultView(const eastl::string &label)
    {
        if (mDefaultView.isNull())
        {
            mDefaultView = mDevice->mTextureViewPool.alloc(new FakeTextureView(mStats, label, mDescriptor.format, mDescriptor.size.width, mDescriptor.size.height));
        }
        return mDefaultView;
    }

    GVM::RHI::TextureView FakeTexture::createView(const GVM::RHI::TextureViewDescriptor &descriptor)
    {
        return ensureDefaultView(descriptor.label.empty() ? (mLabelName + "_View") : descriptor.label);
    }

    GVM::RHI::TextureView FakeTexture::createView()
    {
        return ensureDefaultView(mLabelName + "_DefaultView");
    }

    void FakeTexture::destroy()
    {
        if (mDestroyed)
        {
            return;
        }
        mDestroyed = true;
        mStats->destroyedTextureLabels.push_back(mLabelName);
        if (!mDefaultView.isNull())
        {
            mDefaultView->destroy();
            mDevice->mTextureViewPool.freeByHandle(mDefaultView);
            mDefaultView.reset();
        }
        mStats->destroyedTextures++;
    }

    GVM::Core::RenderSetTextureComponentTextureInfo makeTextureInfo(const char *name)
    {
        static std::array<uint8_t, 16> pixels = {};
        GVM::Core::RenderSetTextureComponentTextureInfo info = {};
        info.textureName = name;
        info.format = GVM::RHI::TextureFormat::RGBA8Unorm;
        info.width = 2;
        info.height = 2;
        info.data = pixels.data();
        info.dataStorageBytes = pixels.size();
        info.mipmapOffsetBytes = {0};
        return info;
    }

    GVM::Core::RenderSetCreateInfo makeSingleVertexRenderSetCreateInfo()
    {
        GVM::Core::RenderSetCreateInfo info = {};
        info.renderSetName = "UnitTestRenderSet";
        info.vertexComponentName = "Vertex";
        info.componentNameList = {"Vertex"};
        info.componentInfos.emplace(0, GVM::Core::RenderComponentCreateInfo{
                                           .type = GVM::Core::RenderComponentType::BufferComponent,
                                           .dataElementStorageSize = sizeof(uint32_t),
                                           .componentName = "Vertex",
                                       });
        return info;
    }

    GVM::Core::RenderSetCreateInfo makeSingleTextureRenderSetCreateInfo()
    {
        GVM::Core::RenderSetCreateInfo info = {};
        info.renderSetName = "UnitTestTextureRenderSet";
        info.componentNameList = {"Albedo"};
        info.componentInfos.emplace(0, GVM::Core::RenderComponentCreateInfo{
                                           .type = GVM::Core::RenderComponentType::TextureComponent,
                                           .maxResourceCount = 8,
                                           .componentName = "Albedo",
                                       });
        return info;
    }

    GVM::Core::RenderSetCreateInfo makeBufferAndTextureRenderSetCreateInfo()
    {
        GVM::Core::RenderSetCreateInfo info = {};
        info.renderSetName = "UnitTestBufferAndTextureRenderSet";
        info.vertexComponentName = "Vertex";
        info.componentNameList = {"Vertex", "Albedo"};
        info.componentInfos.emplace(0, GVM::Core::RenderComponentCreateInfo{
                                           .type = GVM::Core::RenderComponentType::BufferComponent,
                                           .dataElementStorageSize = sizeof(uint32_t),
                                           .componentName = "Vertex",
                                       });
        info.componentInfos.emplace(1, GVM::Core::RenderComponentCreateInfo{
                                           .type = GVM::Core::RenderComponentType::TextureComponent,
                                           .maxResourceCount = 8,
                                           .componentName = "Albedo",
                                       });
        return info;
    }

    /** Returns a typed read-only pointer into a FakeBuffer-backed test RHI buffer. */
    template <class T>
    const T *readFakeBufferElements(GVM::RHI::Buffer buffer, uint64_t elementOffset, uint64_t elementCount)
    {
        if (buffer.isNull())
        {
            throw std::invalid_argument("readFakeBufferElements requires a non-null buffer.");
        }
        auto *fakeBuffer = dynamic_cast<FakeBuffer *>(buffer.get());
        if (fakeBuffer == nullptr)
        {
            throw std::invalid_argument("readFakeBufferElements requires a FakeBuffer-backed test buffer.");
        }
        return static_cast<const T *>(fakeBuffer->getConstMappedRange(elementOffset * sizeof(T), elementCount * sizeof(T)));
    }

    /** Builds deterministic RenderSet streaming payload values for large-buffer verification. */
    uint32_t makeStreamingPayloadValue(uint32_t entity, uint32_t element)
    {
        return 0xA5000000u ^ (entity * 131u) ^ (element * 17u);
    }
} // namespace

namespace GVM::Core
{
    class BufferComponentTestPeer
    {
    public:
        static RenderComponentIndex alloc(BufferComponent &component, RenderEntityIndex entity, const eastl::string &bufferName, uint64_t dataStorageSize, uint32_t instanceCount)
        {
            return component.alloc(entity, bufferName, dataStorageSize, instanceCount);
        }

        static RenderComponentIndexCopyInfo getAllocInfo(BufferComponent &component, RenderEntityIndex entity, StagingLinearAllocator &allocator)
        {
            return component.getComponentIndexAllocInfo(entity, allocator);
        }

        static RenderComponentIndexCopyInfo getRemoveInfo(BufferComponent &component, RenderEntityIndex entity, StagingLinearAllocator &allocator)
        {
            return component.getComponentIndexRemoveInfo(entity, allocator);
        }

        /** Returns the byte offset where one entity's component payload is streamed into the GPU data buffer. */
        static uint32_t getDataBufferCopyOffset(const BufferComponent &component, RenderEntityIndex entity, uint32_t instanceStartIndex)
        {
            return component.getDataBufferCopyOffset(entity, instanceStartIndex);
        }

        /** Returns the current GPU data buffer generation used by RenderSet rebinding tests. */
        static uint64_t getDataBufferGeneration(const BufferComponent &component)
        {
            return component.getResourceGenerationCounter();
        }

        /** Returns the current GPU component-index buffer generation used by RenderSet rebinding tests. */
        static uint64_t getComponentIndexBufferGeneration(const BufferComponent &component)
        {
            return component.getComponentIndexBufferGenerationCounter();
        }
    };

    class TextureComponentTestPeer
    {
    public:
        static void alloc(TextureComponent &component, RenderEntityIndex entity, const RenderSetTextureComponentAllocInfo &allocInfo)
        {
            component.alloc(entity, allocInfo);
        }

        static void performTextureCopy(TextureComponent &component, const RenderSetTextureComponentTextureCopyCommand &command)
        {
            component.performTextureCopy(command);
        }

        static RenderComponentIndexCopyInfo getAllocInfo(TextureComponent &component, RenderEntityIndex entity, StagingLinearAllocator &allocator)
        {
            return component.getComponentIndexAllocInfo(entity, allocator);
        }

        static RenderComponentIndexCopyInfo getRemoveInfo(TextureComponent &component, RenderEntityIndex entity, StagingLinearAllocator &allocator)
        {
            return component.getComponentIndexRemoveInfo(entity, allocator);
        }

        static uint64_t getPerEntityComponentIndexStorageBytes(const TextureComponent &component)
        {
            return component.getPerEntityComponentIndexStorageBytes();
        }

        static uint64_t getComponentIndexElementCountForRenderSetAccessBounds(const TextureComponent &component)
        {
            return component.getComponentIndexElementCountForRenderSetAccessBounds();
        }

        static uint64_t getComponentIndexBufferGeneration(const TextureComponent &component)
        {
            return component.getComponentIndexBufferGenerationCounter();
        }
    };

    class RenderSetCommandEncoderTestPeer
    {
    public:
        static size_t getPendingTextureCopyCount(const RenderSetCommandEncoderImpl &encoder, RenderComponentHandle handle)
        {
            const auto it = encoder.mComponentTexturePendingCopies.find(handle);
            return it == encoder.mComponentTexturePendingCopies.end() ? 0u : it->second.textures.size();
        }

        static size_t getPendingComponentIndexCopyRegionCount(const RenderSetCommandEncoderImpl &encoder, RenderComponentHandle handle)
        {
            const auto it = encoder.mComponentIndexBufferPendingCopies.find(handle);
            return it == encoder.mComponentIndexBufferPendingCopies.end() ? 0u : it->second.regions.size();
        }

        static size_t getPendingRemoveCount(const RenderSetCommandEncoderImpl &encoder)
        {
            return encoder.mPendingRemoves.size();
        }
    };

    class RenderSetTestPeer
    {
    public:
        static GVM::Core::RenderEntityInfo getEntityInfo(const GVM::Core::RenderSet &renderSet, GVM::Core::RenderEntityIndex entity)
        {
            return renderSet.mRenderEntityInfos.read(entity);
        }

        static GVM::Core::RenderEntityCMDParam getCMDParam(const GVM::Core::RenderSet &renderSet, uint32_t cmdParamIndex)
        {
            return renderSet.mRenderEntityCMDParamsInfos.read(cmdParamIndex);
        }

        /** Returns the RenderSet access-bound GPU buffer so tests can verify published capacity data. */
        static GVM::RHI::Buffer getAccessBoundDataBuffer(const GVM::Core::RenderSet &renderSet)
        {
            return renderSet.mRenderSetAccessBoundDataBuffer;
        }

        /** Returns the RenderEntityInfo GPUVector generation used by streaming growth tests. */
        static uint64_t getRenderEntityInfoBufferGeneration(const GVM::Core::RenderSet &renderSet)
        {
            return renderSet.mRenderEntityInfoBuffer != nullptr ? renderSet.mRenderEntityInfoBuffer->generation() : 0u;
        }

        /** Returns the RenderEntityCMDParams GPUVector generation used by streaming growth tests. */
        static uint64_t getRenderEntityCMDParamsBufferGeneration(const GVM::Core::RenderSet &renderSet)
        {
            return renderSet.mRenderEntityCMDParamsBuffer != nullptr ? renderSet.mRenderEntityCMDParamsBuffer->generation() : 0u;
        }
    };
} // namespace GVM::Core

namespace
{
    TEST(BufferComponentTests, AllocRoundsTailBytesUpToAFullAlignedBlockSpan)
    {
        FakeDevice device;
        GVM::Core::BufferComponent component;
        component.create(&device, {
                                      .bufferComponentName = "TailAlignedBuffer",
                                      .dataElementStorageSize = sizeof(uint32_t),
                                      .dataElementCountInBlock = 4,
                                      .dataElementIncreamentCount = 8,
                                  });

        const auto firstIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 0, "Tail17", 17u, 1u);
        const auto secondIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 1, "Tail4", sizeof(uint32_t), 1u);

        EXPECT_EQ(firstIndex, 0u);
        EXPECT_EQ(secondIndex, 8u);

        component.destroy();
    }

    TEST(BufferComponentTests, InstanceCountContributesToAllocatedStorageFootprint)
    {
        FakeDevice device;
        GVM::Core::BufferComponent component;
        component.create(&device, {
                                      .bufferComponentName = "InstanceSizedBuffer",
                                      .dataElementStorageSize = sizeof(uint32_t),
                                      .dataElementCountInBlock = 4,
                                      .dataElementIncreamentCount = 8,
                                  });

        const auto firstIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 0, "Instanced", sizeof(uint32_t), 5u);
        const auto secondIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 1, "Follower", sizeof(uint32_t), 1u);

        EXPECT_EQ(firstIndex, 0u);
        EXPECT_EQ(secondIndex, 8u);

        component.destroy();
    }

    TEST(BufferComponentTests, NamedBuffersCanBeReusedByNameOnlyWithoutRedeclaringStorage)
    {
        FakeDevice device;
        GVM::Core::BufferComponent component;
        component.create(&device, {
                                      .bufferComponentName = "SharedNamedBuffer",
                                      .dataElementStorageSize = sizeof(uint32_t),
                                      .dataElementCountInBlock = 4,
                                      .dataElementIncreamentCount = 8,
                                  });

        const auto firstIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 0, "Shared", sizeof(uint32_t) * 4, 1u);
        const auto secondIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 1, "Shared", 0u, 1u);

        EXPECT_EQ(firstIndex, secondIndex);
        EXPECT_TRUE(component.check("Shared"));
        EXPECT_EQ(component.getComponentStorageByteByName("Shared"), sizeof(uint32_t) * 4);

        component.remove(0);
        EXPECT_TRUE(component.check("Shared"));

        component.remove(1);
        EXPECT_FALSE(component.check("Shared"));

        const auto recycledIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 2, "Recycled", sizeof(uint32_t) * 4, 1u);
        EXPECT_EQ(recycledIndex, firstIndex);

        component.destroy();
    }

    TEST(BufferComponentTests, NamedBuffersRejectMismatchedExplicitStorageRedeclarations)
    {
        FakeDevice device;
        GVM::Core::BufferComponent component;
        component.create(&device, {
                                      .bufferComponentName = "StrictNamedBuffer",
                                      .dataElementStorageSize = sizeof(uint32_t),
                                      .dataElementCountInBlock = 4,
                                      .dataElementIncreamentCount = 8,
                                  });

        [[maybe_unused]] const auto firstIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 0, "Shared", sizeof(uint32_t) * 4, 1u);

        EXPECT_THROW(
            {
                [[maybe_unused]] const auto mismatchIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 1, "Shared", sizeof(uint32_t) * 2, 1u);
            },
            std::invalid_argument);

        component.destroy();
    }

    TEST(BufferComponentTests, ComponentIndexCopyCommandsMirrorAllocatedAndRemovedState)
    {
        FakeDevice device;
        GVM::Core::BufferComponent component;
        component.create(&device, {
                                      .bufferComponentName = "CopyStateBuffer",
                                      .dataElementStorageSize = sizeof(uint32_t),
                                      .dataElementCountInBlock = 4,
                                      .dataElementIncreamentCount = 8,
                                  });

        EXPECT_FALSE(component.check(0));

        const auto allocatedIndex = GVM::Core::BufferComponentTestPeer::alloc(component, 0, "CopyMe", sizeof(uint32_t) * 4, 1u);
        EXPECT_TRUE(component.check(0));

        GVM::Core::StagingLinearAllocator allocator;
        const auto allocInfo = GVM::Core::BufferComponentTestPeer::getAllocInfo(component, 0, allocator);
        const auto *copiedIndex = reinterpret_cast<const GVM::Core::RenderComponentIndex *>(allocator.data() + allocInfo.CPUDataSrcOffset);
        ASSERT_NE(copiedIndex, nullptr);
        EXPECT_EQ(*copiedIndex, allocatedIndex);

        GVM::Core::StagingLinearAllocator removeAllocator;
        const auto removeInfo = GVM::Core::BufferComponentTestPeer::getRemoveInfo(component, 0, removeAllocator);
        const auto *removedIndex = reinterpret_cast<const GVM::Core::RenderComponentIndex *>(removeAllocator.data() + removeInfo.CPUDataSrcOffset);
        ASSERT_NE(removedIndex, nullptr);
        EXPECT_EQ(*removedIndex, GVM::Core::RenderComponentNullIndex);

        component.remove(0);
        EXPECT_FALSE(component.check(0));

        component.destroy();
    }

    TEST(RenderSetTests, NonIndexedEntitiesRemainValidAndDirectRemoveReleasesComponentAllocations)
    {
        FakeDevice device;
        GVM::Core::RenderSet renderSet;
        renderSet.create(&device, makeSingleVertexRenderSetCreateInfo());

        GVM::Core::RenderSetAllocInfo allocInfo = {};
        allocInfo.verticesCount = 3;
        allocInfo.indicesCount = 0;
        allocInfo.instanceCount = 1;
        allocInfo.bufferInfos.push_back({
            .bufferComponentHandle = renderSet.createRenderComponentHandle(0),
            .bufferName = "SharedVertexData",
            .value = nullptr,
            .dataStorageSize = sizeof(uint32_t) * 3,
            .instanceCount = 1,
        });

        const auto entity = renderSet.alloc(allocInfo);
        auto vertexComponent = renderSet.getBufferComponentByName("Vertex");

        ASSERT_TRUE(renderSet.check(entity));
        ASSERT_NE(vertexComponent, nullptr);
        EXPECT_TRUE(vertexComponent->check(entity));
        EXPECT_TRUE(vertexComponent->check("SharedVertexData"));

        renderSet.remove(entity);

        EXPECT_FALSE(renderSet.check(entity));
        EXPECT_FALSE(vertexComponent->check(entity));
        EXPECT_FALSE(vertexComponent->check("SharedVertexData"));

        renderSet.destroy();
    }

    TEST(RenderSetTests, AllocRollsBackPartialComponentAllocationsWhenAHandleIsInvalid)
    {
        FakeDevice device;
        GVM::Core::RenderSet renderSet;
        renderSet.create(&device, makeSingleVertexRenderSetCreateInfo());

        auto vertexComponent = renderSet.getBufferComponentByName("Vertex");
        ASSERT_NE(vertexComponent, nullptr);

        GVM::Core::RenderSetAllocInfo allocInfo = {};
        allocInfo.verticesCount = 3;
        allocInfo.instanceCount = 1;
        allocInfo.bufferInfos.push_back({
            .bufferComponentHandle = renderSet.createRenderComponentHandle(0),
            .bufferName = "TransientVertexData",
            .value = nullptr,
            .dataStorageSize = sizeof(uint32_t) * 3,
            .instanceCount = 1,
        });
        allocInfo.bufferInfos.push_back({
            .bufferComponentHandle = 99,
            .bufferName = "InvalidHandle",
            .value = nullptr,
            .dataStorageSize = sizeof(uint32_t),
            .instanceCount = 1,
        });

        EXPECT_THROW(
            {
                [[maybe_unused]] const auto entity = renderSet.alloc(allocInfo);
            },
            std::out_of_range);

        EXPECT_FALSE(vertexComponent->check("TransientVertexData"));

        renderSet.destroy();
    }

    TEST(RenderSetTests, AllocatesDynamicGlobalInstanceBaseForLargeEntities)
    {
        FakeDevice device;
        GVM::Core::RenderSet renderSet;
        renderSet.create(&device, makeSingleVertexRenderSetCreateInfo());

        const auto allocEntityWithInstanceCount = [&](uint32_t instanceCount) {
            GVM::Core::RenderSetAllocInfo allocInfo = {};
            allocInfo.verticesCount = 3;
            allocInfo.instanceCount = instanceCount;
            allocInfo.bufferInfos.push_back({
                .bufferComponentHandle = renderSet.createRenderComponentHandle(0),
                .bufferName = "",
                .value = nullptr,
                .dataStorageSize = sizeof(uint32_t) * 3,
                .instanceCount = 1,
            });
            return renderSet.alloc(allocInfo);
        };

        const auto entityA = allocEntityWithInstanceCount(4097u);
        const auto entityB = allocEntityWithInstanceCount(8192u);
        const auto entityC = allocEntityWithInstanceCount(65536u);

        const auto infoA = GVM::Core::RenderSetTestPeer::getEntityInfo(renderSet, entityA);
        const auto infoB = GVM::Core::RenderSetTestPeer::getEntityInfo(renderSet, entityB);
        const auto infoC = GVM::Core::RenderSetTestPeer::getEntityInfo(renderSet, entityC);

        EXPECT_EQ(infoA.instanceCount, 4097u);
        EXPECT_EQ(infoB.instanceCount, 8192u);
        EXPECT_EQ(infoC.instanceCount, 65536u);
        EXPECT_EQ(infoA.globalInstanceBase, infoA.cmdParamsOffset);
        EXPECT_EQ(infoB.globalInstanceBase, infoB.cmdParamsOffset);
        EXPECT_EQ(infoC.globalInstanceBase, infoC.cmdParamsOffset);
        EXPECT_GE(infoB.globalInstanceBase, infoA.globalInstanceBase + infoA.instanceCount);
        EXPECT_GE(infoC.globalInstanceBase, infoB.globalInstanceBase + infoB.instanceCount);

        const auto expectCMDParam = [&](const GVM::Core::RenderEntityInfo &info,
                                        GVM::Core::RenderEntityIndex entity,
                                        uint32_t localInstanceIndex) {
            const auto cmdParam = GVM::Core::RenderSetTestPeer::getCMDParam(
                renderSet,
                info.cmdParamsOffset + localInstanceIndex);
            EXPECT_EQ(cmdParam.entity, entity);
            EXPECT_EQ(cmdParam.instanceIndex, localInstanceIndex);
        };

        expectCMDParam(infoA, entityA, 0u);
        expectCMDParam(infoA, entityA, 4095u);
        expectCMDParam(infoA, entityA, 4096u);
        expectCMDParam(infoB, entityB, 0u);
        expectCMDParam(infoB, entityB, 4095u);
        expectCMDParam(infoB, entityB, 4096u);
        expectCMDParam(infoC, entityC, 0u);
        expectCMDParam(infoC, entityC, 4095u);
        expectCMDParam(infoC, entityC, 4096u);
        expectCMDParam(infoC, entityC, infoC.instanceCount - 1u);

        renderSet.destroy();
    }

    TEST(RenderSetTests, ReusesFreedDynamicGlobalInstanceRangeWithoutOverlap)
    {
        FakeDevice device;
        GVM::Core::RenderSet renderSet;
        renderSet.create(&device, makeSingleVertexRenderSetCreateInfo());

        const auto allocEntityWithInstanceCount = [&](uint32_t instanceCount) {
            GVM::Core::RenderSetAllocInfo allocInfo = {};
            allocInfo.verticesCount = 3;
            allocInfo.instanceCount = instanceCount;
            allocInfo.bufferInfos.push_back({
                .bufferComponentHandle = renderSet.createRenderComponentHandle(0),
                .bufferName = "",
                .value = nullptr,
                .dataStorageSize = sizeof(uint32_t) * 3,
                .instanceCount = 1,
            });
            return renderSet.alloc(allocInfo);
        };

        const auto entityA = allocEntityWithInstanceCount(4097u);
        const auto entityB = allocEntityWithInstanceCount(8192u);
        const auto infoA = GVM::Core::RenderSetTestPeer::getEntityInfo(renderSet, entityA);
        const auto infoB = GVM::Core::RenderSetTestPeer::getEntityInfo(renderSet, entityB);

        renderSet.remove(entityA);

        const auto entityC = allocEntityWithInstanceCount(4097u);
        const auto infoC = GVM::Core::RenderSetTestPeer::getEntityInfo(renderSet, entityC);

        EXPECT_EQ(infoC.globalInstanceBase, infoA.globalInstanceBase);
        EXPECT_EQ(infoC.cmdParamsOffset, infoA.cmdParamsOffset);
        EXPECT_GE(infoB.globalInstanceBase, infoC.globalInstanceBase + infoC.instanceCount);

        renderSet.destroy();
    }

    TEST(RenderComponentBatchedCopyCommandTests, SplitsLargeContiguousCopiesIntoBoundedRegions)
    {
        GVM::Core::RenderComponentBatchedCopyCommand command = {};
        command.add(64u, 512u, 10240u);

        ASSERT_EQ(command.regions.size(), 3u);
        EXPECT_EQ(command.regions[0].srcOffset, 64u);
        EXPECT_EQ(command.regions[0].dstOffset, 512u);
        EXPECT_EQ(command.regions[0].size, GVM::Core::RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion);

        EXPECT_EQ(command.regions[1].srcOffset, 64u + GVM::Core::RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion);
        EXPECT_EQ(command.regions[1].dstOffset, 512u + GVM::Core::RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion);
        EXPECT_EQ(command.regions[1].size, GVM::Core::RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion);

        EXPECT_EQ(command.regions[2].srcOffset, 64u + (2u * GVM::Core::RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion));
        EXPECT_EQ(command.regions[2].dstOffset, 512u + (2u * GVM::Core::RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion));
        EXPECT_EQ(command.regions[2].size, 2048u);
    }

    TEST(RenderComponentBatchedCopyCommandTests, KeepsExactBoundedCopiesAsSingleRegion)
    {
        GVM::Core::RenderComponentBatchedCopyCommand command = {};
        command.add(128u, 256u, GVM::Core::RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion);

        ASSERT_EQ(command.regions.size(), 1u);
        EXPECT_EQ(command.regions[0].srcOffset, 128u);
        EXPECT_EQ(command.regions[0].dstOffset, 256u);
        EXPECT_EQ(command.regions[0].size, GVM::Core::RenderComponentBatchedCopyCommand::MaxCopyBytesPerRegion);
    }

    TEST(RenderSetCommandEncoderTests, MultiMipTextureUploadKeepsMipOffsetsRelativeToItsStagingPayload)
    {
        FakeDevice device;
        GVM::Core::RenderSet renderSet;
        renderSet.create(&device, makeBufferAndTextureRenderSetCreateInfo());

        std::array<uint32_t, 1> vertexPayload = {0x12345678u};
        std::array<uint8_t, 84> mipPayload = {};
        GVM::Core::RenderSetAllocInfo allocInfo = {};
        allocInfo.verticesCount = 1;
        allocInfo.instanceCount = 1;
        allocInfo.bufferInfos.push_back({
            .bufferComponentHandle = renderSet.createRenderComponentHandle(0),
            .bufferName = "VertexPayload",
            .value = vertexPayload.data(),
            .dataStorageSize = sizeof(vertexPayload),
            .instanceCount = 1,
        });
        allocInfo.textureInfos.push_back({
            .textureComponentHandle = renderSet.createRenderComponentHandle(1),
            .textures =
                {
                    {
                        .textureName = "TerrainAtlas",
                        .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                        .width = 4,
                        .height = 4,
                        .data = mipPayload.data(),
                        .dataStorageBytes = mipPayload.size(),
                        .mipmapOffsetBytes = {0, 64, 80},
                    },
                },
        });

        auto encoder = eastl::static_pointer_cast<GVM::Core::RenderSetCommandEncoderImpl>(renderSet.createEncoder());
        ASSERT_NE(encoder, nullptr);

        [[maybe_unused]] const auto entity = encoder->allocEntity(allocInfo);

        EXPECT_EQ(GVM::Core::RenderSetCommandEncoderTestPeer::getPendingTextureCopyCount(*encoder, renderSet.createRenderComponentHandle(1)), 1u);
        EXPECT_EQ(GVM::Core::RenderSetCommandEncoderTestPeer::getPendingComponentIndexCopyRegionCount(*encoder, renderSet.createRenderComponentHandle(1)), 1u);

        encoder->processInUserThread();
        encoder->processInternal();

        EXPECT_EQ(device.mStats.copyBufferToTextureCalls, 0u);
        EXPECT_EQ(device.mStats.uploadTextureCalls, 1u);
        ASSERT_EQ(device.mStats.uploadedTextureLabels.size(), 1u);
        EXPECT_EQ(device.mStats.uploadedTextureLabels[0], "TerrainAtlas");
        ASSERT_EQ(device.mStats.uploadedTextureBufferOffsets.size(), 1u);
        EXPECT_GT(device.mStats.uploadedTextureBufferOffsets[0], 0u);
        ASSERT_EQ(device.mStats.uploadedTextureBufferSizes.size(), 1u);
        EXPECT_EQ(device.mStats.uploadedTextureBufferSizes[0], mipPayload.size());
        ASSERT_EQ(device.mStats.uploadedTextureMipOffsets.size(), 1u);
        EXPECT_EQ(device.mStats.uploadedTextureMipOffsets[0], (eastl::vector<uint64_t>{0, 64, 80}));

        const auto textureComponent = renderSet.getTextureComponentByName("Albedo");
        ASSERT_NE(textureComponent, nullptr);
        const auto *componentIndices = readFakeBufferElements<GVM::Core::RenderComponentIndex>(
            textureComponent->getComponentIndexBuffer(),
            0u,
            GVM::Core::RenderTextureMaxTextureCountPerEntity);
        EXPECT_EQ(componentIndices[0], 0u);
        for (uint32_t slot = 1u; slot < GVM::Core::RenderTextureMaxTextureCountPerEntity; ++slot)
        {
            EXPECT_EQ(componentIndices[slot], GVM::Core::RenderComponentNullIndex);
        }

        encoder->destroy();
        renderSet.destroy();
    }

    TEST(RenderSetCommandEncoderTests, UpdatesOneAllocatedBufferComponentRangeInPlace)
    {
        FakeDevice device;
        GVM::Core::RenderSet renderSet;
        renderSet.create(&device, makeSingleVertexRenderSetCreateInfo());

        const std::array<uint32_t, 4u> initialPayload = {10u, 20u, 30u, 40u};
        GVM::Core::RenderSetAllocInfo allocInfo = {};
        allocInfo.verticesCount = static_cast<uint32_t>(initialPayload.size());
        allocInfo.instanceCount = static_cast<uint32_t>(initialPayload.size());
        allocInfo.bufferInfos.push_back({
            .bufferComponentHandle = renderSet.createRenderComponentHandle(0),
            .bufferName = "MutableInstances",
            .value = initialPayload.data(),
            .dataStorageSize = sizeof(initialPayload),
            .instanceCount = static_cast<uint32_t>(initialPayload.size()),
        });

        auto allocationEncoder = renderSet.createEncoder();
        ASSERT_NE(allocationEncoder, nullptr);
        const GVM::Core::RenderEntityIndex entity = allocationEncoder->allocEntity(allocInfo);
        renderSet.executeCommand(allocationEncoder);
        renderSet.update();

        const uint32_t replacement = 99u;
        auto updateEncoder = renderSet.createEncoder();
        ASSERT_NE(updateEncoder, nullptr);
        updateEncoder->setBufferComponentData(
            entity,
            renderSet.createRenderComponentHandle(0),
            &replacement,
            sizeof(replacement),
            2u,
            1u);
        renderSet.executeCommand(updateEncoder);
        renderSet.update();

        const auto component = renderSet.getBufferComponentByName("Vertex");
        ASSERT_NE(component, nullptr);
        const uint32_t *updatedPayload = readFakeBufferElements<uint32_t>(
            component->getDataBuffer(),
            component->getComponentIndexByEntity(entity),
            initialPayload.size());
        EXPECT_EQ(updatedPayload[0], 10u);
        EXPECT_EQ(updatedPayload[1], 20u);
        EXPECT_EQ(updatedPayload[2], replacement);
        EXPECT_EQ(updatedPayload[3], 40u);

        auto invalidEncoder = renderSet.createEncoder();
        ASSERT_NE(invalidEncoder, nullptr);
        EXPECT_THROW(
            invalidEncoder->setBufferComponentData(
                entity,
                renderSet.createRenderComponentHandle(0),
                &replacement,
                sizeof(replacement),
                4u,
                1u),
            std::out_of_range);
        EXPECT_THROW(
            invalidEncoder->setBufferComponentData(
                entity,
                renderSet.createRenderComponentHandle(0),
                nullptr,
                sizeof(replacement),
                0u,
                1u),
            std::invalid_argument);

        renderSet.destroy();
    }

    TEST(RenderSetCommandEncoderTests, StreamsLargeBufferPayloadsThroughGPUVectorBackedStorage)
    {
        static constexpr uint32_t kEntityCount = 65536u;
        static constexpr uint32_t kPayloadElementCount = 64u;
        static constexpr uint32_t kInstanceCount = 4u;
        static constexpr uint32_t kExpectedComponentStride = GVM::Core::RenderComponentVertexBufferSizeAlign;

        FakeDevice device;
        GVM::Core::RenderSet renderSet;
        renderSet.create(&device, makeSingleVertexRenderSetCreateInfo());
        auto vertexComponent = renderSet.getBufferComponentByName("Vertex");
        ASSERT_NE(vertexComponent, nullptr);

        const uint64_t initialEntityInfoGeneration = GVM::Core::RenderSetTestPeer::getRenderEntityInfoBufferGeneration(renderSet);
        const uint64_t initialCMDParamsGeneration = GVM::Core::RenderSetTestPeer::getRenderEntityCMDParamsBufferGeneration(renderSet);
        const uint64_t initialDataGeneration = GVM::Core::BufferComponentTestPeer::getDataBufferGeneration(*vertexComponent);
        const uint64_t initialComponentIndexGeneration = GVM::Core::BufferComponentTestPeer::getComponentIndexBufferGeneration(*vertexComponent);

        auto command = renderSet.createEncoder();
        ASSERT_NE(command, nullptr);
        std::array<uint32_t, kPayloadElementCount> payload = {};
        for (uint32_t entityIndex = 0; entityIndex < kEntityCount; ++entityIndex)
        {
            for (uint32_t elementIndex = 0; elementIndex < kPayloadElementCount; ++elementIndex)
            {
                payload[elementIndex] = makeStreamingPayloadValue(entityIndex, elementIndex);
            }

            GVM::Core::RenderSetAllocInfo allocInfo = {};
            allocInfo.verticesCount = kPayloadElementCount;
            allocInfo.instanceCount = kInstanceCount;
            allocInfo.bufferInfos.push_back({
                .bufferComponentHandle = renderSet.createRenderComponentHandle(0),
                .bufferName = "",
                .value = payload.data(),
                .dataStorageSize = sizeof(payload),
                .instanceCount = 1,
            });
            const auto entity = command->allocEntity(allocInfo);
            ASSERT_EQ(entity, entityIndex);
        }

        renderSet.executeCommand(command);
        renderSet.update();

        EXPECT_GT(GVM::Core::RenderSetTestPeer::getRenderEntityInfoBufferGeneration(renderSet), initialEntityInfoGeneration);
        EXPECT_GT(GVM::Core::RenderSetTestPeer::getRenderEntityCMDParamsBufferGeneration(renderSet), initialCMDParamsGeneration);
        EXPECT_GT(GVM::Core::BufferComponentTestPeer::getDataBufferGeneration(*vertexComponent), initialDataGeneration);
        EXPECT_GT(GVM::Core::BufferComponentTestPeer::getComponentIndexBufferGeneration(*vertexComponent), initialComponentIndexGeneration);

        EXPECT_GE(device.mStats.copyBufferToBufferMultipleRegionCalls, 4u);
        EXPECT_GT(device.mStats.copiedBufferRegionCount, kEntityCount);

        const auto *componentIndices = readFakeBufferElements<GVM::Core::RenderComponentIndex>(vertexComponent->getComponentIndexBuffer(), 0u, kEntityCount);
        const uint64_t requiredResourceElements = uint64_t(kEntityCount - 1u) * kExpectedComponentStride + kPayloadElementCount;
        const auto *streamedPayload = readFakeBufferElements<uint32_t>(vertexComponent->getDataBuffer(), 0u, requiredResourceElements);
        for (uint32_t entityIndex = 0; entityIndex < kEntityCount; ++entityIndex)
        {
            const uint32_t expectedComponentIndex = entityIndex * kExpectedComponentStride;
            if (componentIndices[entityIndex] != expectedComponentIndex)
            {
                ADD_FAILURE() << "Unexpected component index for entity " << entityIndex
                              << ": expected " << expectedComponentIndex
                              << ", got " << componentIndices[entityIndex];
                break;
            }
            for (uint32_t elementIndex = 0; elementIndex < kPayloadElementCount; ++elementIndex)
            {
                const uint32_t expectedValue = makeStreamingPayloadValue(entityIndex, elementIndex);
                const uint32_t actualValue = streamedPayload[expectedComponentIndex + elementIndex];
                if (actualValue != expectedValue)
                {
                    ADD_FAILURE() << "Unexpected streamed payload for entity " << entityIndex
                                  << ", element " << elementIndex
                                  << ": expected " << expectedValue
                                  << ", got " << actualValue;
                    entityIndex = kEntityCount;
                    break;
                }
            }
        }

        const auto *entityInfos = readFakeBufferElements<GVM::Core::RenderEntityInfo>(renderSet.getRenderEntityInfoBuffer(), 0u, kEntityCount);
        for (uint32_t entityIndex = 0; entityIndex < kEntityCount; ++entityIndex)
        {
            const auto &entityInfo = entityInfos[entityIndex];
            if (entityInfo.vertexOffset != entityIndex * kExpectedComponentStride ||
                entityInfo.vertexCount != kPayloadElementCount ||
                entityInfo.indexCount != 0u ||
                entityInfo.instanceCount != kInstanceCount ||
                entityInfo.cmdParamsOffset != entityIndex * kInstanceCount ||
                entityInfo.globalInstanceBase != entityInfo.cmdParamsOffset ||
                entityInfo.entityVersion != 1u)
            {
                ADD_FAILURE() << "Unexpected RenderEntityInfo for entity " << entityIndex;
                break;
            }
        }

        const uint64_t requiredCMDParamCount = uint64_t(kEntityCount) * kInstanceCount;
        const auto *cmdParams = readFakeBufferElements<GVM::Core::RenderEntityCMDParam>(renderSet.getRenderEntityCMDParamsBuffer(), 0u, requiredCMDParamCount);
        for (uint32_t entityIndex = 0; entityIndex < kEntityCount; ++entityIndex)
        {
            for (uint32_t instanceIndex = 0; instanceIndex < kInstanceCount; ++instanceIndex)
            {
                const auto &cmdParam = cmdParams[uint64_t(entityIndex) * kInstanceCount + instanceIndex];
                if (cmdParam.entity != entityIndex || cmdParam.instanceIndex != instanceIndex)
                {
                    ADD_FAILURE() << "Unexpected RenderEntityCMDParam for entity " << entityIndex
                                  << ", instance " << instanceIndex;
                    entityIndex = kEntityCount;
                    break;
                }
            }
        }

        const auto *accessBounds = readFakeBufferElements<GVM::Core::RenderSetAccessBoundData>(
            GVM::Core::RenderSetTestPeer::getAccessBoundDataBuffer(renderSet),
            0u,
            2u);
        EXPECT_GE(accessBounds[0].componentIndexCount, kEntityCount);
        EXPECT_GE(accessBounds[0].resourceCount, requiredCMDParamCount);
        EXPECT_GE(accessBounds[1].componentIndexCount, kEntityCount);
        EXPECT_GE(accessBounds[1].resourceCount, requiredResourceElements);

        renderSet.destroy();
    }

    TEST(RenderSetCommandEncoderTests, PendingEntityRemovalsBecomeEffectiveDuringProcessInternal)
    {
        FakeDevice device;
        GVM::Core::RenderSet renderSet;
        renderSet.create(&device, makeSingleVertexRenderSetCreateInfo());

        GVM::Core::RenderSetAllocInfo allocInfo = {};
        allocInfo.verticesCount = 3;
        allocInfo.instanceCount = 1;
        allocInfo.bufferInfos.push_back({
            .bufferComponentHandle = renderSet.createRenderComponentHandle(0),
            .bufferName = "TransientVertexData",
            .value = nullptr,
            .dataStorageSize = sizeof(uint32_t) * 3,
            .instanceCount = 1,
        });

        auto encoder = eastl::static_pointer_cast<GVM::Core::RenderSetCommandEncoderImpl>(renderSet.createEncoder());
        ASSERT_NE(encoder, nullptr);

        const auto entity = encoder->allocEntity(allocInfo);
        auto vertexComponent = renderSet.getBufferComponentByName("Vertex");
        ASSERT_NE(vertexComponent, nullptr);
        ASSERT_TRUE(renderSet.check(entity));
        ASSERT_TRUE(vertexComponent->check("TransientVertexData"));

        encoder->removeEntity(entity);
        EXPECT_EQ(GVM::Core::RenderSetCommandEncoderTestPeer::getPendingRemoveCount(*encoder), 1u);

        encoder->processInUserThread();
        EXPECT_TRUE(renderSet.check(entity));

        encoder->processInternal();

        EXPECT_FALSE(renderSet.check(entity));
        EXPECT_FALSE(vertexComponent->check(entity));
        EXPECT_FALSE(vertexComponent->check("TransientVertexData"));

        encoder->destroy();
        renderSet.destroy();
    }

    TEST(TextureComponentTests, RejectsMoreThanEightTextureSlotsPerEntity)
    {
        FakeDevice device;
        GVM::Core::TextureComponent component;
        component.create(&device, {
                                      .textureComponentName = "TooManyTextures",
                                      .dataElementIncreamentCount = 4,
                                      .maxTextureCount = 32,
                                  });

        GVM::Core::RenderSetTextureComponentAllocInfo allocInfo = {};
        allocInfo.textureComponentHandle = 1;
        for (uint32_t index = 0; index < GVM::Core::RenderTextureMaxTextureCountPerEntity + 1u; ++index)
        {
            allocInfo.textures.push_back(makeTextureInfo(("Tex" + eastl::to_string(index)).c_str()));
        }

        EXPECT_THROW(GVM::Core::TextureComponentTestPeer::alloc(component, 0, allocInfo), std::invalid_argument);

        component.destroy();
    }

    TEST(TextureComponentTests, NamedTexturesReuseDescriptorAndReleaseAfterLastEntityRemove)
    {
        FakeDevice device;
        GVM::Core::TextureComponent component;
        component.create(&device, {
                                      .textureComponentName = "SharedTextureComponent",
                                      .dataElementIncreamentCount = 4,
                                      .maxTextureCount = 4,
                                  });

        GVM::Core::RenderSetTextureComponentAllocInfo allocInfo = {};
        allocInfo.textureComponentHandle = 1;
        allocInfo.textures.push_back(makeTextureInfo("SharedTexture"));

        GVM::Core::TextureComponentTestPeer::alloc(component, 0, allocInfo);
        GVM::Core::TextureComponentTestPeer::alloc(component, 1, allocInfo);

        GVM::Core::StagingLinearAllocator allocator;
        const auto firstAllocInfo = GVM::Core::TextureComponentTestPeer::getAllocInfo(component, 0, allocator);
        ASSERT_EQ(firstAllocInfo.size, sizeof(GVM::Core::RenderComponentIndex) * GVM::Core::RenderTextureMaxTextureCountPerEntity);
        auto firstIndices = reinterpret_cast<const GVM::Core::RenderComponentIndex *>(allocator.data() + firstAllocInfo.CPUDataSrcOffset);
        EXPECT_EQ(firstIndices[0], 0u);
        const GVM::Core::RenderComponentIndex firstDescriptorIndex = firstIndices[0];

        allocator.reset();
        const auto secondAllocInfo = GVM::Core::TextureComponentTestPeer::getAllocInfo(component, 1, allocator);
        ASSERT_EQ(secondAllocInfo.size, sizeof(GVM::Core::RenderComponentIndex) * GVM::Core::RenderTextureMaxTextureCountPerEntity);
        auto secondIndices = reinterpret_cast<const GVM::Core::RenderComponentIndex *>(allocator.data() + secondAllocInfo.CPUDataSrcOffset);
        EXPECT_EQ(secondIndices[0], firstDescriptorIndex);

        component.remove(0);
        EXPECT_TRUE(component.check("SharedTexture"));
        EXPECT_EQ(device.mStats.destroyedTextures, 0u);

        component.remove(1);
        EXPECT_FALSE(component.check("SharedTexture"));
        EXPECT_EQ(device.mStats.destroyedTextures, 1u);

        GVM::Core::RenderSetTextureComponentAllocInfo recycledAllocInfo = {};
        recycledAllocInfo.textureComponentHandle = 1;
        recycledAllocInfo.textures.push_back(makeTextureInfo("FreshTexture"));
        GVM::Core::TextureComponentTestPeer::alloc(component, 2, recycledAllocInfo);
        allocator.reset();
        const auto recycledCopyInfo = GVM::Core::TextureComponentTestPeer::getAllocInfo(component, 2, allocator);
        ASSERT_EQ(recycledCopyInfo.size, sizeof(GVM::Core::RenderComponentIndex) * GVM::Core::RenderTextureMaxTextureCountPerEntity);
        auto recycledIndices = reinterpret_cast<const GVM::Core::RenderComponentIndex *>(allocator.data() + recycledCopyInfo.CPUDataSrcOffset);
        EXPECT_EQ(recycledIndices[0], 0u);

        component.destroy();
    }

    TEST(TextureComponentTests, PublishesPerEntityTextureIndexPages)
    {
        FakeDevice device;
        GVM::Core::TextureComponent component;
        component.create(&device, {
                                      .textureComponentName = "RemoveInfoTextureComponent",
                                      .dataElementIncreamentCount = 4,
                                      .maxTextureCount = 8,
                                  });

        GVM::Core::RenderSetTextureComponentAllocInfo allocInfo = {};
        allocInfo.textureComponentHandle = 1;
        allocInfo.textures.push_back(makeTextureInfo("TexA"));
        allocInfo.textures.push_back(makeTextureInfo("TexB"));
        GVM::Core::TextureComponentTestPeer::alloc(component, 0, allocInfo);

        EXPECT_EQ(GVM::Core::TextureComponentTestPeer::getPerEntityComponentIndexStorageBytes(component),
                  sizeof(GVM::Core::RenderComponentIndex) * GVM::Core::RenderTextureMaxTextureCountPerEntity);
        EXPECT_GE(GVM::Core::TextureComponentTestPeer::getComponentIndexElementCountForRenderSetAccessBounds(component),
                  4u * GVM::Core::RenderTextureMaxTextureCountPerEntity);

        GVM::Core::StagingLinearAllocator allocator;
        const auto allocCopy = GVM::Core::TextureComponentTestPeer::getAllocInfo(component, 0, allocator);
        ASSERT_EQ(allocCopy.size, sizeof(GVM::Core::RenderComponentIndex) * GVM::Core::RenderTextureMaxTextureCountPerEntity);
        const auto *indices = reinterpret_cast<const GVM::Core::RenderComponentIndex *>(allocator.data() + allocCopy.CPUDataSrcOffset);
        EXPECT_EQ(indices[0], 0u);
        EXPECT_EQ(indices[1], 1u);
        for (uint32_t slot = 2u; slot < GVM::Core::RenderTextureMaxTextureCountPerEntity; ++slot)
        {
            EXPECT_EQ(indices[slot], GVM::Core::RenderComponentNullIndex);
        }

        allocator.reset();
        const auto removeCopy = GVM::Core::TextureComponentTestPeer::getRemoveInfo(component, 0, allocator);
        ASSERT_EQ(removeCopy.size, sizeof(GVM::Core::RenderComponentIndex) * GVM::Core::RenderTextureMaxTextureCountPerEntity);
        const auto *removedIndices = reinterpret_cast<const GVM::Core::RenderComponentIndex *>(allocator.data() + removeCopy.CPUDataSrcOffset);
        for (uint32_t slot = 0u; slot < GVM::Core::RenderTextureMaxTextureCountPerEntity; ++slot)
        {
            EXPECT_EQ(removedIndices[slot], GVM::Core::RenderComponentNullIndex);
        }

        component.destroy();
    }

    TEST(TextureComponentTests, PerformTextureCopyAllowsReuploadForExistingTextureName)
    {
        FakeDevice device;
        GVM::Core::TextureComponent component;
        component.create(&device, {
                                      .textureComponentName = "DynamicTextureComponent",
                                      .dataElementIncreamentCount = 4,
                                      .maxTextureCount = 4,
                                  });

        GVM::Core::RenderSetTextureComponentAllocInfo allocInfo = {};
        allocInfo.textureComponentHandle = 1;
        allocInfo.textures.push_back(makeTextureInfo("DynamicTexture"));
        GVM::Core::TextureComponentTestPeer::alloc(component, 0, allocInfo);

        auto stagingBuffer = device.createBuffer({
            .label = "TextureUploadStaging",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapWrite,
            .size = 64,
        });
        ASSERT_FALSE(stagingBuffer.isNull());

        GVM::Core::RenderSetTextureComponentTextureCopyCommand command = {};
        command.textureName = "DynamicTexture";
        command.format = GVM::RHI::TextureFormat::RGBA8Unorm;
        command.width = 2;
        command.height = 2;
        command.stagingBuffer = stagingBuffer;
        command.stagingBufferOffset = 0;
        command.copyByteSize = 16;
        command.mipmapOffsetBytes = {0};

        GVM::Core::TextureComponentTestPeer::performTextureCopy(component, command);
        GVM::Core::TextureComponentTestPeer::performTextureCopy(component, command);

        EXPECT_EQ(device.mStats.copyBufferToTextureCalls, 0u);
        EXPECT_EQ(device.mStats.uploadTextureCalls, 2u);
        ASSERT_EQ(device.mStats.uploadedTextureLabels.size(), 2u);
        EXPECT_EQ(device.mStats.uploadedTextureLabels[0], "DynamicTexture");
        EXPECT_EQ(device.mStats.uploadedTextureLabels[1], "DynamicTexture");
        ASSERT_EQ(device.mStats.uploadedTextureBufferOffsets.size(), 2u);
        EXPECT_EQ(device.mStats.uploadedTextureBufferOffsets[0], 0u);
        EXPECT_EQ(device.mStats.uploadedTextureBufferOffsets[1], 0u);
        ASSERT_EQ(device.mStats.uploadedTextureMipOffsets.size(), 2u);
        EXPECT_EQ(device.mStats.uploadedTextureMipOffsets[0], (eastl::vector<uint64_t>{0}));
        EXPECT_EQ(device.mStats.uploadedTextureMipOffsets[1], (eastl::vector<uint64_t>{0}));

        device.freeBuffer(stagingBuffer);
        component.destroy();
    }

    TEST(TextureComponentTests, PerformTextureCopyRejectsMipOffsetsThatEscapeTheRecordedPayload)
    {
        FakeDevice device;
        GVM::Core::TextureComponent component;
        component.create(&device, {
                                      .textureComponentName = "ValidatedTextureComponent",
                                      .dataElementIncreamentCount = 4,
                                      .maxTextureCount = 4,
                                  });

        GVM::Core::RenderSetTextureComponentAllocInfo allocInfo = {};
        allocInfo.textureComponentHandle = 1;
        allocInfo.textures.push_back(makeTextureInfo("ValidatedTexture"));
        GVM::Core::TextureComponentTestPeer::alloc(component, 0, allocInfo);

        auto stagingBuffer = device.createBuffer({
            .label = "TextureUploadStaging",
            .usage = GVM::RHI::BufferUsage::CopySrc | GVM::RHI::BufferUsage::MapWrite,
            .size = 64,
        });
        ASSERT_FALSE(stagingBuffer.isNull());

        GVM::Core::RenderSetTextureComponentTextureCopyCommand command = {};
        command.textureName = "ValidatedTexture";
        command.format = GVM::RHI::TextureFormat::RGBA8Unorm;
        command.width = 2;
        command.height = 2;
        command.stagingBuffer = stagingBuffer;
        command.stagingBufferOffset = 0;
        command.copyByteSize = 15;
        command.mipmapOffsetBytes = {0};

        EXPECT_THROW(GVM::Core::TextureComponentTestPeer::performTextureCopy(component, command), std::invalid_argument);
        EXPECT_EQ(device.mStats.copyBufferToTextureCalls, 0u);
        EXPECT_EQ(device.mStats.uploadTextureCalls, 0u);

        device.freeBuffer(stagingBuffer);
        component.destroy();
    }

    TEST(TextureComponentTests, DestroyReleasesLiveTexturesEvenWithoutExplicitRemove)
    {
        FakeDevice device;
        GVM::Core::TextureComponent component;
        component.create(&device, {
                                      .textureComponentName = "DestroyTextureComponent",
                                      .dataElementIncreamentCount = 4,
                                      .maxTextureCount = 4,
                                  });

        GVM::Core::RenderSetTextureComponentAllocInfo allocInfo = {};
        allocInfo.textureComponentHandle = 1;
        allocInfo.textures.push_back(makeTextureInfo("LiveTexture"));
        GVM::Core::TextureComponentTestPeer::alloc(component, 0, allocInfo);

        component.destroy();

        EXPECT_EQ(device.mStats.destroyedTextures, 2u);
        EXPECT_EQ(device.mStats.destroyedTextureViews, 2u);
        EXPECT_EQ(device.mStats.destroyedTextureLabels[0], "LiveTexture");
        EXPECT_EQ(device.mStats.destroyedTextureLabels[1], "DestroyTextureComponent_EmptyTexture");
    }

    TEST(TextureComponentTests, RejectsAllocationsThatExceedConfiguredTextureLibraryCapacity)
    {
        FakeDevice device;
        GVM::Core::TextureComponent component;
        component.create(&device, {
                                      .textureComponentName = "CapacityTextureComponent",
                                      .dataElementIncreamentCount = 4,
                                      .maxTextureCount = 1,
                                  });

        GVM::Core::RenderSetTextureComponentAllocInfo firstAllocInfo = {};
        firstAllocInfo.textureComponentHandle = 1;
        firstAllocInfo.textures.push_back(makeTextureInfo("OnlySlot"));
        GVM::Core::TextureComponentTestPeer::alloc(component, 0, firstAllocInfo);

        GVM::Core::RenderSetTextureComponentAllocInfo secondAllocInfo = {};
        secondAllocInfo.textureComponentHandle = 1;
        secondAllocInfo.textures.push_back(makeTextureInfo("OverflowSlot"));

        EXPECT_THROW(GVM::Core::TextureComponentTestPeer::alloc(component, 1, secondAllocInfo), std::out_of_range);

        component.destroy();
    }

    TEST(GPUVectorTests, StagingCopyUploadsTypedDataThroughExplicitStagingBuffer)
    {
        FakeDevice device;
        auto values = GVM::Core::createGPUVectorStorage<uint32_t>(
            &device,
            "UnitGPUVector",
            GVM::RHI::BufferUsage::Storage,
            2u * sizeof(uint32_t),
            2u * sizeof(uint32_t));

        ASSERT_NE(dynamic_cast<GVM::Core::StagingCopyGPUVector<uint32_t> *>(values.get()), nullptr);

        const uint32_t initialValues[] = {10u, 20u};
        values->commitData(initialValues, 2u, true);
        ASSERT_FALSE(values->gpuBuffer().isNull());
        EXPECT_EQ(values->generation(), 1u);
        EXPECT_EQ(device.mStats.writeBufferCalls, 0u);
        EXPECT_EQ(device.mStats.copyBufferToBufferCalls, 1u);
        ASSERT_GE(device.mStats.createdBufferUsages.size(), 2u);
        EXPECT_EQ(device.mStats.createdBufferUsages[0] & GVM::RHI::BufferUsage::MapWrite, 0u);
        EXPECT_NE(device.mStats.createdBufferUsages[1] & GVM::RHI::BufferUsage::MapWrite, 0u);

        const uint32_t grownValues[] = {10u, 20u, 30u};
        values->commitData(grownValues, 3u, true);
        EXPECT_EQ(values->generation(), 2u);
        EXPECT_EQ(device.mStats.writeBufferCalls, 0u);
        EXPECT_EQ(device.mStats.copyBufferToBufferCalls, 3u);

        const uint32_t updatedValues[] = {99u, 20u, 30u};
        values->commitData(updatedValues, 3u, true);
        EXPECT_EQ(values->generation(), 2u);
        EXPECT_EQ(device.mStats.writeBufferCalls, 0u);
        EXPECT_EQ(device.mStats.copyBufferToBufferCalls, 4u);

        const auto *gpuValues = readFakeBufferElements<uint32_t>(values->gpuBuffer(), 0u, 3u);
        ASSERT_NE(gpuValues, nullptr);
        EXPECT_EQ(gpuValues[0], 99u);
        EXPECT_EQ(gpuValues[1], 20u);
        EXPECT_EQ(gpuValues[2], 30u);

        values->destroy();
        values.reset();
        EXPECT_EQ(device.mStats.destroyedBuffers, 4u);
    }

    TEST(GPUVectorTests, UnifiedMemoryCommitWritesMappedBackingBufferWithoutQueueUpload)
    {
        FakeDevice device({.unifiedMemory = GVM::RHI::True});
        auto values = GVM::Core::createGPUVectorStorage<uint32_t>(
            &device,
            "UnifiedGPUVector",
            GVM::RHI::BufferUsage::Storage,
            4u * sizeof(uint32_t),
            0u);

        ASSERT_NE(dynamic_cast<GVM::Core::UnifiedMemoryGPUVector<uint32_t> *>(values.get()), nullptr);

        const uint32_t initialValues[] = {7u, 11u};
        values->commitData(initialValues, 2u, true);

        ASSERT_FALSE(values->gpuBuffer().isNull());
        EXPECT_EQ(device.mStats.writeBufferCalls, 0u);
        EXPECT_EQ(device.mStats.copyBufferToBufferCalls, 0u);
        EXPECT_EQ(device.mStats.mapBufferCalls, 1u);
        ASSERT_FALSE(device.mStats.createdBufferUsages.empty());
        EXPECT_NE(device.mStats.createdBufferUsages[0] & GVM::RHI::BufferUsage::MapWrite, 0u);

        const auto *mapped = static_cast<const uint32_t *>(values->gpuBuffer()->getConstMappedRange(0u, 2u * sizeof(uint32_t)));
        ASSERT_NE(mapped, nullptr);
        EXPECT_EQ(mapped[0], 7u);
        EXPECT_EQ(mapped[1], 11u);

        values->destroy();
        values.reset();
        EXPECT_EQ(device.mStats.unmapBufferCalls, 1u);
        EXPECT_EQ(device.mStats.destroyedBuffers, 1u);
    }

    TEST(GPUVectorTests, CommitStorageGrowsPrivateGpuStorageWithoutCpuUpload)
    {
        FakeDevice device;
        auto storage = GVM::Core::createGPUVectorStorage<uint8_t>(
            &device,
            "PrivateRenderSetStorage",
            GVM::RHI::BufferUsage::Storage,
            0u,
            16u);

        storage->reserveGPUBytes(32u);
        storage->commitStorage();
        ASSERT_FALSE(storage->gpuBuffer().isNull());
        EXPECT_EQ(storage->gpuCapacityBytes(), 32u);
        EXPECT_EQ(storage->generation(), 1u);
        EXPECT_EQ(device.mStats.writeBufferCalls, 0u);

        storage->reserveGPUBytes(40u);
        storage->commitStorage();
        EXPECT_EQ(storage->gpuCapacityBytes(), 48u);
        EXPECT_EQ(storage->generation(), 2u);
        EXPECT_EQ(device.mStats.copyBufferToBufferCalls, 1u);
        EXPECT_EQ(device.mStats.writeBufferCalls, 0u);
        EXPECT_EQ(device.mStats.createdBufferUsages.size(), 2u);

        storage->destroy();
        storage.reset();
        EXPECT_EQ(device.mStats.destroyedBuffers, 2u);
    }

    TEST(GPUVectorTests, GrowPreservesTypedDataForUnifiedAndStagingStorage)
    {
        {
            FakeDevice device;
            auto storage = GVM::Core::createGPUVectorStorage<uint32_t>(
                &device,
                "StagingPreserveGPUVector",
                GVM::RHI::BufferUsage::Storage,
                2u * sizeof(uint32_t),
                2u * sizeof(uint32_t));

            const uint32_t values[] = {31u, 37u};
            storage->commitData(values, 2u, true);
            storage->reserveElements(4u);
            storage->commitStorage();

            const auto *gpuValues = readFakeBufferElements<uint32_t>(storage->gpuBuffer(), 0u, 2u);
            ASSERT_NE(gpuValues, nullptr);
            EXPECT_EQ(gpuValues[0], 31u);
            EXPECT_EQ(gpuValues[1], 37u);
            EXPECT_EQ(storage->generation(), 2u);

            storage->destroy();
        }

        {
            FakeDevice device({.unifiedMemory = GVM::RHI::True});
            auto storage = GVM::Core::createGPUVectorStorage<uint32_t>(
                &device,
                "UnifiedPreserveGPUVector",
                GVM::RHI::BufferUsage::Storage,
                2u * sizeof(uint32_t),
                2u * sizeof(uint32_t));

            const uint32_t values[] = {41u, 43u};
            storage->commitData(values, 2u, true);
            storage->reserveElements(4u);
            storage->commitStorage();

            const auto *gpuValues = readFakeBufferElements<uint32_t>(storage->gpuBuffer(), 0u, 2u);
            ASSERT_NE(gpuValues, nullptr);
            EXPECT_EQ(gpuValues[0], 41u);
            EXPECT_EQ(gpuValues[1], 43u);
            EXPECT_EQ(storage->generation(), 2u);

            storage->destroy();
        }
    }
} // namespace
