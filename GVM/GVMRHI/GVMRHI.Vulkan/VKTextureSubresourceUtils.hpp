#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    struct ResolvedTextureSubresourceRange
    {
        uint32_t baseMipLevel = 0u;
        uint32_t mipLevelCount = 0u;
        uint32_t baseArrayLayer = 0u;
        uint32_t arrayLayerCount = 0u;
        vk::ImageAspectFlags nativeAspectMask = {};

        [[nodiscard]]
        uint32_t mipLevelEnd() const;

        [[nodiscard]]
        uint32_t arrayLayerEnd() const;
    };

    [[nodiscard]]
    ResolvedTextureSubresourceRange resolveTextureSubresourceRange(
        const VKTexture *texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount);

    [[nodiscard]]
    bool texturePlaneMatches(
        const ResolvedTextureSubresourceRange &range,
        vk::ImageAspectFlagBits planeAspect);

    [[nodiscard]]
    uint32_t resolveTextureSubresourceIndex(
        const VKTexture *texture,
        uint32_t mipLevel,
        uint32_t arrayLayer);

    [[nodiscard]]
    uint32_t resolveTexturePlaneSubresourceCount(const VKTexture *texture);

    [[nodiscard]]
    eastl::vector<vk::ImageAspectFlagBits> resolveTexturePlaneAspects(const VKTexture *texture);

    [[nodiscard]]
    vk::ImageSubresourceRange buildTextureSubresourceRange(
        const VKTexture *texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount);

    [[nodiscard]]
    uint64_t calculateRequiredTextureCopyBytes(
        TextureFormat format,
        const TextureDataLayout &layout,
        const Extent3D &size);

    [[nodiscard]]
    uint32_t resolveImageCopyBaseArrayLayer(
        const VKTexture *texture,
        const ImageCopyTexture &copy);

    [[nodiscard]]
    uint32_t resolveImageCopyLayerCount(
        const VKTexture *texture,
        const Extent3D &copySize);

    [[nodiscard]]
    vk::BufferImageCopy buildBufferImageCopy(
        const VKTexture *texture,
        uint64_t bufferOffset,
        const TextureDataLayout &layout,
        const ImageCopyTexture &copy,
        const Extent3D &copySize);
} // namespace GVM::RHI::Vulkan
