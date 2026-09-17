#pragma once
#include "MDefines.hpp"
#include <EASTL/unordered_map.h>
#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
namespace GVM::RHI::Metal
{
    struct VertexBufferInfo
    {
        BufferRange buffer;
        uint32_t slot;
    };
    struct BindGroupInfo
    {
        BindGroup bindGroup = nullptr;
        uint32_t groupIndex;
    };
    struct ViewportInfo
    {
        float x;
        float y;
        float width;
        float height;
        float minDepth;
        float maxDepth;
    };
    struct ScissorRectInfo
    {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
    };
    struct CmdSetViewport
    {
        ViewportInfo viewport;
    };

    struct CmdSetScissorRect
    {
        ScissorRectInfo rect;
    };

    struct CmdSetPipeline
    {
        RenderPipeline pipeline;
    };

    struct CmdSetBindGroup
    {
        BindGroupInfo group;
    };

    struct CmdSetVertexBuffer
    {
        VertexBufferInfo vb;
    };

    struct CmdSetIndexBuffer
    {
        BufferRange indexBuffer;
        IndexFormat indexFormat;
    };

    struct CmdDraw
    {
        std::uint32_t vertexCount;
        std::uint32_t instanceCount;
        std::uint32_t firstVertex;
        std::uint32_t firstInstance;
    };

    struct CmdDrawIndexed
    {
        std::uint32_t indexCount;
        std::uint32_t instanceCount;
        std::uint32_t firstIndex;
        std::int32_t firstVertex;
        std::uint32_t firstInstance;
    };

    struct CmdDrawIndirect
    {
        BufferRange indirectBuffer;
        std::uint32_t indirectBufferStride;
        std::uint32_t commandCount;
    };

    struct CmdDrawIndexedIndirect
    {
        BufferRange indirectBuffer;
        std::uint32_t indirectBufferStride;
        std::uint32_t commandCount;
    };
    struct CmdDrawPixels
    {
    };
    struct CmdNextPixelLocalPass
    {
    };
    struct DrawCommandCacheData
    {
        std::uint32_t maxVertexBufferCountFromPipeline = 0;
        bool hasIndexBuffer = false;
        CmdSetIndexBuffer setIndexBuffer;
        RenderPipeline pipeline = nullptr;
        eastl::unordered_map<std::uint32_t, BindGroup> bindgroups;
    };

    using DrawCommand = std::variant<CmdSetViewport, CmdSetScissorRect, CmdSetPipeline, CmdSetBindGroup, CmdSetVertexBuffer, CmdSetIndexBuffer, CmdDraw, CmdDrawIndexed, CmdDrawIndirect, CmdDrawIndexedIndirect, CmdDrawPixels, CmdNextPixelLocalPass>;
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
        virtual void setVertexBuffer(BufferRange buffer, uint32_t slot) override;
        virtual void setIndexBuffer(BufferRange buffer, IndexFormat format) override;
        virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
        virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) override;
        virtual void drawIndirect(BufferRange buffer, uint32_t indirectCommandCount, uint32_t stride) override;
        virtual void drawIndexedIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride) override;
        virtual void drawPixels() override;
        virtual void nextPixelLocalPass() override;
        virtual void end() override;
        MTL::RenderCommandEncoder *getNativeRenderEncoder() const;

    private:
        MTL::RenderCommandEncoder *mNativeRenderEncoder = nullptr;
        MTL::RenderPassDescriptor *mNativeRenderEncoderDesp = nullptr;
        eastl::vector<DrawCommand> mDrawCommands;
        RenderPassDescriptor mDescriptor;
        uint32_t attachmentWidth = 0, attachmentHeight = 0;
        uint32_t mQueuedPixelLocalPassIndex = 0;
        bool mEnded = false;

        MDevice *mDevice = nullptr;
        MTL::CommandBuffer *mCommandBuffer = nullptr;
        GVM::RHI::CommandEncoderImpl *mWeakCommandEncoder = nullptr;
        void replayCommand();
        void setScissorRectInternal(const CmdSetScissorRect &descriptor);
        void setViewportInternal(const CmdSetViewport &descriptor);
        void setPipelineInternal(const CmdSetPipeline &descriptor);
        void setBindGroupInternal(const CmdSetBindGroup &descriptor, const DrawCommandCacheData &cacheData);
        void setVertexBufferInternal(const CmdSetVertexBuffer &descriptor);
        void setIndexBufferInternal(const CmdSetIndexBuffer &descriptor, const DrawCommandCacheData &cacheData);
        void drawInternal(const CmdDraw &descriptor, const DrawCommandCacheData &cacheData);
        void drawIndexedInternal(const CmdDrawIndexed &descriptor, const DrawCommandCacheData &cacheData);
        void drawIndirectInternal(const CmdDrawIndirect &descriptor, const DrawCommandCacheData &cacheData);
        void drawIndexedIndirectInternal(const CmdDrawIndexedIndirect &descriptor, const DrawCommandCacheData &cacheData);
        void drawPixelsInternal(const CmdDrawPixels &descriptor, const DrawCommandCacheData &cacheData);
        /// Resolves per-phase pixel-local attachment access from queued commands and validates phase hazards.
        eastl::vector<PixelLocalPassAttachmentAccess> resolvePixelLocalAttachmentAccessesFromCommands() const;
        void ensureOpen(const char *apiName) const;
    };

} // namespace GVM::RHI::Metal
