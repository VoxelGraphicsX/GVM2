#include "VKRenderPassHashUtils.hpp"

#include "Private/RHIHashXXH64.hpp"

namespace GVM::RHI::Vulkan::CacheDetail
{
    bool RenderPassColorAttachmentSignature::operator==(const RenderPassColorAttachmentSignature &rhs) const
    {
        return format == rhs.format &&
            loadOp == rhs.loadOp &&
            storeOp == rhs.storeOp &&
            pixelLocal == rhs.pixelLocal;
    }

    bool RenderPassDepthStencilAttachmentSignature::operator==(const RenderPassDepthStencilAttachmentSignature &rhs) const
    {
        return format == rhs.format &&
            loadOp == rhs.loadOp &&
            storeOp == rhs.storeOp &&
            pixelLocal == rhs.pixelLocal;
    }

    bool PipelineCompatibleRenderPassSignature::operator==(const PipelineCompatibleRenderPassSignature &rhs) const
    {
        return colorAttachments == rhs.colorAttachments &&
            depthStencilAttachment == rhs.depthStencilAttachment &&
            pixelLocalEnabled == rhs.pixelLocalEnabled &&
            pixelLocalAttachmentAccess == rhs.pixelLocalAttachmentAccess;
    }

    uint64_t hashRenderPassSignature(
        const eastl::vector<RenderPassColorAttachmentSignature> &colorAttachments,
        const RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
        uint32_t pixelLocalPassCount,
        const eastl::vector<PixelLocalPassAttachmentAccess> &pixelLocalAttachmentAccesses)
    {
        GVM::RHI::Detail::XXH64State state;
        const uint64_t colorAttachmentCount = static_cast<uint64_t>(colorAttachments.size());
        state.updatePod(colorAttachmentCount);
        for (const RenderPassColorAttachmentSignature &attachment : colorAttachments)
        {
            state.updateEnum(attachment.format);
            state.updateEnum(attachment.loadOp);
            state.updateEnum(attachment.storeOp);
            state.updatePod(attachment.pixelLocal);
        }
        state.updateEnum(depthStencilAttachment.format);
        state.updateEnum(depthStencilAttachment.loadOp);
        state.updateEnum(depthStencilAttachment.storeOp);
        state.updatePod(depthStencilAttachment.pixelLocal);
        state.updatePod(pixelLocalPassCount);
        const uint64_t pixelLocalAccessCount = static_cast<uint64_t>(pixelLocalAttachmentAccesses.size());
        state.updatePod(pixelLocalAccessCount);
        for (const PixelLocalPassAttachmentAccess &access : pixelLocalAttachmentAccesses)
        {
            state.updatePod(access.colorReadMask);
            state.updatePod(access.colorWriteMask);
            state.updatePod(access.depthWrite);
        }
        return state.digest();
    }

    uint64_t hashPipelineRenderPassSignature(const PipelineCompatibleRenderPassSignature &signature)
    {
        GVM::RHI::Detail::XXH64State state;
        const uint64_t colorAttachmentCount = static_cast<uint64_t>(signature.colorAttachments.size());
        state.updatePod(colorAttachmentCount);
        for (const RenderPassColorAttachmentSignature &attachment : signature.colorAttachments)
        {
            state.updateEnum(attachment.format);
            state.updatePod(attachment.pixelLocal);
        }
        state.updateEnum(signature.depthStencilAttachment.format);
        state.updatePod(signature.depthStencilAttachment.pixelLocal);
        state.updatePod(signature.pixelLocalEnabled);
        state.updatePod(signature.pixelLocalAttachmentAccess.colorReadMask);
        state.updatePod(signature.pixelLocalAttachmentAccess.colorWriteMask);
        state.updatePod(signature.pixelLocalAttachmentAccess.depthWrite);
        return state.digest();
    }

    uint64_t hashFramebufferSignature(
        vk::RenderPass renderPass,
        const eastl::vector<vk::ImageView> &attachments,
        uint32_t width,
        uint32_t height,
        uint32_t layers)
    {
        GVM::RHI::Detail::XXH64State state;
        const VkRenderPass nativeRenderPass = static_cast<VkRenderPass>(renderPass);
        state.updatePod(nativeRenderPass);
        const uint64_t attachmentCount = static_cast<uint64_t>(attachments.size());
        state.updatePod(attachmentCount);
        for (vk::ImageView attachment : attachments)
        {
            const VkImageView nativeImageView = static_cast<VkImageView>(attachment);
            state.updatePod(nativeImageView);
        }
        state.updatePod(width);
        state.updatePod(height);
        state.updatePod(layers);
        return state.digest();
    }
} // namespace GVM::RHI::Vulkan::CacheDetail
