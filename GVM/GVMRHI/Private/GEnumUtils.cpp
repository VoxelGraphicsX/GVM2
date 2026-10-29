#include "GEnumUtils.hpp"
#include "TextureFormatTable.hpp"
#include <stdexcept>

namespace GVM::RHI::Private
{
    uint32_t resolveIndirectDrawStride(const BufferRange &range, uint32_t count, uint32_t stride, bool indexed)
    {
        const uint32_t commandSize = indexed ? sizeof(IndirectIndexedRenderCommand) : sizeof(IndirectRenderCommand);
        const uint32_t resolved = stride == 0 ? commandSize : stride;
        if (resolved < commandSize || resolved % 4u != 0)
        {
            throw std::invalid_argument("Indirect draw stride must cover one native command and be aligned to four bytes.");
        }
        if (count != 0 && (uint64_t(count - 1u) * resolved + commandSize > range.size))
        {
            throw std::out_of_range("Indirect draw commands exceed their captured buffer range.");
        }
        return resolved;
    }


    uint64_t getTextureBlockWidth(TextureFormat format)
    {
        const uint32_t index = static_cast<uint32_t>(format);
        return index < TextureFormatCount ? TextureFormatTable[index].blockWidth : 1u;
    }

    uint64_t getTextureBlockHeight(TextureFormat format)
    {
        const uint32_t index = static_cast<uint32_t>(format);
        return index < TextureFormatCount ? TextureFormatTable[index].blockHeight : 1u;
    }

    uint64_t getTextureBytesPerBlock(TextureFormat format)
    {
        const uint32_t index = static_cast<uint32_t>(format);
        if (index >= TextureFormatCount || TextureFormatTable[index].bytesPerBlock == 0u)
        {
            throw std::runtime_error("Unsupported texture format");
        }
        return TextureFormatTable[index].bytesPerBlock;
    }
    BlockInfo getTextureBlockInfo(TextureFormat format)
    {
        return {.width = getTextureBlockWidth(format), .height = getTextureBlockHeight(format), .bytes = getTextureBytesPerBlock(format)};
    }
    uint64_t getIndexBufferElementStorageSizeFromFormat(IndexFormat format)
    {
        uint64_t result = 0;
        switch (format)
        {
        case IndexFormat::Uint16:
            result = 2;
            break;
        case IndexFormat::Uint32:
            result = 4;
            break;
        default:
            throw std::runtime_error("Unsupported index format");
        }
        return result;
    }

    GVM::RHI::TextureViewDimension getTextureViewDimension(GVM::RHI::TextureDimension dim, uint32_t arrayLayers)
    {
        GVM::RHI::TextureViewDimension res;
        if (dim == TextureDimension::e1D)
        {
            res = GVM::RHI::TextureViewDimension::e1D;
        }
        else if (dim == TextureDimension::e2D)
        {
            res = arrayLayers > 1 ? GVM::RHI::TextureViewDimension::e2DArray : GVM::RHI::TextureViewDimension::e2D;
        }
        else if (dim == TextureDimension::e3D)
        {
            res = GVM::RHI::TextureViewDimension::e3D;
        }
        else
        {
            throw std::runtime_error("Unsupported texture dimension");
        }
        return res;
    }

    bool isDepthFormat(TextureFormat format)
    {
        const uint32_t index = static_cast<uint32_t>(format);
        return index < TextureFormatCount && TextureFormatTable[index].isDepth;
    }

    bool hasStencilAspect(TextureFormat format)
    {
        const uint32_t index = static_cast<uint32_t>(format);
        return index < TextureFormatCount && TextureFormatTable[index].hasStencil;
    }

    bool isDepthStencilFormat(TextureFormat format)
    {
        return isDepthFormat(format) || hasStencilAspect(format);
    }
    TextureSampleType getTextureSampleType(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::R8Uint:
        case TextureFormat::R16Uint:
        case TextureFormat::RG8Uint:
        case TextureFormat::R32Uint:
        case TextureFormat::RG16Uint:
        case TextureFormat::RGBA8Uint:
        case TextureFormat::RG32Uint:
        case TextureFormat::RGBA16Uint:
        case TextureFormat::RGBA32Uint:
        case TextureFormat::Stencil8:
            return TextureSampleType::Uint;
        case TextureFormat::R8Sint:
        case TextureFormat::R16Sint:
        case TextureFormat::RG8Sint:
        case TextureFormat::R32Sint:
        case TextureFormat::RG16Sint:
        case TextureFormat::RGBA8Sint:
        case TextureFormat::RG32Sint:
        case TextureFormat::RGBA16Sint:
        case TextureFormat::RGBA32Sint:
            return TextureSampleType::Sint;
        default:
            return isDepthFormat(format) ? TextureSampleType::Depth : TextureSampleType::Float;
        }
    }
} // namespace GVM::RHI::Private
