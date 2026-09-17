#include "GEnumUtils.hpp"

namespace GVM::RHI::Private
{


    uint64_t getTextureBlockWidth(TextureFormat format)
    {
        uint64_t blockWidth = 1;
        switch (format)
        {
        // Block-compressed formats with 4x4 blocks
        case TextureFormat::BC1RGBAUnorm:
        case TextureFormat::BC1RGBAUnormSrgb:
        case TextureFormat::BC2RGBAUnorm:
        case TextureFormat::BC2RGBAUnormSrgb:
        case TextureFormat::BC3RGBAUnorm:
        case TextureFormat::BC3RGBAUnormSrgb:
        case TextureFormat::BC4RUnorm:
        case TextureFormat::BC4RSnorm:
        case TextureFormat::BC5RGUnorm:
        case TextureFormat::BC5RGSnorm:
        case TextureFormat::BC6HRGBUfloat:
        case TextureFormat::BC6HRGBFloat:
        case TextureFormat::BC7RGBAUnorm:
        case TextureFormat::BC7RGBAUnormSrgb:
        case TextureFormat::ETC2RGB8Unorm:
        case TextureFormat::ETC2RGB8UnormSrgb:
        case TextureFormat::ETC2RGB8A1Unorm:
        case TextureFormat::ETC2RGB8A1UnormSrgb:
        case TextureFormat::EACR11Unorm:
        case TextureFormat::EACR11Snorm:
        case TextureFormat::EACRG11Unorm:
        case TextureFormat::EACRG11Snorm:
        case TextureFormat::ASTC4x4Unorm:
        case TextureFormat::ASTC4x4UnormSrgb:
            blockWidth = 4;
            break;

        // ASTC formats with block width of 5
        case TextureFormat::ASTC5x4Unorm:
        case TextureFormat::ASTC5x4UnormSrgb:
        case TextureFormat::ASTC5x5Unorm:
        case TextureFormat::ASTC5x5UnormSrgb:
            blockWidth = 5;
            break;

        // ASTC formats with block width of 6
        case TextureFormat::ASTC6x5Unorm:
        case TextureFormat::ASTC6x5UnormSrgb:
        case TextureFormat::ASTC6x6Unorm:
        case TextureFormat::ASTC6x6UnormSrgb:
            blockWidth = 6;
            break;

        // ASTC formats with block width of 8
        case TextureFormat::ASTC8x5Unorm:
        case TextureFormat::ASTC8x5UnormSrgb:
        case TextureFormat::ASTC8x6Unorm:
        case TextureFormat::ASTC8x6UnormSrgb:
        case TextureFormat::ASTC8x8Unorm:
        case TextureFormat::ASTC8x8UnormSrgb:
            blockWidth = 8;
            break;

        // ASTC formats with block width of 10
        case TextureFormat::ASTC10x5Unorm:
        case TextureFormat::ASTC10x5UnormSrgb:
        case TextureFormat::ASTC10x6Unorm:
        case TextureFormat::ASTC10x6UnormSrgb:
        case TextureFormat::ASTC10x8Unorm:
        case TextureFormat::ASTC10x8UnormSrgb:
        case TextureFormat::ASTC10x10Unorm:
        case TextureFormat::ASTC10x10UnormSrgb:
            blockWidth = 10;
            break;

        // ASTC formats with block width of 12
        case TextureFormat::ASTC12x10Unorm:
        case TextureFormat::ASTC12x10UnormSrgb:
        case TextureFormat::ASTC12x12Unorm:
        case TextureFormat::ASTC12x12UnormSrgb:
            blockWidth = 12;
            break;

        default:
            blockWidth = 1; // Uncompressed formats have block width of 1
            break;
        }
        return blockWidth;
    }

    uint64_t getTextureBlockHeight(TextureFormat format)
    {
        uint64_t blockHeight = 1;
        switch (format)
        {
        case TextureFormat::BC1RGBAUnorm:
        case TextureFormat::BC1RGBAUnormSrgb:
        case TextureFormat::BC2RGBAUnorm:
        case TextureFormat::BC2RGBAUnormSrgb:
        case TextureFormat::BC3RGBAUnorm:
        case TextureFormat::BC3RGBAUnormSrgb:
        case TextureFormat::BC4RUnorm:
        case TextureFormat::BC4RSnorm:
        case TextureFormat::BC5RGUnorm:
        case TextureFormat::BC5RGSnorm:
        case TextureFormat::BC6HRGBUfloat:
        case TextureFormat::BC6HRGBFloat:
        case TextureFormat::BC7RGBAUnorm:
        case TextureFormat::BC7RGBAUnormSrgb:
        case TextureFormat::ETC2RGB8Unorm:
        case TextureFormat::ETC2RGB8UnormSrgb:
        case TextureFormat::ETC2RGB8A1Unorm:
        case TextureFormat::ETC2RGB8A1UnormSrgb:
        case TextureFormat::EACR11Unorm:
        case TextureFormat::EACR11Snorm:
        case TextureFormat::EACRG11Unorm:
        case TextureFormat::EACRG11Snorm:
        case TextureFormat::ASTC4x4Unorm:
        case TextureFormat::ASTC4x4UnormSrgb:
            blockHeight = 4;
            break;

        case TextureFormat::ASTC5x4Unorm:
        case TextureFormat::ASTC5x4UnormSrgb:
            blockHeight = 4;
            break;

        case TextureFormat::ASTC5x5Unorm:
        case TextureFormat::ASTC5x5UnormSrgb:
            blockHeight = 5;
            break;

        case TextureFormat::ASTC6x5Unorm:
        case TextureFormat::ASTC6x5UnormSrgb:
            blockHeight = 5;
            break;

        case TextureFormat::ASTC6x6Unorm:
        case TextureFormat::ASTC6x6UnormSrgb:
            blockHeight = 6;
            break;

        case TextureFormat::ASTC8x5Unorm:
        case TextureFormat::ASTC8x5UnormSrgb:
            blockHeight = 5;
            break;

        case TextureFormat::ASTC8x6Unorm:
        case TextureFormat::ASTC8x6UnormSrgb:
            blockHeight = 6;
            break;

        case TextureFormat::ASTC8x8Unorm:
        case TextureFormat::ASTC8x8UnormSrgb:
            blockHeight = 8;
            break;

        case TextureFormat::ASTC10x5Unorm:
        case TextureFormat::ASTC10x5UnormSrgb:
            blockHeight = 5;
            break;

        case TextureFormat::ASTC10x6Unorm:
        case TextureFormat::ASTC10x6UnormSrgb:
            blockHeight = 6;
            break;

        case TextureFormat::ASTC10x8Unorm:
        case TextureFormat::ASTC10x8UnormSrgb:
            blockHeight = 8;
            break;

        case TextureFormat::ASTC10x10Unorm:
        case TextureFormat::ASTC10x10UnormSrgb:
            blockHeight = 10;
            break;

        case TextureFormat::ASTC12x10Unorm:
        case TextureFormat::ASTC12x10UnormSrgb:
            blockHeight = 10;
            break;

        case TextureFormat::ASTC12x12Unorm:
        case TextureFormat::ASTC12x12UnormSrgb:
            blockHeight = 12;
            break;

        default:
            blockHeight = 1;
            break;
        }
        return blockHeight;
    }

    uint64_t getTextureBytesPerBlock(TextureFormat format)
    {
        uint64_t bytesPerBlock = 1;
        switch (format)
        {
        // 1 byte per pixel formats
        case TextureFormat::R8Unorm:
        case TextureFormat::R8Snorm:
        case TextureFormat::R8Uint:
        case TextureFormat::R8Sint:
            bytesPerBlock = 1;
            break;

        // 2 bytes per pixel formats
        case TextureFormat::R16Uint:
        case TextureFormat::R16Sint:
        case TextureFormat::R16Float:
        case TextureFormat::Depth16Unorm:
        case TextureFormat::RG8Unorm:
        case TextureFormat::RG8Snorm:
        case TextureFormat::RG8Uint:
        case TextureFormat::RG8Sint:
            bytesPerBlock = 2;
            break;

        // 4 bytes per pixel formats
        case TextureFormat::R32Float:
        case TextureFormat::Depth32Float:
        case TextureFormat::R32Uint:
        case TextureFormat::R32Sint:
        case TextureFormat::RG16Uint:
        case TextureFormat::RG16Sint:
        case TextureFormat::RG16Float:
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::RGBA8UnormSrgb:
        case TextureFormat::RGBA8Snorm:
        case TextureFormat::RGBA8Uint:
        case TextureFormat::RGBA8Sint:
        case TextureFormat::BGRA8Unorm:
        case TextureFormat::BGRA8UnormSrgb:
        case TextureFormat::RGB10A2Uint:
        case TextureFormat::RGB10A2Unorm:
        case TextureFormat::RG11B10Ufloat:
        case TextureFormat::RGB9E5Ufloat:
            bytesPerBlock = 4;
            break;

        // 8 bytes per pixel formats
        case TextureFormat::RG32Float:
        case TextureFormat::RG32Uint:
        case TextureFormat::RG32Sint:
        case TextureFormat::RGBA16Uint:
        case TextureFormat::RGBA16Sint:
        case TextureFormat::RGBA16Float:
            bytesPerBlock = 8;
            break;

        // 16 bytes per pixel formats
        case TextureFormat::RGBA32Float:
        case TextureFormat::RGBA32Uint:
        case TextureFormat::RGBA32Sint:
            bytesPerBlock = 16;
            break;

        // Compressed formats with 8 bytes per block
        case TextureFormat::BC1RGBAUnorm:
        case TextureFormat::BC1RGBAUnormSrgb:
        case TextureFormat::BC4RUnorm:
        case TextureFormat::BC4RSnorm:
        case TextureFormat::ETC2RGB8Unorm:
        case TextureFormat::ETC2RGB8UnormSrgb:
        case TextureFormat::ETC2RGB8A1Unorm:
        case TextureFormat::ETC2RGB8A1UnormSrgb:
            bytesPerBlock = 8;
            break;

        // Compressed formats with 16 bytes per block
        case TextureFormat::BC2RGBAUnorm:
        case TextureFormat::BC2RGBAUnormSrgb:
        case TextureFormat::BC3RGBAUnorm:
        case TextureFormat::BC3RGBAUnormSrgb:
        case TextureFormat::BC5RGUnorm:
        case TextureFormat::BC5RGSnorm:
        case TextureFormat::BC6HRGBUfloat:
        case TextureFormat::BC6HRGBFloat:
        case TextureFormat::BC7RGBAUnorm:
        case TextureFormat::BC7RGBAUnormSrgb:
        case TextureFormat::EACR11Unorm:
        case TextureFormat::EACR11Snorm:
        case TextureFormat::EACRG11Unorm:
        case TextureFormat::EACRG11Snorm:
        case TextureFormat::ASTC4x4Unorm:
        case TextureFormat::ASTC4x4UnormSrgb:
        case TextureFormat::ASTC5x4Unorm:
        case TextureFormat::ASTC5x4UnormSrgb:
        case TextureFormat::ASTC5x5Unorm:
        case TextureFormat::ASTC5x5UnormSrgb:
        case TextureFormat::ASTC6x5Unorm:
        case TextureFormat::ASTC6x5UnormSrgb:
        case TextureFormat::ASTC6x6Unorm:
        case TextureFormat::ASTC6x6UnormSrgb:
        case TextureFormat::ASTC8x5Unorm:
        case TextureFormat::ASTC8x5UnormSrgb:
        case TextureFormat::ASTC8x6Unorm:
        case TextureFormat::ASTC8x6UnormSrgb:
        case TextureFormat::ASTC8x8Unorm:
        case TextureFormat::ASTC8x8UnormSrgb:
        case TextureFormat::ASTC10x5Unorm:
        case TextureFormat::ASTC10x5UnormSrgb:
        case TextureFormat::ASTC10x6Unorm:
        case TextureFormat::ASTC10x6UnormSrgb:
        case TextureFormat::ASTC10x8Unorm:
        case TextureFormat::ASTC10x8UnormSrgb:
        case TextureFormat::ASTC10x10Unorm:
        case TextureFormat::ASTC10x10UnormSrgb:
        case TextureFormat::ASTC12x10Unorm:
        case TextureFormat::ASTC12x10UnormSrgb:
        case TextureFormat::ASTC12x12Unorm:
        case TextureFormat::ASTC12x12UnormSrgb:
            bytesPerBlock = 16;
            break;

        default:
            throw std::runtime_error("Unsupported texture format");
        }
        return bytesPerBlock;
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
        if (format == TextureFormat::Depth16Unorm || format == TextureFormat::Depth24Plus || format == TextureFormat::Depth24PlusStencil8 || format == TextureFormat::Depth32Float || format == TextureFormat::Depth32FloatStencil8)
        {
            return true;
        }
        return false;
    }
} // namespace GVM::RHI::Private
