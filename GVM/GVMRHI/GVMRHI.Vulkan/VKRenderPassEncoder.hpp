#pragma once

#include "VKBindingUtils.hpp"
#include "VKArguments.hpp"
#include "VKCommon.hpp"
#include "VKRenderPassFramebufferCache.hpp"
#include "VKRenderPassReplayDiagnostics.hpp"
#include "VKDefines.hpp"

#include <GVMRHI/Private/RenderPassRecorder.hpp>

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    /** Attachment views, clear values, and render-area metadata prepared for one render-pass replay. */
    struct AttachmentContext
    {
        eastl::vector<vk::ImageView> imageViews;
        eastl::vector<vk::ClearValue> clearValues;
        eastl::vector<vk::ImageView> pixelLocalInputImageViews;
        eastl::vector<uint32_t> pixelLocalInputColorAttachmentIndices;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layers = 1;
    };

    /** Resolved render pass and framebuffer pair used to replay one recorded render pass. */
    struct RenderPassSetup
    {
        AttachmentContext attachments = {};
        vk::RenderPass renderPass = nullptr;
        VKRenderPassFramebufferCache::FramebufferHandle framebuffer = {};
    };

    /** Stores one transient pixel-local input descriptor set that is valid for the active command encoder frame. */
    struct PixelLocalInputDescriptorCacheEntry
    {
        vk::RenderPass renderPass = nullptr;
        vk::DescriptorSetLayout descriptorSetLayout = nullptr;
        uint32_t pixelLocalPassIndex = 0u;
        RenderPipeline pipeline = nullptr;
        eastl::vector<vk::ImageView> framebufferImageViews;
        vk::DescriptorSet descriptorSet = nullptr;
    };

    class VKRenderPassEncoder final : public RenderPassEncoderImpl
    {
    public:
        VKRenderPassEncoder() = default;

        void init(VKDevice *device, CommandEncoder commandEncoder, const RenderPassDescriptor &descriptor);

        void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) override;
        void drawFullscreenTexture(Texture source, const RenderToSwapchainDescriptor &descriptor) override;
        void setPipeline(RenderPipeline pipeline) override;
        /** Captures the selected pipeline's Args and records their immutable root address. */
        void setArguments(Arguments arguments) override;
        void setBindGroup(BindGroup group, uint32_t groupIndex) override;
        void setVertexBuffer(BufferRange buffer, uint32_t slot) override;
        void setIndexBuffer(BufferRange buffer, IndexFormat format) override;
        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) override;
        void drawIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride) override;
        void drawIndexedIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride) override;
        /** Records a GPU-counted draw with explicit command and counter ranges. */
        void drawIndirectCount(BufferRange indirectBuffer, BufferRange countBuffer, uint32_t maxDrawCount, uint32_t stride) override;
        /** Records indexed GPU-counted work with explicit command and counter ranges. */
        void drawIndexedIndirectCount(BufferRange indirectBuffer, BufferRange countBuffer, uint32_t maxDrawCount, uint32_t stride) override;
        void drawPixels() override;
        void nextPixelLocalPass() override;
        void end() override;
        /// Emits the closed pass after its queue submission order has been determined.
        void encode(VKCommandEncoder &commandEncoder);

    private:
        /// Tracks native Vulkan state while replaying queued render pass commands.
        struct ReplayState
        {
            RenderPipeline pipeline = nullptr;
            PipelineLayout pipelineLayout = nullptr;
            vk::PipelineLayout nativePipelineLayout = nullptr;
            DescriptorBindingTable descriptorBindings;
            DescriptorBindingTable nativeBoundDescriptorBindings;
            eastl::unordered_map<uint32_t, BufferRange> nativeBoundVertexBuffers;
            bool hasNativeIndexBuffer = false;
            BufferRange nativeIndexBuffer = {};
            IndexFormat nativeIndexFormat = IndexFormat::Undefined;
            bool hasViewport = false;
            Private::RenderSetViewport viewport = {};
            bool hasScissor = false;
            Private::RenderSetScissorRect scissor = {};
            uint32_t currentPixelLocalPassIndex = 0u;
        };

        /// Bundles the per-replay state shared by the extracted replay member functions.
        struct ReplayContext
        {
            vk::CommandBuffer commandBuffer;
            VKCommandEncoder *commandEncoder;
            const RenderPassSetup *setup;
            ReplayState *state;
            eastl::vector<PixelLocalInputDescriptorCacheEntry> *pixelLocalInputDescriptorCache;
            VKRenderPassReplayDiagnostics *diagnostics;
        };

        /** Performs the one-shot replay preamble: dependency synchronization, bind-group preparation,
         *  the begin log, vkCmdBeginRenderPass, and the initial attachment-sized viewport and scissor. */
        void beginReplay(ReplayContext &context);
        /** Ends the native render pass, emits the replay summary log, and reports the pass to the encoder. */
        void endReplay(ReplayContext &context);
        /** Replays one scissor-rect update, skipping the call when the current rect already matches. */
        void replaySetScissorRect(ReplayContext &context, const Private::RenderSetScissorRect &typedCommand);
        /** Replays one viewport update, skipping the call when the current viewport already matches. */
        void replaySetViewport(ReplayContext &context, const Private::RenderSetViewport &typedCommand);
        /** Replays one pipeline bind, including pixel-local input descriptors and layout-change rebinds. */
        void replaySetPipeline(ReplayContext &context, const Private::RenderSetPipeline &typedCommand);
        /** Replays one Args-table bind, pushing the immutable root address when present. */
        void replaySetArguments(ReplayContext &context, const Private::RenderSetArguments &typedCommand);
        /** Replays one bind-group set, skipping rebinding when the recorded state already matches. */
        void replaySetBindGroup(ReplayContext &context, const Private::RenderSetBindGroup &typedCommand);
        /** Replays one vertex-buffer bind, skipping the call when the bound range already matches. */
        void replaySetVertexBuffer(ReplayContext &context, const Private::RenderSetVertexBuffer &typedCommand);
        /** Replays one index-buffer bind, skipping the call when the bound range and format already match. */
        void replaySetIndexBuffer(ReplayContext &context, const Private::RenderSetIndexBuffer &typedCommand);
        /** Replays one indirect or count-indirect draw. */
        void replayDrawIndirect(ReplayContext &context, const Private::RenderDrawIndirect &typedCommand);
        /** Replays the fixed-function pixel-local draw with its attachment-sized viewport and scissor. */
        void replayDrawPixels(ReplayContext &context, const Private::RenderDrawPixels &typedCommand);
        /** Replays one pixel-local subpass advance and resets the per-subpass native state. */
        void replayNextPixelLocalPass(ReplayContext &context, const Private::RenderNextPixelLocalPass &typedCommand);



        void enqueueBindGroupPreparation(BindGroup group);
        /** Merges the current draw's image dependencies for deferred synchronization without re-validating them. */
        void enqueueArgumentTextures();
        void enqueueExplicitSampledTexture(Texture texture);

        void ensureOpen(const char *apiName) const;

        VKDevice *mDevice = nullptr;
        CommandEncoder mRecordingOwner = nullptr;
        VKCommandEncoder *mCommandEncoder = nullptr;
        RenderPassDescriptor mDescriptor = {};
        Private::RenderPassRecorder mRecorder;
        eastl::vector<VKArgumentAllocation> mArgumentAllocations;
        eastl::unordered_map<const VKBindGroup *, BindGroup> mPendingBindGroupPreparations;
        eastl::vector<VKTaskDependencyResolver::BufferRangeSyncRequest> mPendingBufferPreparations;
        /** The pipeline selected by the latest successful setPipeline; tracks pipeline switches for Args lifetime. */
        VKRenderPipeline *mRecordingPipeline = nullptr;
        VKArguments *mCurrentArguments = nullptr;
        eastl::vector<VKTaskDependencyResolver::TextureSubresourceSyncRequest> mPendingArgumentTextures;
        eastl::unordered_map<const VKTexture *, eastl::vector<size_t>> mArgumentTextureIndices;
        eastl::unordered_map<const VKTexture *, Texture> mExplicitSampledTextures;
        bool mEnded = false;
    };
} // namespace GVM::RHI::Vulkan
