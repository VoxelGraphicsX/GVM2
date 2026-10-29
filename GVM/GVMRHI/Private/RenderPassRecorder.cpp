#include "RenderPassRecorder.hpp"

#include <GVMRHI/Private/ArgumentValidation.hpp>
#include <GVMRHI/Private/TextureViewResolution.hpp>
#include <GVMRHI/Private/GEnumUtils.hpp>

#include <EASTL/algorithm.h>

#include <stdexcept>

namespace GVM::RHI::Private
{
    namespace
    {
        /** Reports whether a primitive topology indexes vertices with a strip restart format. */
        bool isStripTopology(PrimitiveTopology topology)
        {
            return topology == PrimitiveTopology::LineStrip || topology == PrimitiveTopology::TriangleStrip;
        }

        /** Reports whether two recorded Args texture uses share any texel, aspect, mip level, or layer. */
        bool textureUsesOverlap(const PassTextureUse &lhs, const PassTextureUse &rhs)
        {
            return lhs.baseMipLevel < rhs.baseMipLevel + rhs.mipLevelCount &&
                rhs.baseMipLevel < lhs.baseMipLevel + lhs.mipLevelCount &&
                lhs.baseArrayLayer < rhs.baseArrayLayer + rhs.arrayLayerCount &&
                rhs.baseArrayLayer < lhs.baseArrayLayer + lhs.arrayLayerCount &&
                (lhs.aspect == TextureAspect::All || rhs.aspect == TextureAspect::All || (lhs.aspect & rhs.aspect) != 0);
        }

        /** Reports whether two Args texture uses describe the same subresource range and access mode. */
        bool isIdenticalTextureUse(const PassTextureUse &lhs, const PassTextureUse &rhs)
        {
            return lhs.aspect == rhs.aspect &&
                lhs.baseMipLevel == rhs.baseMipLevel && lhs.mipLevelCount == rhs.mipLevelCount &&
                lhs.baseArrayLayer == rhs.baseArrayLayer && lhs.arrayLayerCount == rhs.arrayLayerCount &&
                lhs.storage == rhs.storage && lhs.writes == rhs.writes;
        }
    } // namespace

    PassTextureUse makePassTextureUse(const TextureImpl &texture, const TextureViewDescriptor &view, StorageTextureAccess access)
    {
        return {
            &texture,
            view.aspect,
            view.baseMipLevel,
            view.mipLevelCount,
            view.baseArrayLayer,
            view.arrayLayerCount,
            access != StorageTextureAccess::Undefined,
            access == StorageTextureAccess::WriteOnly || access == StorageTextureAccess::ReadWrite,
        };
    }

    uint32_t RenderPassRecorder::getIndexFormatSize(IndexFormat format)
    {
        switch (format)
        {
        case IndexFormat::Uint16:
            return 2u;
        case IndexFormat::Uint32:
            return 4u;
        default:
            throw std::invalid_argument("Index binding requires a concrete index format.");
        }
    }

    void RenderPassRecorder::init(const RenderPassDescriptor &descriptor, eastl::span<const RenderPassAttachmentExtent> attachments, const RenderPassRecorderLimits &limits)
    {
        mDescriptor = descriptor;
        mLimits = limits;
        mCommands.clear();
        mPipeline = nullptr;
        mPipelineFacts = {};
        mPipelineBoundInPhase = false;
        mHasArguments = false;
        mArgumentTextureUses.clear();
        mBoundGroups.clear();
        mBoundGroups.resize(limits.maxBindGroups);
        mScannedBindGroups.clear();
        mBindGroupCheckDirty = true;
        mBindGroupsSatisfied = false;
        mIndexBuffer = {};
        mIndexFormat = IndexFormat::Undefined;
        mTextureRoles.clear();
        mAttachmentTextures.clear();
        mRecordedArgumentUses.clear();
        mAccessBuilder = PixelLocalPassAccessBuilder{};
        mCurrentPhaseAccess = {};
        mCurrentPhaseIndex = 0;
        mPhaseMaxInputAttachmentCount = 0;

        const size_t colorAttachmentCount = descriptor.colorAttachments.size();
        for (size_t index = 0; index < colorAttachmentCount; ++index)
        {
            if (index >= attachments.size() || attachments[index].texture == nullptr)
            {
                throw std::invalid_argument("RenderPassEncoder received a null color attachment view.");
            }
        }

        bool hasAttachment = false;
        uint32_t passWidth = 0;
        uint32_t passHeight = 0;
        uint32_t passLayers = 0;
        for (const RenderPassAttachmentExtent &attachment : attachments)
        {
            if (attachment.texture == nullptr)
            {
                continue;
            }
            if (!hasAttachment)
            {
                passWidth = attachment.width;
                passHeight = attachment.height;
                passLayers = attachment.layerCount;
                hasAttachment = true;
            }
            else if (attachment.width != passWidth || attachment.height != passHeight || attachment.layerCount != passLayers)
            {
                throw std::invalid_argument("RenderPassEncoder requires all attachments in a render pass to share the same extent and layer count.");
            }
            mAttachmentTextures.insert(attachment.texture);
        }
        if (!hasAttachment)
        {
            throw std::invalid_argument("RenderPassEncoder requires at least one valid attachment.");
        }
    }

    void RenderPassRecorder::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        mCommands.push_back(RenderSetViewport{x, y, width, height, minDepth, maxDepth});
    }

    void RenderPassRecorder::setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        mCommands.push_back(RenderSetScissorRect{x, y, width, height});
    }

    void RenderPassRecorder::setPipeline(RenderPipeline pipeline, const RenderPassPipelineFacts &facts)
    {
        if (pipeline == nullptr)
        {
            throw std::invalid_argument("RenderPassEncoder::setPipeline requires a valid pipeline.");
        }
        if (facts.pixelLocalInputAttachmentCount > 0u && (!mDescriptor.pixelLocal.enabled || mCurrentPhaseIndex == 0u))
        {
            throw std::logic_error("RenderPassEncoder cannot bind pixelLocal input attachments before a previous pixelLocal pass has completed.");
        }
        if (pipeline.get() != mPipeline.get())
        {
            mHasArguments = false;
            mArgumentTextureUses.clear();
        }
        mPipeline = pipeline;
        mPipelineFacts = facts;
        mPipelineBoundInPhase = true;
        mBindGroupCheckDirty = true;
        if (facts.pixelLocalInputAttachmentCount > mPhaseMaxInputAttachmentCount)
        {
            mPhaseMaxInputAttachmentCount = facts.pixelLocalInputAttachmentCount;
        }
        mCommands.push_back(RenderSetPipeline{pipeline});
    }

    void RenderPassRecorder::checkArguments(const PipelineLayout &snapshotLayout) const
    {
        if (mPipeline == nullptr || snapshotLayout != mPipelineFacts.layout)
        {
            throw std::invalid_argument("Render Args snapshot does not belong to the selected pipeline layout.");
        }
    }

    void RenderPassRecorder::setArguments(eastl::span<const PassTextureUse> textureUses, uint32_t payload)
    {
        mArgumentTextureUses.assign(textureUses.begin(), textureUses.end());
        mHasArguments = true;
        mCommands.push_back(RenderSetArguments{payload});
    }

    void RenderPassRecorder::setBindGroup(BindGroup group, uint32_t groupIndex, const BindGroupLayout &layout, eastl::span<const PassTextureUse> textureUses)
    {
        if (group == nullptr)
        {
            throw std::invalid_argument("RenderPassEncoder::setBindGroup requires a valid bind group.");
        }
        if (groupIndex >= mLimits.maxBindGroups)
        {
            throw std::invalid_argument((eastl::string("RenderPassEncoder::setBindGroup supports at most ") +
                eastl::to_string(mLimits.maxBindGroups) + " bind groups.").c_str());
        }
        if (mScannedBindGroups.find(group.get()) == mScannedBindGroups.end())
        {
            applyTextureUses(textureUses);
            mScannedBindGroups.insert(group.get());
        }
        mBoundGroups[groupIndex] = BoundGroup{group, layout};
        mBindGroupCheckDirty = true;
        mCommands.push_back(RenderSetBindGroup{group, groupIndex});
    }

    void RenderPassRecorder::setVertexBuffer(BufferRange buffer, uint32_t slot)
    {
        if (buffer.buffer.isNull())
        {
            throw std::invalid_argument("RenderPassEncoder::setVertexBuffer requires a valid buffer.");
        }
        if (slot >= mLimits.maxVertexBuffers)
        {
            throw std::invalid_argument("RenderPassEncoder::setVertexBuffer exceeds the device vertex buffer limit.");
        }
        mCommands.push_back(RenderSetVertexBuffer{buffer, slot});
    }

    void RenderPassRecorder::setIndexBuffer(BufferRange buffer, IndexFormat format, uint32_t payload)
    {
        (void)getIndexFormatSize(format);
        mIndexBuffer = buffer;
        mIndexFormat = format;
        mCommands.push_back(RenderSetIndexBuffer{buffer, format, payload});
    }

    void RenderPassRecorder::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        requirePipelineBound("draw");
        requireArgumentsSnapshot();
        requireSatisfiedBindGroups("draw");
        const auto attachmentAccess = resolvePipelineAttachmentAccess();
        trackArgumentTextureUses();
        if (attachmentAccess.has_value())
        {
            applyPipelineAttachmentAccess(*attachmentAccess);
        }
        mCommands.push_back(RenderDraw{vertexCount, instanceCount, firstVertex, firstInstance});
    }

    void RenderPassRecorder::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance)
    {
        requirePipelineBound("drawIndexed");
        requireArgumentsSnapshot();
        requireSatisfiedBindGroups("drawIndexed");
        requireIndexBufferForIndexedDraw("drawIndexed");
        const uint64_t elementSize = getIndexFormatSize(mIndexFormat);
        const uint64_t firstByte = uint64_t(firstIndex) * elementSize;
        if (firstByte > mIndexBuffer.size || uint64_t(indexCount) * elementSize > mIndexBuffer.size - firstByte)
        {
            throw std::out_of_range("Indexed draw exceeds its current index buffer range.");
        }
        const auto attachmentAccess = resolvePipelineAttachmentAccess();
        trackArgumentTextureUses();
        if (attachmentAccess.has_value())
        {
            applyPipelineAttachmentAccess(*attachmentAccess);
        }
        mCommands.push_back(RenderDrawIndexed{indexCount, instanceCount, firstIndex, baseVertex, firstInstance});
    }

    void RenderPassRecorder::drawIndirect(BufferRange commands, uint32_t count, uint32_t stride, bool indexed, uint32_t payload)
    {
        const char *apiName = indexed ? "drawIndexedIndirect" : "drawIndirect";
        requirePipelineBound(apiName);
        requireArgumentsSnapshot();
        requireSatisfiedBindGroups(apiName);
        if (indexed)
        {
            requireIndexBufferForIndexedDraw(apiName);
        }
        stride = resolveIndirectDrawStride(commands, count, stride, indexed);
        if (count > mLimits.maxDrawIndirectCount || (count > 1u && !mLimits.multiDrawIndirect))
        {
            throw std::out_of_range("Indirect draw count exceeds the enabled device limit.");
        }
        const auto attachmentAccess = resolvePipelineAttachmentAccess();
        trackArgumentTextureUses();
        if (attachmentAccess.has_value())
        {
            applyPipelineAttachmentAccess(*attachmentAccess);
        }
        mCommands.push_back(RenderDrawIndirect{commands, BufferRange{}, count, stride, indexed, payload});
    }

    bool RenderPassRecorder::drawIndirectCount(BufferRange commands, BufferRange counter, uint32_t maxDrawCount, uint32_t stride, bool indexed, uint32_t payload)
    {
        const char *apiName = indexed ? "drawIndexedIndirectCount" : "drawIndirectCount";
        requirePipelineBound(apiName);
        requireArgumentsSnapshot();
        requireSatisfiedBindGroups(apiName);
        if (indexed)
        {
            requireIndexBufferForIndexedDraw(apiName);
        }
        stride = resolveIndirectDrawStride(commands, maxDrawCount, stride, indexed);
        if (counter.size < sizeof(uint32_t))
        {
            throw std::out_of_range("Indirect count requires one aligned uint32.");
        }
        counter.size = sizeof(uint32_t);
        if (!mLimits.drawIndirectCount)
        {
            throw std::logic_error("RenderPassEncoder drawIndirectCount is not supported by the enabled device features.");
        }
        if (maxDrawCount > mLimits.maxIndirectCountDrawCount)
        {
            throw std::out_of_range("Indirect draw count exceeds the enabled device limit.");
        }
        if (maxDrawCount == 0u)
        {
            return false;
        }
        const auto attachmentAccess = resolvePipelineAttachmentAccess();
        trackArgumentTextureUses();
        if (attachmentAccess.has_value())
        {
            applyPipelineAttachmentAccess(*attachmentAccess);
        }
        mCommands.push_back(RenderDrawIndirect{commands, counter, maxDrawCount, stride, indexed, payload});
        return true;
    }

    void RenderPassRecorder::drawPixels()
    {
        requirePipelineBound("drawPixels");
        requireArgumentsSnapshot();
        requireSatisfiedBindGroups("drawPixels");
        if (!mPipelineFacts.supportsDrawPixels)
        {
            throw std::logic_error("RenderPassEncoder::drawPixels requires a pipeline that supports drawPixels.");
        }
        const auto attachmentAccess = resolvePipelineAttachmentAccess();
        trackArgumentTextureUses();
        if (attachmentAccess.has_value())
        {
            applyPipelineAttachmentAccess(*attachmentAccess);
        }
        mCommands.push_back(RenderDrawPixels{});
    }

    void RenderPassRecorder::addFullscreenTextureSource(const Texture &source)
    {
        if (source.isNull())
        {
            throw std::invalid_argument("RenderPassEncoder::drawFullscreenTexture requires a valid source texture.");
        }
        if (mDescriptor.colorAttachments.empty())
        {
            throw std::invalid_argument("RenderPassEncoder::drawFullscreenTexture requires a color attachment target.");
        }
        if (source->getDepth() != 1u || source->getArrayLayerCount() != 1u)
        {
            throw std::invalid_argument("RenderPassEncoder::drawFullscreenTexture currently only supports non-array 2D textures.");
        }
        const TextureImpl *texture = source.get();
        const auto found = mTextureRoles.find(texture);
        if (found != mTextureRoles.end() && found->second)
        {
            throw std::logic_error((eastl::string("Render pass '") + mDescriptor.label + "': texture '" +
                describeTextureLabel(texture) +
                "' is used both as a sampled texture and as a storage texture; split these accesses into separate render passes.").c_str());
        }
        mTextureRoles[texture] = false;
    }

    void RenderPassRecorder::nextPixelLocalPass()
    {
        const uint32_t pixelLocalPassCount = getPixelLocalPassCount();
        if (pixelLocalPassCount <= 1u)
        {
            throw std::logic_error("RenderPassEncoder::nextPixelLocalPass requires a render pass with multiple pixelLocal passes.");
        }
        if (mCurrentPhaseIndex + 1u >= pixelLocalPassCount)
        {
            throw std::logic_error("RenderPassEncoder::nextPixelLocalPass exceeded the render pass pixelLocal pass count.");
        }
        validatePhaseInputAttachments();
        mCurrentPhaseAccess = {};
        mPhaseMaxInputAttachmentCount = 0;
        mAccessBuilder.nextPass();
        ++mCurrentPhaseIndex;
        mPipelineBoundInPhase = false;
        mCommands.push_back(RenderNextPixelLocalPass{});
    }

    eastl::vector<PixelLocalPassAttachmentAccess> RenderPassRecorder::end()
    {
        const uint32_t pixelLocalPassCount = getPixelLocalPassCount();
        if (pixelLocalPassCount > 1u && mCurrentPhaseIndex + 1u != pixelLocalPassCount)
        {
            throw std::logic_error("RenderPassEncoder::end called before all declared pixelLocal passes were reached.");
        }
        validatePhaseInputAttachments();
        if (!mDescriptor.pixelLocal.enabled)
        {
            return {};
        }
        return mAccessBuilder.finish();
    }

    eastl::span<const RenderCommand> RenderPassRecorder::getCommands() const
    {
        return {mCommands.data(), mCommands.size()};
    }

    uint32_t RenderPassRecorder::getPixelLocalPassCount() const
    {
        return mDescriptor.pixelLocal.enabled
            ? eastl::max(1u, mDescriptor.pixelLocal.passCount)
            : 1u;
    }

    void RenderPassRecorder::requirePipelineBound(const char *apiName) const
    {
        if (!mPipelineBoundInPhase)
        {
            throw std::logic_error((eastl::string("RenderPassEncoder::") + apiName +
                " requires a pipeline to be bound first.").c_str());
        }
    }

    void RenderPassRecorder::requireArgumentsSnapshot() const
    {
        const bool pipelineDeclaresArguments = mPipelineFacts.layoutDescriptor != nullptr &&
            mPipelineFacts.layoutDescriptor->arguments.byteSize != 0u;
        requireArgumentSnapshot(pipelineDeclaresArguments, mHasArguments);
    }

    void RenderPassRecorder::requireSatisfiedBindGroups(const char *apiName)
    {
        if (mPipelineFacts.layoutDescriptor == nullptr)
        {
            return;
        }
        if (mBindGroupCheckDirty)
        {
            mBindGroupsSatisfied = true;
            const eastl::vector<BindGroupLayout> &required = mPipelineFacts.layoutDescriptor->bindGroupLayouts;
            for (uint32_t groupIndex = 0; groupIndex < required.size(); ++groupIndex)
            {
                if (required[groupIndex] == nullptr)
                {
                    continue;
                }
                if (groupIndex >= mBoundGroups.size() || mBoundGroups[groupIndex].layout.get() != required[groupIndex].get())
                {
                    mBindGroupsSatisfied = false;
                    break;
                }
            }
            mBindGroupCheckDirty = false;
        }
        if (!mBindGroupsSatisfied)
        {
            throw std::logic_error(buildMissingBindGroupMessage(apiName).c_str());
        }
    }

    void RenderPassRecorder::requireIndexBufferForIndexedDraw(const char *apiName) const
    {
        if (mIndexFormat == IndexFormat::Undefined)
        {
            throw std::logic_error((eastl::string("RenderPassEncoder::") + apiName +
                " requires an index buffer to be bound for indexed drawing.").c_str());
        }
        const RenderPipelineDescriptor *descriptor = mPipelineFacts.descriptor;
        if (descriptor != nullptr && isStripTopology(descriptor->primitive.topology))
        {
            if (descriptor->primitive.stripIndexFormat == IndexFormat::Undefined)
            {
                throw std::logic_error((eastl::string("RenderPassEncoder::") + apiName +
                    " requires primitive.stripIndexFormat for strip topologies.").c_str());
            }
            if (descriptor->primitive.stripIndexFormat != mIndexFormat)
            {
                throw std::logic_error((eastl::string("RenderPassEncoder::") + apiName +
                    " index format does not match pipeline primitive.stripIndexFormat.").c_str());
            }
        }
    }

    void RenderPassRecorder::trackArgumentTextureUses()
    {
        eastl::vector<const PassTextureUse *> pendingUses;
        eastl::vector<PendingRole> pendingRoles;
        for (const PassTextureUse &use : mArgumentTextureUses)
        {
            if (use.texture == nullptr)
            {
                continue;
            }
            const auto recorded = mRecordedArgumentUses.find(use.texture);
            bool identical = false;
            if (recorded != mRecordedArgumentUses.end())
            {
                for (const PassTextureUse &previous : recorded->second)
                {
                    if (textureUsesOverlap(previous, use) && (previous.writes || use.writes))
                    {
                        throw std::logic_error((eastl::string("Render pass '") + mDescriptor.label +
                            "': Args image writes followed by overlapping image access require separate render passes.").c_str());
                    }
                    if (isIdenticalTextureUse(previous, use))
                    {
                        identical = true;
                    }
                }
            }
            if (!identical)
            {
                for (const PassTextureUse *pending : pendingUses)
                {
                    if (pending->texture != use.texture)
                    {
                        continue;
                    }
                    if (textureUsesOverlap(*pending, use) && (pending->writes || use.writes))
                    {
                        throw std::logic_error((eastl::string("Render pass '") + mDescriptor.label +
                            "': Args image writes followed by overlapping image access require separate render passes.").c_str());
                    }
                    if (isIdenticalTextureUse(*pending, use))
                    {
                        identical = true;
                    }
                }
            }
            if (identical)
            {
                continue;
            }
            checkTextureUseRoleAndAttachment(use, pendingRoles);
            pendingRoles.push_back({use.texture, use.storage});
            pendingUses.push_back(&use);
        }
        for (const PendingRole &role : pendingRoles)
        {
            mTextureRoles[role.texture] = role.storage;
        }
        for (const PassTextureUse *use : pendingUses)
        {
            mRecordedArgumentUses[use->texture].push_back(*use);
        }
    }

    void RenderPassRecorder::checkTextureUseRoleAndAttachment(const PassTextureUse &use, const eastl::vector<PendingRole> &pendingRoles) const
    {
        const auto found = mTextureRoles.find(use.texture);
        bool conflicting = found != mTextureRoles.end() && found->second != use.storage;
        if (!conflicting)
        {
            for (const PendingRole &pending : pendingRoles)
            {
                if (pending.texture == use.texture && pending.storage != use.storage)
                {
                    conflicting = true;
                    break;
                }
            }
        }
        if (conflicting)
        {
            throw std::logic_error((eastl::string("Render pass '") + mDescriptor.label + "': texture '" +
                describeTextureLabel(use.texture) +
                "' is used both as a sampled texture and as a storage texture; split these accesses into separate render passes.").c_str());
        }
        checkTextureNotAttachment(use.texture);
    }

    void RenderPassRecorder::applyTextureUses(eastl::span<const PassTextureUse> textureUses)
    {
        eastl::vector<PendingRole> pendingRoles;
        for (const PassTextureUse &use : textureUses)
        {
            if (use.texture == nullptr)
            {
                continue;
            }
            checkTextureUseRoleAndAttachment(use, pendingRoles);
            pendingRoles.push_back({use.texture, use.storage});
        }
        for (const PendingRole &role : pendingRoles)
        {
            mTextureRoles[role.texture] = role.storage;
        }
    }

    void RenderPassRecorder::checkTextureNotAttachment(const TextureImpl *texture) const
    {
        if (mAttachmentTextures.find(texture) == mAttachmentTextures.end())
        {
            return;
        }
        throw std::logic_error((eastl::string("Render pass '") + mDescriptor.label + "': texture '" +
            describeTextureLabel(texture) +
            "' is an attachment of this render pass and cannot also be sampled or used as a storage texture.").c_str());
    }

    eastl::optional<PixelLocalPassAttachmentAccess> RenderPassRecorder::resolvePipelineAttachmentAccess() const
    {
        if (!mDescriptor.pixelLocal.enabled)
        {
            return eastl::nullopt;
        }
        const RenderPipelineDescriptor *descriptor = mPipelineFacts.descriptor;
        PixelLocalPassAttachmentAccess access;
        if (descriptor != nullptr && hasExplicitPixelLocalAttachmentAccess(descriptor->pixelLocalAttachmentAccess))
        {
            access = descriptor->pixelLocalAttachmentAccess;
        }
        else if (mPipelineFacts.pixelLocalInputAttachmentCount > 0u)
        {
            throw std::logic_error("RenderPassEncoder requires explicit pixelLocalAttachmentAccess metadata for pixel-local input attachment pipelines.");
        }
        else if (descriptor != nullptr)
        {
            access = derivePixelLocalAttachmentWriteAccessFromRenderPipelineDescriptor(*descriptor);
        }
        return access;
    }

    void RenderPassRecorder::applyPipelineAttachmentAccess(const PixelLocalPassAttachmentAccess &access)
    {
        mAccessBuilder.appendTask(access);
        mergePixelLocalPassAttachmentAccess(mCurrentPhaseAccess, access);
    }

    void RenderPassRecorder::validatePhaseInputAttachments() const
    {
        if (!mDescriptor.pixelLocal.enabled || mPhaseMaxInputAttachmentCount == 0u)
        {
            return;
        }
        uint32_t readableCount = 0;
        for (uint32_t index = 0; index < mDescriptor.colorAttachments.size(); ++index)
        {
            if (mDescriptor.colorAttachments[index].pixelLocal &&
                isPixelLocalColorAttachmentRead(mCurrentPhaseAccess, index))
            {
                ++readableCount;
            }
        }
        if (mPhaseMaxInputAttachmentCount > readableCount)
        {
            throw std::logic_error("RenderPassEncoder pixelLocal input attachment index exceeds the current pixelLocal pass input range.");
        }
    }

    eastl::string RenderPassRecorder::buildMissingBindGroupMessage(const char *apiName) const
    {
        eastl::string message = eastl::string("RenderPassEncoder::") + apiName +
            " cannot execute because pipeline '";
        message += (mPipeline != nullptr && !mPipeline->getLabelName().empty())
            ? mPipeline->getLabelName().c_str()
            : "<unnamed>";
        message += "' has unresolved bind groups: ";

        bool hasIssue = false;
        if (mPipelineFacts.layoutDescriptor != nullptr)
        {
            const eastl::vector<BindGroupLayout> &required = mPipelineFacts.layoutDescriptor->bindGroupLayouts;
            for (uint32_t groupIndex = 0; groupIndex < required.size(); ++groupIndex)
            {
                if (required[groupIndex] == nullptr)
                {
                    continue;
                }
                const bool bound = groupIndex < mBoundGroups.size() && mBoundGroups[groupIndex].group != nullptr;
                const bool compatible = bound && mBoundGroups[groupIndex].layout.get() == required[groupIndex].get();
                if (compatible)
                {
                    continue;
                }
                if (hasIssue)
                {
                    message += "; ";
                }
                message += "set ";
                message += eastl::to_string(groupIndex);
                if (!bound)
                {
                    message += " not bound";
                }
                else
                {
                    message += " bound to incompatible bind group '";
                    const eastl::string &groupLabel = mBoundGroups[groupIndex].group->getLabelName();
                    message += groupLabel.empty() ? "<unnamed>" : groupLabel.c_str();
                    message += "'";
                }
                hasIssue = true;
            }
        }

        if (!hasIssue)
        {
            return message;
        }
        message += ". Bound sets: ";
        bool firstBound = true;
        for (uint32_t groupIndex = 0; groupIndex < mBoundGroups.size(); ++groupIndex)
        {
            const BoundGroup &bound = mBoundGroups[groupIndex];
            if (bound.group == nullptr)
            {
                continue;
            }
            if (!firstBound)
            {
                message += ", ";
            }
            firstBound = false;
            message += eastl::to_string(groupIndex);
            message += "='";
            const eastl::string &groupLabel = bound.group->getLabelName();
            message += groupLabel.empty() ? "<unnamed>" : groupLabel.c_str();
            message += "'";
        }
        return message;
    }
} // namespace GVM::RHI::Private
