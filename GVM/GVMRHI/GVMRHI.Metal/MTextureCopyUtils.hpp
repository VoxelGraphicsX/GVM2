#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <cstdint>
#include <cstring>

namespace GVM::RHI::Metal::Detail
{
    constexpr uint32_t TextureDataLayoutOffsetAlignment = 4u;
    constexpr uint32_t MetalTextureBytesPerRowAlignment = 256u;

    /** Copies tightly packed texel-block rows between two differently strided transfer layouts. */
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
