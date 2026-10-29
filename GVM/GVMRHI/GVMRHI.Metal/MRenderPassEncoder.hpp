#pragma once
#include "MDefines.hpp"
#include "MArguments.hpp"
#include "MBuffer.hpp"
#include <EASTL/array.h>
#include <EASTL/unordered_map.h>
#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/RenderPassRecorder.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{
    class MBindGroup;
    class MBuffer;
    class MCommandEncoder;
    class MTexture;
    struct MIndirectIndexedRenderCommandConvertOptions;

    /** Captures the render state visible to one recorded command during indirect preparation and replay. */
    struct DrawCommandCacheData
    {
        bool hasIndexBuffer = false;
        BufferRange indexBuffer;
        IndexFormat indexFormat = IndexFormat::Undefined;
        uint32_t indexBufferPayload = 0;
        RenderPipeline pipeline = nullptr;
        eastl::unordered_map<std::uint32_t, BindGroup> bindgroups;
        eastl::unordered_map<uint32_t, Private::RenderSetVertexBuffer> vertexBuffers;
    };

    /** Holds the native indirect resources one recorded indirect draw resolves and replays. */
    struct MIndirectDrawState
    {
        NS::SharedPtr<MTL::Buffer> nativeIndirectBuffer;
        NS::SharedPtr<MTL::Buffer> nativeCountBuffer;
        eastl::shared_ptr<NativeIndirectCommandBufferCache> indirectCommands;
        MTL::IndirectCommandBuffer *nativeICB = nullptr;
        MTL::Buffer *nativeExecutionRangeBuffer = nullptr;
    };

    class MRenderPassEncoder final : public RenderPassEncoderImpl
    {
    public:
        MRenderPassEncoder();
        ~MRenderPassEncoder();
        void init(MDevice *device, GVM::RHI::CommandEncoder commandEncoder, const RenderPassDescriptor &descriptor);
        virtual void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        virtual void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) override;
        virtual void drawFullscreenTexture(Texture source, const RenderToSwapchainDescriptor &descriptor) override;
        virtual void setPipeline(RenderPipeline pipeline) override;
        virtual void setBindGroup(BindGroup group, uint32_t groupIndex) override;
        /** Captures a compatible Args interval before deferred native render replay. */
        void setArguments(Arguments arguments) override;
        virtual void setVertexBuffer(BufferRange buffer, uint32_t slot) override;
        virtual void setIndexBuffer(BufferRange buffer, IndexFormat format) override;
        virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
        virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) override;
        virtual void drawIndirect(BufferRange buffer, uint32_t indirectCommandCount, uint32_t stride) override;
        virtual void drawIndexedIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride) override;
        /** Records a GPU-counted draw with explicit command and counter ranges. */
        void drawIndirectCount(BufferRange indirectBuffer, BufferRange countBuffer, uint32_t maxDrawCount, uint32_t stride) override;
        /** Records indexed GPU-counted work with explicit command and counter ranges. */
        void drawIndexedIndirectCount(BufferRange indirectBuffer, BufferRange countBuffer, uint32_t maxDrawCount, uint32_t stride) override;
        virtual void drawPixels() override;
        virtual void nextPixelLocalPass() override;
        virtual void end() override;
        MTL::RenderCommandEncoder *getNativeRenderEncoder() const;

    private:
        using IndirectConversionCounts =
            eastl::unordered_map<MTL::Buffer *, eastl::array<uint32_t, 2>>;

        /** Captures draw state and assigns distinct conversion slots before native rendering. */
        void prepareIndirectCommands();
        /** Assigns one recorded indirect draw's conversion slot and ICB argument buffer. */
        void prepareIndirectDraw(const Private::RenderDrawIndirect &draw, MIndirectDrawState &entry,
            const DrawCommandCacheData &state, IndirectConversionCounts &counts,
            eastl::vector<MIndirectIndexedRenderCommandConvertOptions> &options);
        /** Acquires one exclusive per-pass slot and binds its matching conversion argument. */
        MTL::IndirectCommandBuffer *assignIndirectCommandBuffer(
            MIndirectIndexedRenderCommandConvertOptions &options,
            IndirectConversionCounts &counts, NativeIndirectCommandBufferCache &cache);
        /** Creates the native encoder and retains its optional profiling query sets. */
        void beginNativeRenderPass(MCommandEncoder &commandEncoder);
        MTL::RenderCommandEncoder *mNativeRenderEncoder = nullptr;
        MTL::RenderPassDescriptor *mNativeRenderEncoderDesp = nullptr;
        Private::RenderPassRecorder mRecorder;
        eastl::vector<MArgumentAllocation> mArgumentAllocations;
        eastl::vector<NS::SharedPtr<MTL::Buffer>> mIndexBuffers;
        eastl::vector<MIndirectDrawState> mIndirectDraws;
        RenderPassDescriptor mDescriptor;
        uint32_t attachmentWidth = 0, attachmentHeight = 0;
        bool mEnded = false;

        MDevice *mDevice = nullptr;
        MTL::CommandBuffer *mCommandBuffer = nullptr;
        GVM::RHI::CommandEncoderImpl *mWeakCommandEncoder = nullptr;
        void replayCommand();
        void setScissorRectInternal(const Private::RenderSetScissorRect &descriptor);
        void setViewportInternal(const Private::RenderSetViewport &descriptor);
        void setPipelineInternal(const Private::RenderSetPipeline &descriptor);
        void setBindGroupInternal(const Private::RenderSetBindGroup &descriptor, const DrawCommandCacheData &cacheData);
        void setVertexBufferInternal(const Private::RenderSetVertexBuffer &descriptor);
        void drawInternal(const Private::RenderDraw &descriptor, const DrawCommandCacheData &cacheData);
        void drawIndexedInternal(const Private::RenderDrawIndexed &descriptor, const DrawCommandCacheData &cacheData);
        void drawIndirectInternal(const Private::RenderDrawIndirect &descriptor, const DrawCommandCacheData &cacheData);
        void drawPixelsInternal(const Private::RenderDrawPixels &descriptor, const DrawCommandCacheData &cacheData);
        void ensureOpen(const char *apiName) const;
    };

} // namespace GVM::RHI::Metal
