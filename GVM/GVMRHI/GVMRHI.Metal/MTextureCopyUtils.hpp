#pragma once

#include <GVMRHI/GVMRHI.hpp>
#include <GVMRHI/Private/GEnumUtils.hpp>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace GVM::RHI::Metal::Detail
{
    constexpr uint32_t TextureDataLayoutOffsetAlignment = 4u;
    constexpr uint32_t MetalTextureBytesPerRowAlignment = 256u;

    struct TextureCopyFootprint
    {
        uint32_t blocksX = 0;
        uint32_t blocksY = 0;
        uint64_t tightRowBytes = 0;
        uint64_t logicalBytesPerRow = 0;
        uint64_t logicalRowsPerImage = 0;
        uint64_t logicalBytesPerImage = 0;
        uint64_t metalBytesPerRow = 0;
        uint64_t metalRowsPerImage = 0;
        uint64_t metalBytesPerImage = 0;
        uint64_t requiredBytes = 0;
    };

    inline uint64_t alignUp(uint64_t value, uint64_t alignment)
    {
        if (alignment == 0u)
        {
            return value;
        }
        const uint64_t remainder = value % alignment;
        return remainder == 0u ? value : (value + (alignment - remainder));
    }

    inline TextureCopyFootprint buildTextureCopyFootprint(
        TextureFormat format,
        const TextureDataLayout &layout,
        const Extent3D &size,
        bool alignBytesPerRowToMetal)
    {
        const Private::BlockInfo blockInfo = Private::getTextureBlockInfo(format);
        const uint32_t blocksX = static_cast<uint32_t>((size.width + blockInfo.width - 1u) / blockInfo.width);
        const uint32_t blocksY = static_cast<uint32_t>((size.height + blockInfo.height - 1u) / blockInfo.height);
        const uint64_t tightRowBytes = static_cast<uint64_t>(blocksX) * blockInfo.bytes;
        const uint64_t logicalBytesPerRow = layout.bytesPerRow != 0u ? layout.bytesPerRow : tightRowBytes;
        const uint64_t logicalRowsPerImage = layout.rowsPerImage != 0u ? layout.rowsPerImage : blocksY;
        const uint64_t logicalBytesPerImage = logicalRowsPerImage * logicalBytesPerRow;
        const uint64_t metalBytesPerRow = alignBytesPerRowToMetal ? alignUp(logicalBytesPerRow, MetalTextureBytesPerRowAlignment) : logicalBytesPerRow;
        const uint64_t metalRowsPerImage = logicalRowsPerImage;
        const uint64_t metalBytesPerImage = metalRowsPerImage * metalBytesPerRow;

        return {
            .blocksX = blocksX,
            .blocksY = blocksY,
            .tightRowBytes = tightRowBytes,
            .logicalBytesPerRow = logicalBytesPerRow,
            .logicalRowsPerImage = logicalRowsPerImage,
            .logicalBytesPerImage = logicalBytesPerImage,
            .metalBytesPerRow = metalBytesPerRow,
            .metalRowsPerImage = metalRowsPerImage,
            .metalBytesPerImage = metalBytesPerImage,
            .requiredBytes = metalBytesPerImage * size.depth,
        };
    }

    inline void validateTextureDataRequest(
        const char *apiName,
        TextureFormat format,
        const TextureDataLayout &layout,
        const Extent3D &size,
        uint64_t dataSize)
    {
        if ((layout.offset % TextureDataLayoutOffsetAlignment) != 0u)
        {
            throw std::invalid_argument(std::string(apiName) + " layout.offset must be a multiple of 4 bytes.");
        }

        const TextureCopyFootprint footprint = buildTextureCopyFootprint(format, layout, size, false);
        if (footprint.logicalBytesPerRow < footprint.tightRowBytes)
        {
            throw std::invalid_argument(std::string(apiName) + " bytesPerRow is smaller than the tightly packed row size.");
        }
        if (footprint.logicalRowsPerImage < footprint.blocksY)
        {
            throw std::invalid_argument(std::string(apiName) + " rowsPerImage is smaller than the texture block row count.");
        }
        if (layout.offset + footprint.logicalBytesPerImage * size.depth > dataSize)
        {
            throw std::invalid_argument(std::string(apiName) + " dataSize is smaller than the requested texture region.");
        }
    }

    inline void copyTextureRows(
        const uint8_t *sourceBytes,
        uint8_t *destinationBytes,
        uint64_t tightRowBytes,
        uint64_t sourceBytesPerRow,
        uint64_t sourceBytesPerImage,
        uint64_t destinationBytesPerRow,
        uint64_t destinationBytesPerImage,
        uint32_t rowCount,
        uint32_t depth)
    {
        for (uint32_t slice = 0; slice < depth; ++slice)
        {
            const uint8_t *sliceSource = sourceBytes + (static_cast<uint64_t>(slice) * sourceBytesPerImage);
            uint8_t *sliceDestination = destinationBytes + (static_cast<uint64_t>(slice) * destinationBytesPerImage);
            for (uint32_t row = 0; row < rowCount; ++row)
            {
                std::memcpy(
                    sliceDestination + (static_cast<uint64_t>(row) * destinationBytesPerRow),
                    sliceSource + (static_cast<uint64_t>(row) * sourceBytesPerRow),
                    tightRowBytes);
            }
        }
    }
} // namespace GVM::RHI::Metal::Detail
