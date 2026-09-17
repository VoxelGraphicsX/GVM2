#include "VKTextureSubresourceUtils.hpp"

#include "VKEnumUtils.hpp"
#include "VKTexture.hpp"

#include <GVMRHI/Private/GEnumUtils.hpp>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        uint32_t ceilDivide(uint32_t value, uint32_t divisor)
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        uint64_t calculateTightRowBytes(TextureFormat format, uint32_t width)
        {
            const GVM::RHI::Private::BlockInfo blockInfo = GVM::RHI::Private::getTextureBlockInfo(format);
            return static_cast<uint64_t>(ceilDivide(width, static_cast<uint32_t>(blockInfo.width))) * blockInfo.bytes;
        }

        uint32_t resolveBufferRowLength(TextureFormat format, uint32_t bytesPerRow)
        {
            if (bytesPerRow == 0)
            {
                return 0;
            }

            const GVM::RHI::Private::BlockInfo blockInfo = GVM::RHI::Private::getTextureBlockInfo(format);
            if ((bytesPerRow % blockInfo.bytes) != 0u)
            {
                throw makeInvalidArgument("Texture copy bytesPerRow violates block alignment.");
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

    uint64_t calculateRequiredTextureCopyBytes(
        TextureFormat format,
        const TextureDataLayout &layout,
        const Extent3D &size)
    {
        const GVM::RHI::Private::BlockInfo blockInfo = GVM::RHI::Private::getTextureBlockInfo(format);
        const uint32_t widthBlocks = ceilDivide(size.width, static_cast<uint32_t>(blockInfo.width));
        const uint32_t heightBlocks = ceilDivide(size.height, static_cast<uint32_t>(blockInfo.height));
        const uint64_t tightRowBytes = static_cast<uint64_t>(widthBlocks) * blockInfo.bytes;
        const uint64_t bytesPerRow = layout.bytesPerRow == 0 ? tightRowBytes : layout.bytesPerRow;

        if (bytesPerRow < tightRowBytes || (bytesPerRow % blockInfo.bytes) != 0u)
        {
            throw makeInvalidArgument("Texture copy bytesPerRow is smaller than the required tightly packed row or violates block alignment.");
        }

        const uint32_t rowsPerImageTexels = layout.rowsPerImage == 0 ? size.height : layout.rowsPerImage;
        const uint32_t rowsPerImageBlocks = ceilDivide(rowsPerImageTexels, static_cast<uint32_t>(blockInfo.height));

        return layout.offset +
            static_cast<uint64_t>(size.depth - 1u) * bytesPerRow * rowsPerImageBlocks +
            static_cast<uint64_t>(heightBlocks - 1u) * bytesPerRow +
            calculateTightRowBytes(format, size.width);
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
        region.bufferImageHeight = layout.rowsPerImage;
        region.imageSubresource = buildImageSubresourceLayers(texture, copy, copySize);
        region.imageOffset = buildImageOffset(texture, copy.origin);
        region.imageExtent = buildImageExtent(texture, copySize);
        return region;
    }
} // namespace GVM::RHI::Vulkan
