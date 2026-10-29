#pragma once

#include <EASTL/array.h>
#include <GVMRHI/GVMRHI.hpp>
#include <stdexcept>

namespace GVM::RHI::Private
{
    /** Static properties of one TextureFormat shared by every backend. */
    struct TextureFormatInfo
    {
        TextureFormat format = TextureFormat::Undefined;
        /** Texels covered by one compression block in X; 1 for uncompressed formats. */
        uint32_t blockWidth = 1;
        /** Texels covered by one compression block in Y; 1 for uncompressed formats. */
        uint32_t blockHeight = 1;
        /** Bytes per texel block; 0 marks formats with no fixed block size (the set getTextureBytesPerBlock rejects). */
        uint32_t bytesPerBlock = 0;
        /** Whether the format carries a depth aspect. */
        bool isDepth = false;
        /** Whether the format carries a stencil aspect. */
        bool hasStencil = false;
    };

    /** TextureFormat values 0x00..0x5F are contiguous; the table holds one row per value. */
    inline constexpr uint32_t TextureFormatCount = 0x60;

    /** Static properties of every TextureFormat, indexed by its enum value. */
    inline constexpr eastl::array<TextureFormatInfo, TextureFormatCount> TextureFormatTable = {{
            {.format = TextureFormat::Undefined, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 0u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R8Unorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 1u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R8Snorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 1u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R8Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 1u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R8Sint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 1u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R16Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 2u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R16Sint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 2u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R16Float, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 2u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG8Unorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 2u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG8Snorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 2u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG8Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 2u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG8Sint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 2u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R32Float, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R32Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::R32Sint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG16Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG16Sint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG16Float, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA8Unorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA8UnormSrgb, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA8Snorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA8Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA8Sint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BGRA8Unorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BGRA8UnormSrgb, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGB10A2Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGB10A2Unorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG11B10Ufloat, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGB9E5Ufloat, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG32Float, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG32Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RG32Sint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA16Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA16Sint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA16Float, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA32Float, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA32Uint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::RGBA32Sint, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::Stencil8, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 0u, .isDepth = false, .hasStencil = true},
            {.format = TextureFormat::Depth16Unorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 2u, .isDepth = true, .hasStencil = false},
            {.format = TextureFormat::Depth24Plus, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 0u, .isDepth = true, .hasStencil = false},
            {.format = TextureFormat::Depth24PlusStencil8, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 0u, .isDepth = true, .hasStencil = true},
            {.format = TextureFormat::Depth32Float, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 4u, .isDepth = true, .hasStencil = false},
            {.format = TextureFormat::Depth32FloatStencil8, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 0u, .isDepth = true, .hasStencil = true},
            {.format = TextureFormat::BC1RGBAUnorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC1RGBAUnormSrgb, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC2RGBAUnorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC2RGBAUnormSrgb, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC3RGBAUnorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC3RGBAUnormSrgb, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC4RUnorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC4RSnorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC5RGUnorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC5RGSnorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC6HRGBUfloat, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC6HRGBFloat, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC7RGBAUnorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::BC7RGBAUnormSrgb, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ETC2RGB8Unorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ETC2RGB8UnormSrgb, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ETC2RGB8A1Unorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ETC2RGB8A1UnormSrgb, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 8u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ETC2RGBA8Unorm, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 0u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ETC2RGBA8UnormSrgb, .blockWidth = 1u, .blockHeight = 1u, .bytesPerBlock = 0u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::EACR11Unorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::EACR11Snorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::EACRG11Unorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::EACRG11Snorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC4x4Unorm, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC4x4UnormSrgb, .blockWidth = 4u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC5x4Unorm, .blockWidth = 5u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC5x4UnormSrgb, .blockWidth = 5u, .blockHeight = 4u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC5x5Unorm, .blockWidth = 5u, .blockHeight = 5u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC5x5UnormSrgb, .blockWidth = 5u, .blockHeight = 5u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC6x5Unorm, .blockWidth = 6u, .blockHeight = 5u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC6x5UnormSrgb, .blockWidth = 6u, .blockHeight = 5u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC6x6Unorm, .blockWidth = 6u, .blockHeight = 6u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC6x6UnormSrgb, .blockWidth = 6u, .blockHeight = 6u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC8x5Unorm, .blockWidth = 8u, .blockHeight = 5u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC8x5UnormSrgb, .blockWidth = 8u, .blockHeight = 5u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC8x6Unorm, .blockWidth = 8u, .blockHeight = 6u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC8x6UnormSrgb, .blockWidth = 8u, .blockHeight = 6u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC8x8Unorm, .blockWidth = 8u, .blockHeight = 8u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC8x8UnormSrgb, .blockWidth = 8u, .blockHeight = 8u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC10x5Unorm, .blockWidth = 10u, .blockHeight = 5u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC10x5UnormSrgb, .blockWidth = 10u, .blockHeight = 5u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC10x6Unorm, .blockWidth = 10u, .blockHeight = 6u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC10x6UnormSrgb, .blockWidth = 10u, .blockHeight = 6u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC10x8Unorm, .blockWidth = 10u, .blockHeight = 8u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC10x8UnormSrgb, .blockWidth = 10u, .blockHeight = 8u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC10x10Unorm, .blockWidth = 10u, .blockHeight = 10u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC10x10UnormSrgb, .blockWidth = 10u, .blockHeight = 10u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC12x10Unorm, .blockWidth = 12u, .blockHeight = 10u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC12x10UnormSrgb, .blockWidth = 12u, .blockHeight = 10u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC12x12Unorm, .blockWidth = 12u, .blockHeight = 12u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
            {.format = TextureFormat::ASTC12x12UnormSrgb, .blockWidth = 12u, .blockHeight = 12u, .bytesPerBlock = 16u, .isDepth = false, .hasStencil = false},
    }};

    /** Returns true when row i of TextureFormatTable describes TextureFormat i, checked at compile time. */
    constexpr bool textureFormatTableIsOrdered()
    {
        for (uint32_t index = 0; index < TextureFormatCount; ++index)
        {
            if (TextureFormatTable[index].format != static_cast<TextureFormat>(index))
            {
                return false;
            }
        }
        return true;
    }
    static_assert(textureFormatTableIsOrdered(), "TextureFormatTable rows must appear in TextureFormat order.");

    /** Returns the table row for format; throws std::invalid_argument for values outside the table. */
    inline const TextureFormatInfo &getTextureFormatInfo(TextureFormat format)
    {
        const uint32_t index = static_cast<uint32_t>(format);
        if (index >= TextureFormatCount)
        {
            throw std::invalid_argument("TextureFormat value has no property table row.");
        }
        return TextureFormatTable[index];
    }
} // namespace GVM::RHI::Private
