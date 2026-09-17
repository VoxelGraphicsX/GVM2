#pragma once

#include "VKBindingUtils.hpp"
#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <EASTL/variant.h>

namespace GVM::RHI::Vulkan
{
    class VKRenderPassEncoder final : public RenderPassEncoderImpl
    {
    public:
        VKRenderPassEncoder() = default;

        void init(VKDevice *device, CommandEncoder commandEncoder, const RenderPassDescriptor &descriptor);

        void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) override;
        void drawFullscreenTexture(Texture source, const RenderToSwapchainDescriptor &descriptor) override;
        void setPipeline(RenderPipeline pipeline) override;
        void setBindGroup(BindGroup group, uint32_t groupIndex) override;
        void setVertexBuffer(BufferRange buffer, uint32_t slot) override;
        void setIndexBuffer(BufferRange buffer, IndexFormat format) override;
        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) override;
        void drawIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride) override;
        void drawIndexedIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride) override;
        void drawPixels() override;
        void nextPixelLocalPass() override;
        void end() override;

        void setTransientDescriptorSet(BindGroupLayout layout, vk::DescriptorSet descriptorSet, uint32_t groupIndex);

        [[nodiscard]]
        VKCommandEncoder *getCommandEncoder() const;

    private:
        struct PendingBufferPreparation
        {
            Buffer buffer = {};
            uint64_t offset = 0u;
            uint64_t size = 0u;
            vk::PipelineStageFlags stageMask = {};
            vk::AccessFlags accessMask = {};
        };

        struct CmdSetScissorRect
        {
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        struct CmdSetViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float minDepth = 0.0f;
            float maxDepth = 1.0f;
        };

        struct CmdSetPipeline
        {
            RenderPipeline pipeline = nullptr;
        };

        struct CmdSetBindGroup
        {
            BindGroup group = nullptr;
            uint32_t groupIndex = 0;
        };

        struct CmdSetTransientDescriptorSet
        {
            BindGroupLayout layout = nullptr;
            vk::DescriptorSet descriptorSet = nullptr;
            uint32_t groupIndex = 0;
        };

        struct CmdSetVertexBuffer
        {
            BufferRange buffer;
            uint32_t slot = 0;
        };

        struct CmdSetIndexBuffer
        {
            BufferRange buffer;
            IndexFormat format = IndexFormat::Undefined;
        };

        struct CmdDraw
        {
            uint32_t vertexCount = 0;
            uint32_t instanceCount = 0;
            uint32_t firstVertex = 0;
            uint32_t firstInstance = 0;
        };

        struct CmdDrawIndexed
        {
            uint32_t indexCount = 0;
            uint32_t instanceCount = 0;
            uint32_t firstIndex = 0;
            int32_t baseVertex = 0;
            uint32_t firstInstance = 0;
        };

        struct CmdDrawIndirect
        {
            BufferRange indirectBuffer;
            uint32_t indirectCommandCount = 0;
            uint32_t stride = 0;
        };

        struct CmdDrawIndexedIndirect
        {
            BufferRange indirectBuffer;
            uint32_t indirectCommandCount = 0;
            uint32_t stride = 0;
        };

        struct CmdDrawPixels
        {
        };

        struct CmdNextPixelLocalPass
        {
        };

        using RenderCommand = eastl::variant<
            CmdSetScissorRect,
            CmdSetViewport,
            CmdSetPipeline,
            CmdSetBindGroup,
            CmdSetTransientDescriptorSet,
            CmdSetVertexBuffer,
            CmdSetIndexBuffer,
            CmdDraw,
            CmdDrawIndexed,
            CmdDrawIndirect,
            CmdDrawIndexedIndirect,
            CmdDrawPixels,
            CmdNextPixelLocalPass>;

        /// Tracks native Vulkan state while replaying queued render pass commands.
        struct ReplayState
        {
            RenderPipeline pipeline = nullptr;
            PipelineLayout pipelineLayout = nullptr;
            vk::PipelineLayout nativePipelineLayout = nullptr;
            bool bindGroupStateDirty = true;
            bool bindGroupStateValid = false;
            eastl::unordered_map<uint32_t, DescriptorBindingState> descriptorBindings;
            eastl::unordered_map<uint32_t, DescriptorBindingState> nativeBoundDescriptorBindings;
            eastl::unordered_map<uint32_t, BufferRange> nativeBoundVertexBuffers;
            bool hasNativeIndexBuffer = false;
            BufferRange nativeIndexBuffer = {};
            IndexFormat nativeIndexFormat = IndexFormat::Undefined;
            bool hasViewport = false;
            CmdSetViewport viewport = {};
            bool hasScissor = false;
            CmdSetScissorRect scissor = {};
            uint32_t currentPixelLocalPassIndex = 0u;
        };

        /// Collects render pass replay counters and labels for trace diagnostics.
        struct ReplayDiagnostics
        {
            uint32_t pipelineSwitchCount = 0u;
            uint32_t bindGroupSetCount = 0u;
            uint32_t drawCount = 0u;
            uint32_t drawIndexedCount = 0u;
            uint32_t drawIndirectCount = 0u;
            uint32_t drawIndexedIndirectCount = 0u;
            eastl::vector<eastl::string> pipelineLabels;
            eastl::vector<eastl::string> bindGroupLabels;
        };

        void enqueueBindGroupPreparation(BindGroup group);
        void enqueueBufferPreparation(BufferRange buffer, vk::PipelineStageFlags stageMask, vk::AccessFlags accessMask);
        void enqueueExplicitSampledTexture(Texture texture);

        /// Resolves per-pass pixel-local attachment access from queued render commands before render pass creation.
        eastl::vector<PixelLocalPassAttachmentAccess> resolvePixelLocalAttachmentAccessesFromCommands() const;

        void ensureOpen(const char *apiName) const;

        VKDevice *mDevice = nullptr;
        CommandEncoder mCommandEncoder = nullptr;
        RenderPassDescriptor mDescriptor = {};
        eastl::vector<RenderCommand> mCommands;
        eastl::unordered_map<const VKBindGroup *, BindGroup> mPendingBindGroupPreparations;
        eastl::unordered_map<const VKBuffer *, eastl::vector<PendingBufferPreparation>> mPendingBufferPreparations;
        eastl::unordered_map<const VKTexture *, Texture> mExplicitSampledTextures;
        uint32_t mQueuedPixelLocalPassIndex = 0u;
        bool mEnded = false;
    };
} // namespace GVM::RHI::Vulkan
