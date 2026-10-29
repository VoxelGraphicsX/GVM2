#pragma once

#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/PixelLocalPassAccess.hpp>

#include <EASTL/optional.h>
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/utility.h>
#include <EASTL/variant.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::RHI::Private
{
    /** One texture subresource range a bind group, Args snapshot, or fullscreen source exposes to render or compute shaders. */
    struct PassTextureUse
    {
        const TextureImpl *texture = nullptr;
        TextureAspectFlags aspect = TextureAspect::All;
        uint32_t baseMipLevel = 0;
        uint32_t mipLevelCount = 1;
        uint32_t baseArrayLayer = 0;
        uint32_t arrayLayerCount = 1;
        bool storage = false;
        bool writes = false;
    };

    /** Shader stages that execute inside a render pass; texture uses visible only to other stages are not tracked by the recorder. */
    inline constexpr ShaderStageFlags RenderPassShaderStages = ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Hull | ShaderStage::Domain;

    /** Shader stages that execute inside a compute pass; texture uses visible only to other stages are not tracked by the recorder. */
    inline constexpr ShaderStageFlags ComputePassShaderStages = ShaderStage::Compute;

    /** Builds the render-pass use of one bound texture view: the subresource range comes from the view descriptor, and the storage and write roles from the binding's storage access (WriteOnly and ReadWrite write; ReadOnly and sampled bindings only read). */
    PassTextureUse makePassTextureUse(const TextureImpl &texture, const TextureViewDescriptor &view, StorageTextureAccess access);

    /** Parent texture and extent of one attachment, in descriptor order: color attachments, then depth if present. texture == nullptr means the descriptor view is null. */
    struct RenderPassAttachmentExtent
    {
        const TextureImpl *texture = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layerCount = 0;
    };

    /** Device limits the recorder validates against. */
    struct RenderPassRecorderLimits
    {
        uint32_t maxBindGroups = 0;
        uint32_t maxVertexBuffers = 0;
        uint32_t maxDrawIndirectCount = 0;
        uint32_t maxIndirectCountDrawCount = 0;
        bool multiDrawIndirect = false;
        bool drawIndirectCount = false;
    };

    /** Backend facts about the pipeline passed to setPipeline; pointers must stay valid while the pipeline is alive. */
    struct RenderPassPipelineFacts
    {
        const RenderPipelineDescriptor *descriptor = nullptr;
        PipelineLayout layout;
        const PipelineLayoutDescriptor *layoutDescriptor = nullptr;
        uint32_t pixelLocalInputAttachmentCount = 0;
        bool supportsDrawPixels = true;
    };

    /** Deferred viewport assignment replayed verbatim by the backend. */
    struct RenderSetViewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    /** Deferred scissor assignment replayed verbatim by the backend. */
    struct RenderSetScissorRect
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /** Deferred pipeline selection; replay resolves the native pipeline from the retained handle. */
    struct RenderSetPipeline
    {
        RenderPipeline pipeline = nullptr;
    };

    /** Deferred Args binding; payload indexes the backend-owned allocation side table. */
    struct RenderSetArguments
    {
        uint32_t payload = 0;
    };

    /** Deferred bind-group assignment at one pipeline-layout index. */
    struct RenderSetBindGroup
    {
        BindGroup group = nullptr;
        uint32_t index = 0;
    };

    /** Deferred vertex-buffer binding at one input slot. */
    struct RenderSetVertexBuffer
    {
        BufferRange buffer;
        uint32_t slot = 0;
    };

    /** Deferred index-buffer binding; payload indexes the backend-owned native buffer side table. */
    struct RenderSetIndexBuffer
    {
        BufferRange buffer;
        IndexFormat format = IndexFormat::Undefined;
        uint32_t payload = 0;
    };

    /** Deferred ordinary draw call. */
    struct RenderDraw
    {
        uint32_t vertexCount = 0;
        uint32_t instanceCount = 0;
        uint32_t firstVertex = 0;
        uint32_t firstInstance = 0;
    };

    /** Deferred indexed draw call. */
    struct RenderDrawIndexed
    {
        uint32_t indexCount = 0;
        uint32_t instanceCount = 0;
        uint32_t firstIndex = 0;
        int32_t baseVertex = 0;
        uint32_t firstInstance = 0;
    };

    /** Deferred indirect draw, indexed or ordinary, optionally GPU-counted; counter.buffer is null when not counted. */
    struct RenderDrawIndirect
    {
        BufferRange commands;
        BufferRange counter;
        uint32_t maxCount = 0;
        uint32_t stride = 0;
        bool indexed = false;
        uint32_t payload = 0;
    };

    /** Deferred fullscreen pixel-local draw for framebuffer-fetch pipelines. */
    struct RenderDrawPixels
    {
    };

    /** Deferred pixel-local pass boundary inside one render pass. */
    struct RenderNextPixelLocalPass
    {
    };

    /** One backend-neutral deferred render command; each backend maps it to native lowering during replay. */
    using RenderCommand = eastl::variant<
        RenderSetViewport,
        RenderSetScissorRect,
        RenderSetPipeline,
        RenderSetArguments,
        RenderSetBindGroup,
        RenderSetVertexBuffer,
        RenderSetIndexBuffer,
        RenderDraw,
        RenderDrawIndexed,
        RenderDrawIndirect,
        RenderDrawPixels,
        RenderNextPixelLocalPass>;

    /**
     * Owns the deferred render command list and all deterministic user-error validation for one render pass.
     * Every validation error is raised at the offending encoder call, or at init/end for pass-level rules, so
     * backend replay contains no user-error checks. Mutating methods give the strong exception guarantee:
     * a rejected call leaves the recorded state untouched. Not thread-safe; callers hold their encoder lock.
     */
    class RenderPassRecorder
    {
    public:
        /** Returns the byte size of one index element; throws std::invalid_argument for non-concrete formats. */
        static uint32_t getIndexFormatSize(IndexFormat format);

        /** Validates attachment extents, registers attachment parents, and resets all recorded state. */
        void init(const RenderPassDescriptor &descriptor, eastl::span<const RenderPassAttachmentExtent> attachments, const RenderPassRecorderLimits &limits);

        void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth);
        void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
        void setPipeline(RenderPipeline pipeline, const RenderPassPipelineFacts &facts);
        /** Validates snapshot layout compatibility; call before the backend allocates its Args interval. */
        void checkArguments(const PipelineLayout &snapshotLayout) const;
        /** Copies the render-visible texture uses and records the Args binding; cannot fail after checkArguments. */
        void setArguments(eastl::span<const PassTextureUse> textureUses, uint32_t payload);
        void setBindGroup(BindGroup group, uint32_t groupIndex, const BindGroupLayout &layout, eastl::span<const PassTextureUse> textureUses);
        void setVertexBuffer(BufferRange buffer, uint32_t slot);
        void setIndexBuffer(BufferRange buffer, IndexFormat format, uint32_t payload);
        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance);
        /** Records one indirect draw; the caller keeps the early return for count == 0. */
        void drawIndirect(BufferRange commands, uint32_t count, uint32_t stride, bool indexed, uint32_t payload);
        /** Validates and records one GPU-counted indirect draw; returns false when maxDrawCount == 0 and nothing was recorded. */
        bool drawIndirectCount(BufferRange commands, BufferRange counter, uint32_t maxDrawCount, uint32_t stride, bool indexed, uint32_t payload);
        void drawPixels();
        void addFullscreenTextureSource(const Texture &source);
        void nextPixelLocalPass();
        /** Validates pass-level rules and returns per-phase pixel-local accesses (empty when pixelLocal is disabled). */
        eastl::vector<PixelLocalPassAttachmentAccess> end();
        /** Returns the recorded command list for backend replay. */
        eastl::span<const RenderCommand> getCommands() const;

    private:
        /** One bind-group slot's desired layout for the pipeline-satisfaction check. */
        struct BoundGroup
        {
            BindGroup group = nullptr;
            BindGroupLayout layout = nullptr;
        };

        /** Role assignment queued by one validated batch before it is applied to mTextureRoles. */
        struct PendingRole
        {
            const TextureImpl *texture = nullptr;
            bool storage = false;
        };

        /** Returns the declared pixel-local pass count, or 1 when pixelLocal is disabled. */
        uint32_t getPixelLocalPassCount() const;
        /** Requires a pipeline bound inside the current pixel-local phase. */
        void requirePipelineBound(const char *apiName) const;
        /** Requires a bound Args snapshot when the current pipeline declares an Args layout. */
        void requireArgumentsSnapshot() const;
        /** Requires every non-null pipeline-layout bind-group slot to hold a group with an identical layout. */
        void requireSatisfiedBindGroups(const char *apiName);
        /** Requires a bound index buffer and a stripIndexFormat compatible with the current topology. */
        void requireIndexBufferForIndexedDraw(const char *apiName) const;
        /** Validates all current Args texture uses against earlier uses, then records the survivors. */
        void trackArgumentTextureUses();
        /** Validates one non-merged use's role and attachment rules against current and pending state. */
        void checkTextureUseRoleAndAttachment(const PassTextureUse &use, const eastl::vector<PendingRole> &pendingRoles) const;
        /** Validates every use in one batch without mutating state, then applies the surviving roles. */
        void applyTextureUses(eastl::span<const PassTextureUse> textureUses);
        /** Requires that the texture is not an attachment of this render pass. */
        void checkTextureNotAttachment(const TextureImpl *texture) const;
        /** Resolves the current pipeline's attachment access; returns nothing when pixelLocal is disabled. May throw before any draw mutates state. */
        eastl::optional<PixelLocalPassAttachmentAccess> resolvePipelineAttachmentAccess() const;
        /** Appends a resolved access to the pixel-local pass builder and merges it into the current phase; cannot fail. */
        void applyPipelineAttachmentAccess(const PixelLocalPassAttachmentAccess &access);
        /** Requires the finishing phase to satisfy the largest input-attachment requirement set within it. */
        void validatePhaseInputAttachments() const;
        /** Builds the unresolved bind-group message for one failed draw-time check. */
        eastl::string buildMissingBindGroupMessage(const char *apiName) const;

        RenderPassDescriptor mDescriptor = {};
        RenderPassRecorderLimits mLimits = {};
        eastl::vector<RenderCommand> mCommands;
        RenderPipeline mPipeline = nullptr;
        RenderPassPipelineFacts mPipelineFacts = {};
        bool mPipelineBoundInPhase = false;
        bool mHasArguments = false;
        eastl::vector<PassTextureUse> mArgumentTextureUses;
        eastl::vector<BoundGroup> mBoundGroups;
        eastl::unordered_set<const BindGroupImpl *> mScannedBindGroups;
        bool mBindGroupCheckDirty = true;
        bool mBindGroupsSatisfied = false;
        BufferRange mIndexBuffer = {};
        IndexFormat mIndexFormat = IndexFormat::Undefined;
        eastl::unordered_map<const TextureImpl *, bool> mTextureRoles;
        eastl::unordered_set<const TextureImpl *> mAttachmentTextures;
        eastl::unordered_map<const TextureImpl *, eastl::vector<PassTextureUse>> mRecordedArgumentUses;
        PixelLocalPassAccessBuilder mAccessBuilder;
        PixelLocalPassAttachmentAccess mCurrentPhaseAccess = {};
        uint32_t mCurrentPhaseIndex = 0;
        uint32_t mPhaseMaxInputAttachmentCount = 0;
    };
} // namespace GVM::RHI::Private
