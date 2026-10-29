#include "TextureCopyFootprint.hpp"

#include "GEnumUtils.hpp"

#include <EASTL/string.h>

#include <stdexcept>

namespace GVM::RHI::Private
{
    namespace
    {
        /** Multiplies transfer byte counts without allowing the result to wrap. */
        uint64_t multiplyTextureCopyBytes(uint64_t left, uint64_t right)
        {
            if (right != 0u && left > ~uint64_t(0) / right)
            {
                throw std::length_error("Texture transfer byte size exceeds the supported range.");
            }
            return left * right;
        }

        /** Adds transfer byte counts without allowing the result to wrap. */
        uint64_t addTextureCopyBytes(uint64_t left, uint64_t right)
        {
            if (left > ~uint64_t(0) - right)
            {
                throw std::length_error("Texture transfer byte size exceeds the supported range.");
            }
            return left + right;
        }

        /** Aligns a transfer byte count up without allowing the result to wrap. */
        uint64_t alignTextureCopyBytes(uint64_t value, uint64_t alignment)
        {
            if (alignment <= 1u)
            {
                return value;
            }
            const uint64_t remainder = value % alignment;
            const uint64_t padding = remainder == 0u ? 0u : alignment - remainder;
            return addTextureCopyBytes(value, padding);
        }
    } // namespace

    TextureCopyFootprint computeTextureCopyFootprint(
        const char *apiName,
        TextureFormat format,
        const TextureDataLayout &layout,
        const Extent3D &size,
        uint64_t stagingRowAlignment)
    {
        const char *context = apiName != nullptr ? apiName : "texture copy";
        if ((layout.offset % 4u) != 0u)
        {
            throw std::invalid_argument((eastl::string(context) + " layout.offset must be a multiple of 4 bytes.").c_str());
        }

        const BlockInfo blockInfo = getTextureBlockInfo(format);
        const uint32_t blocksX = static_cast<uint32_t>((static_cast<uint64_t>(size.width) + blockInfo.width - 1u) / blockInfo.width);
        const uint32_t blocksY = static_cast<uint32_t>((static_cast<uint64_t>(size.height) + blockInfo.height - 1u) / blockInfo.height);
        const uint64_t tightRowBytes = static_cast<uint64_t>(blocksX) * blockInfo.bytes;
        const uint64_t bytesPerRow = layout.bytesPerRow != 0u ? layout.bytesPerRow : tightRowBytes;
        const uint64_t rowsPerImage = layout.rowsPerImage != 0u ? layout.rowsPerImage : blocksY;

        if (bytesPerRow < tightRowBytes)
        {
            throw std::invalid_argument((eastl::string(context) + " bytesPerRow is smaller than the tightly packed row size.").c_str());
        }
        if ((bytesPerRow % blockInfo.bytes) != 0u)
        {
            throw std::invalid_argument((eastl::string(context) + " bytesPerRow must be a multiple of the texel-block byte size.").c_str());
        }
        if (rowsPerImage < blocksY)
        {
            throw std::invalid_argument((eastl::string(context) + " rowsPerImage is smaller than the texture block row count.").c_str());
        }

        const uint64_t bytesPerImage = multiplyTextureCopyBytes(rowsPerImage, bytesPerRow);
        const uint64_t minimumDataBytes = addTextureCopyBytes(
            addTextureCopyBytes(
                addTextureCopyBytes(layout.offset, multiplyTextureCopyBytes(size.depth != 0u ? size.depth - 1u : 0u, bytesPerImage)),
                multiplyTextureCopyBytes(blocksY != 0u ? blocksY - 1u : 0u, bytesPerRow)),
            tightRowBytes);
        const uint64_t stagingBytesPerRow = alignTextureCopyBytes(bytesPerRow, stagingRowAlignment);
        if (stagingBytesPerRow > uint64_t(0xffffffffu))
        {
            throw std::length_error("Texture transfer row stride exceeds the layout encoding range.");
        }
        const uint64_t stagingBytesPerImage = multiplyTextureCopyBytes(rowsPerImage, stagingBytesPerRow);
        const uint64_t stagingBytes = multiplyTextureCopyBytes(stagingBytesPerImage, size.depth);

        return {
            .blocksX = blocksX,
            .blocksY = blocksY,
            .tightRowBytes = tightRowBytes,
            .bytesPerRow = bytesPerRow,
            .rowsPerImage = rowsPerImage,
            .bytesPerImage = bytesPerImage,
            .minimumDataBytes = minimumDataBytes,
            .stagingBytesPerRow = stagingBytesPerRow,
            .stagingBytesPerImage = stagingBytesPerImage,
            .stagingBytes = stagingBytes,
        };
    }
} // namespace GVM::RHI::Private
