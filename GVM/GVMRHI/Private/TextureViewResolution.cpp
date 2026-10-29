#include "TextureViewResolution.hpp"

#include "GEnumUtils.hpp"

#include <stdexcept>

namespace GVM::RHI::Private
{
    namespace
    {
        /** Returns the image layer count used by view range checks; 3D textures always have exactly one layer. */
        uint32_t resolveViewArrayLayerCount(const TextureDescriptor &descriptor)
        {
            return descriptor.dimension == TextureDimension::e3D ? 1u : descriptor.arrayLayerCount;
        }

        /** Validates the resolved view dimension against the texture shape and resolved layer count. */
        void validateResolvedTextureViewDescriptorOrThrow(const TextureDescriptor &textureDescriptor, const TextureViewDescriptor &viewDescriptor)
        {
            if (viewDescriptor.dimension == TextureViewDimension::Undefined)
            {
                throw std::invalid_argument((
                    "createView texture '" + describeTextureLabel(textureDescriptor) +
                    "' resolved to an undefined texture-view dimension. Texture descriptor: " +
                    describeTextureShape(textureDescriptor) + ". View descriptor: " +
                    describeTextureViewShape(viewDescriptor) + ".").c_str());
            }

            if (viewDescriptor.dimension == TextureViewDimension::e1D || textureDescriptor.dimension == TextureDimension::e1D)
            {
                throw std::invalid_argument((
                    "createView texture '" + describeTextureLabel(textureDescriptor) +
                    "' resolved to an unexpected 1D texture/view. GVM does not author 1D textures, so this indicates an upstream descriptor/codegen bug. Texture descriptor: " +
                    describeTextureShape(textureDescriptor) + ". View descriptor: " +
                    describeTextureViewShape(viewDescriptor) + ".").c_str());
            }

            switch (viewDescriptor.dimension)
            {
            case TextureViewDimension::e2D:
                if (textureDescriptor.dimension != TextureDimension::e2D || viewDescriptor.arrayLayerCount != 1u)
                {
                    throw std::invalid_argument((
                        "createView texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a 2D view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".").c_str());
                }
                break;
            case TextureViewDimension::e2DArray:
                if (textureDescriptor.dimension != TextureDimension::e2D || viewDescriptor.arrayLayerCount <= 1u)
                {
                    throw std::invalid_argument((
                        "createView texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a 2D-array view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".").c_str());
                }
                break;
            case TextureViewDimension::Cube:
                if (textureDescriptor.dimension != TextureDimension::e2D || viewDescriptor.arrayLayerCount != 6u)
                {
                    throw std::invalid_argument((
                        "createView texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a cube view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".").c_str());
                }
                break;
            case TextureViewDimension::CubeArray:
                if (textureDescriptor.dimension != TextureDimension::e2D ||
                    viewDescriptor.arrayLayerCount < 6u ||
                    (viewDescriptor.arrayLayerCount % 6u) != 0u)
                {
                    throw std::invalid_argument((
                        "createView texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a cube-array view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".").c_str());
                }
                break;
            case TextureViewDimension::e3D:
                if (textureDescriptor.dimension != TextureDimension::e3D ||
                    viewDescriptor.baseArrayLayer != 0u ||
                    viewDescriptor.arrayLayerCount != 1u)
                {
                    throw std::invalid_argument((
                        "createView texture '" + describeTextureLabel(textureDescriptor) +
                        "' cannot create a 3D view from the resolved descriptors. Texture descriptor: " +
                        describeTextureShape(textureDescriptor) + ". View descriptor: " +
                        describeTextureViewShape(viewDescriptor) + ".").c_str());
                }
                break;
            default:
                throw std::invalid_argument((
                    "createView texture '" + describeTextureLabel(textureDescriptor) +
                    "' encountered unsupported view dimension " + eastl::string(describeTextureViewDimension(viewDescriptor.dimension)) +
                    ". Texture descriptor: " + describeTextureShape(textureDescriptor) +
                    ". View descriptor: " + describeTextureViewShape(viewDescriptor) + ".").c_str());
            }
        }
    } // namespace

    eastl::string describeTextureLabel(const TextureDescriptor &descriptor)
    {
        return descriptor.label.empty() ? "unnamed texture" : eastl::string(descriptor.label.c_str());
    }

    eastl::string describeTextureLabel(const TextureImpl *texture)
    {
        if (texture == nullptr)
        {
            return "<null>";
        }
        return texture->getLabelName().empty() ? eastl::string("<unnamed>") : eastl::string(texture->getLabelName().c_str());
    }

    const char *describeTextureDimension(TextureDimension dimension)
    {
        switch (dimension)
        {
        case TextureDimension::e1D: return "e1D";
        case TextureDimension::e2D: return "e2D";
        case TextureDimension::e3D: return "e3D";
        default: return "Unknown";
        }
    }

    const char *describeTextureViewDimension(TextureViewDimension dimension)
    {
        switch (dimension)
        {
        case TextureViewDimension::Undefined: return "Undefined";
        case TextureViewDimension::e1D: return "e1D";
        case TextureViewDimension::e2D: return "e2D";
        case TextureViewDimension::e2DArray: return "e2DArray";
        case TextureViewDimension::Cube: return "Cube";
        case TextureViewDimension::CubeArray: return "CubeArray";
        case TextureViewDimension::e3D: return "e3D";
        default: return "Unknown";
        }
    }

    eastl::string describeTextureShape(const TextureDescriptor &descriptor)
    {
        return "dimension=" + eastl::string(describeTextureDimension(descriptor.dimension)) +
            ", size=(" + eastl::to_string(descriptor.size.width) + "x" +
            eastl::to_string(descriptor.size.height) + "x" +
            eastl::to_string(descriptor.size.depth) + "), layers=" +
            eastl::to_string(descriptor.arrayLayerCount) + ", mipLevels=" +
            eastl::to_string(descriptor.mipLevelCount);
    }

    eastl::string describeTextureViewShape(const TextureViewDescriptor &descriptor)
    {
        return "viewDimension=" + eastl::string(describeTextureViewDimension(descriptor.dimension)) +
            ", format=" + eastl::to_string(static_cast<uint32_t>(descriptor.format)) +
            ", baseMipLevel=" + eastl::to_string(descriptor.baseMipLevel) +
            ", mipLevelCount=" + eastl::to_string(descriptor.mipLevelCount) +
            ", baseArrayLayer=" + eastl::to_string(descriptor.baseArrayLayer) +
            ", arrayLayerCount=" + eastl::to_string(descriptor.arrayLayerCount);
    }

    TextureViewDescriptor resolveTextureViewDescriptor(const TextureDescriptor &texture, const TextureViewDescriptor &view)
    {
        const uint32_t totalArrayLayerCount = resolveViewArrayLayerCount(texture);
        if (view.baseMipLevel >= texture.mipLevelCount)
        {
            throw std::out_of_range("createView received baseMipLevel outside the texture range.");
        }
        if (view.baseArrayLayer >= totalArrayLayerCount)
        {
            throw std::out_of_range("createView received baseArrayLayer outside the texture range.");
        }

        const uint32_t resolvedMipLevelCount = view.mipLevelCount == 0
            ? (texture.mipLevelCount - view.baseMipLevel)
            : view.mipLevelCount;
        const uint32_t resolvedArrayLayerCount = view.arrayLayerCount == 0
            ? (totalArrayLayerCount - view.baseArrayLayer)
            : view.arrayLayerCount;

        if (resolvedMipLevelCount > texture.mipLevelCount - view.baseMipLevel)
        {
            throw std::out_of_range("createView requested more mip levels than the texture owns.");
        }
        if (resolvedArrayLayerCount > totalArrayLayerCount - view.baseArrayLayer)
        {
            throw std::out_of_range("createView requested more array layers than the texture owns.");
        }

        TextureViewDescriptor resolved = view;
        resolved.format = view.format == TextureFormat::Undefined ? texture.format : view.format;
        resolved.dimension = view.dimension == TextureViewDimension::Undefined
            ? getTextureViewDimension(texture.dimension, resolvedArrayLayerCount)
            : view.dimension;
        resolved.mipLevelCount = resolvedMipLevelCount;
        resolved.arrayLayerCount = resolvedArrayLayerCount;
        validateResolvedTextureViewDescriptorOrThrow(texture, resolved);
        return resolved;
    }

    bool equalTextureViewDescriptors(const TextureViewDescriptor &lhs, const TextureViewDescriptor &rhs)
    {
        return lhs.format == rhs.format &&
            lhs.dimension == rhs.dimension &&
            lhs.baseMipLevel == rhs.baseMipLevel &&
            lhs.mipLevelCount == rhs.mipLevelCount &&
            lhs.baseArrayLayer == rhs.baseArrayLayer &&
            lhs.arrayLayerCount == rhs.arrayLayerCount &&
            lhs.aspect == rhs.aspect;
    }

    TextureView TextureViewCache::find(const TextureViewDescriptor &resolved) const
    {
        for (const Entry &entry : mEntries)
        {
            if (equalTextureViewDescriptors(entry.descriptor, resolved))
            {
                return entry.view;
            }
        }
        return {};
    }

    void TextureViewCache::insert(const TextureViewDescriptor &resolved, TextureView view)
    {
        mEntries.push_back({.descriptor = resolved, .view = view});
    }

    void TextureViewCache::clear()
    {
        mEntries.clear();
    }
} // namespace GVM::RHI::Private
