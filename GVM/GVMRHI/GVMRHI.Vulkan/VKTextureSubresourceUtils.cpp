#include "VKTextureSubresourceUtils.hpp"

#include "VKEnumUtils.hpp"
#include "VKTexture.hpp"

#include <GVMRHI/Private/GEnumUtils.hpp>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        /** Converts a bytes-per-block-row stride into the texel row length Vulkan expects, or 0 for tightly packed rows. */
        uint32_t resolveBufferRowLength(TextureFormat format, uint32_t bytesPerRow)
        {
            if (bytesPerRow == 0)
            {
                return 0;
            }

            const GVM::RHI::Private::BlockInfo blockInfo = GVM::RHI::Private::getTextureBlockInfo(format);
            if ((bytesPerRow % blockInfo.bytes) != 0u)
            {
                throw std::invalid_argument("Texture copy bytesPerRow violates block alignment.");
            }
            return static_cast<uint32_t>((bytesPerRow / blockInfo.bytes) * blockInfo.width);
        }

        vk::Offset3D buildImageOffset(const VKTexture *texture, const Origin3D &origin)
        {
            return vk::Offset3D{
                static_cast<int32_t>(origin.x),
                static_cast<int32_t>(texture->getDimension() == TextureDimension::e1D ? 0u : origin.y),
                static_cast<int32_t>(texture->is3D() ? origin.z : 0u)};
        }

        vk::Extent3D buildImageExtent(const VKTexture *texture, const Extent3D &extent)
        {
            return vk::Extent3D{
                extent.width,
                texture->getDimension() == TextureDimension::e1D ? 1u : extent.height,
                texture->is3D() ? extent.depth : 1u};
        }

        vk::ImageSubresourceLayers buildImageSubresourceLayers(
            const VKTexture *texture,
            const ImageCopyTexture &copy,
            const Extent3D &copySize)
        {
            vk::ImageSubresourceLayers subresource = {};
            subresource.aspectMask = resolveTextureAspect(texture->getFormat(), copy.aspect);
            subresource.mipLevel = copy.mipLevel;
            subresource.baseArrayLayer = resolveImageCopyBaseArrayLayer(texture, copy);
            subresource.layerCount = resolveImageCopyLayerCount(texture, copySize);
            return subresource;
        }
    } // namespace

    uint32_t ResolvedTextureSubresourceRange::mipLevelEnd() const
    {
        return baseMipLevel + mipLevelCount;
    }

    uint32_t ResolvedTextureSubresourceRange::arrayLayerEnd() const
    {
        return baseArrayLayer + arrayLayerCount;
    }

    ResolvedTextureSubresourceRange resolveTextureSubresourceRange(
        const VKTexture *texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount)
    {
        ResolvedTextureSubresourceRange range = {};
        if (texture == nullptr)
        {
            return range;
        }

        range.baseMipLevel = baseMipLevel;
        range.mipLevelCount = mipLevelCount == 0u ? (texture->getMipLevelCount() - baseMipLevel) : mipLevelCount;
        range.baseArrayLayer = baseArrayLayer;
        range.arrayLayerCount = arrayLayerCount == 0u ? (texture->getCachedArrayLayerCount() - baseArrayLayer) : arrayLayerCount;
        range.nativeAspectMask = resolveTextureAspect(texture->getFormat(), aspectMask);
        return range;
    }

    bool texturePlaneMatches(
        const ResolvedTextureSubresourceRange &range,
        vk::ImageAspectFlagBits planeAspect)
    {
        return (range.nativeAspectMask & planeAspect) == planeAspect;
    }

    uint32_t resolveTextureSubresourceIndex(
        const VKTexture *texture,
        uint32_t mipLevel,
        uint32_t arrayLayer)
    {
        return mipLevel * texture->getCachedArrayLayerCount() + arrayLayer;
    }

    uint32_t resolveTexturePlaneSubresourceCount(const VKTexture *texture)
    {
        if (texture == nullptr)
        {
            return 0u;
        }

        return texture->getMipLevelCount() * texture->getCachedArrayLayerCount();
    }

    eastl::vector<vk::ImageAspectFlagBits> resolveTexturePlaneAspects(const VKTexture *texture)
    {
        eastl::vector<vk::ImageAspectFlagBits> aspects;
        if (texture == nullptr)
        {
            return aspects;
        }

        const vk::ImageAspectFlags nativeAspectMask = resolveTextureAspect(texture->getFormat(), TextureAspect::All);
        if ((nativeAspectMask & vk::ImageAspectFlagBits::eColor) == vk::ImageAspectFlagBits::eColor)
        {
            aspects.push_back(vk::ImageAspectFlagBits::eColor);
        }
        if ((nativeAspectMask & vk::ImageAspectFlagBits::eDepth) == vk::ImageAspectFlagBits::eDepth)
        {
            aspects.push_back(vk::ImageAspectFlagBits::eDepth);
        }
        if ((nativeAspectMask & vk::ImageAspectFlagBits::eStencil) == vk::ImageAspectFlagBits::eStencil)
        {
            aspects.push_back(vk::ImageAspectFlagBits::eStencil);
        }
        return aspects;
    }

    vk::ImageSubresourceRange buildTextureSubresourceRange(
        const VKTexture *texture,
        TextureAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t mipLevelCount,
        uint32_t baseArrayLayer,
        uint32_t arrayLayerCount)
    {
        if (texture == nullptr)
        {
            return {};
        }

        return texture->buildSubresourceRange(
            aspectMask,
            baseMipLevel,
            mipLevelCount,
            baseArrayLayer,
            arrayLayerCount);
    }

    uint32_t resolveImageCopyBaseArrayLayer(
        const VKTexture *texture,
        const ImageCopyTexture &copy)
    {
        return texture->is3D() ? 0u : copy.origin.z;
    }

    uint32_t resolveImageCopyLayerCount(
        const VKTexture *texture,
        const Extent3D &copySize)
    {
        return texture->is3D() ? 1u : copySize.depth;
    }

    vk::BufferImageCopy buildBufferImageCopy(
        const VKTexture *texture,
        uint64_t bufferOffset,
        const TextureDataLayout &layout,
        const ImageCopyTexture &copy,
        const Extent3D &copySize)
    {
        vk::BufferImageCopy region = {};
        region.bufferOffset = bufferOffset + layout.offset;
        region.bufferRowLength = resolveBufferRowLength(texture->getFormat(), layout.bytesPerRow);
        // TextureDataLayout::rowsPerImage counts texel-block rows while bufferImageHeight counts texels.
        region.bufferImageHeight = layout.rowsPerImage == 0u
            ? 0u
            : static_cast<uint32_t>(static_cast<uint64_t>(layout.rowsPerImage) * GVM::RHI::Private::getTextureBlockHeight(texture->getFormat()));
        region.imageSubresource = buildImageSubresourceLayers(texture, copy, copySize);
        region.imageOffset = buildImageOffset(texture, copy.origin);
        region.imageExtent = buildImageExtent(texture, copySize);
        return region;
    }
} // namespace GVM::RHI::Vulkan
