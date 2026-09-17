#pragma once

#include "VKCommon.hpp"

#include <cstdint>

namespace GVM::RHI::Vulkan::CacheDetail
{
    struct RenderPassColorAttachmentSignature
    {
        TextureFormat format = TextureFormat::Undefined;
        LoadOp loadOp = LoadOp::Undefined;
        StoreOp storeOp = StoreOp::Undefined;
        Bool pixelLocal = false;

        bool operator==(const RenderPassColorAttachmentSignature &rhs) const;
    };

    struct RenderPassDepthStencilAttachmentSignature
    {
        TextureFormat format = TextureFormat::Undefined;
        LoadOp loadOp = LoadOp::Undefined;
        StoreOp storeOp = StoreOp::Undefined;
        Bool pixelLocal = false;

        bool operator==(const RenderPassDepthStencilAttachmentSignature &rhs) const;
    };

    struct PipelineCompatibleRenderPassSignature
    {
        eastl::vector<RenderPassColorAttachmentSignature> colorAttachments;
        RenderPassDepthStencilAttachmentSignature depthStencilAttachment;
        Bool pixelLocalEnabled = false;
        PixelLocalPassAttachmentAccess pixelLocalAttachmentAccess = {};

        bool operator==(const PipelineCompatibleRenderPassSignature &rhs) const;
    };

    uint64_t hashRenderPassSignature(
        const eastl::vector<RenderPassColorAttachmentSignature> &colorAttachments,
        const RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
        uint32_t pixelLocalPassCount,
        const eastl::vector<PixelLocalPassAttachmentAccess> &pixelLocalAttachmentAccesses);

    uint64_t hashPipelineRenderPassSignature(const PipelineCompatibleRenderPassSignature &signature);

    uint64_t hashFramebufferSignature(
        vk::RenderPass renderPass,
        const eastl::vector<vk::ImageView> &attachments,
        uint32_t width,
        uint32_t height,
        uint32_t layers);
} // namespace GVM::RHI::Vulkan::CacheDetail
