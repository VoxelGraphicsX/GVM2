#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <cstdint>

namespace GVM::RHI::Private
{
    /** Byte layout of one buffer<->texture transfer; bytesPerRow is per texel-block row and rowsPerImage counts texel-block rows. */
    struct TextureCopyFootprint
    {
        uint32_t blocksX = 0;
        uint32_t blocksY = 0;
        uint64_t tightRowBytes = 0;
        uint64_t bytesPerRow = 0;
        uint64_t rowsPerImage = 0;
        uint64_t bytesPerImage = 0;
        uint64_t minimumDataBytes = 0;
        uint64_t stagingBytesPerRow = 0;
        uint64_t stagingBytesPerImage = 0;
        uint64_t stagingBytes = 0;
    };

    /** Computes and validates one transfer footprint; stagingRowAlignment is the backend's staging row alignment, 1 when none. */
    TextureCopyFootprint computeTextureCopyFootprint(
        const char *apiName,
        TextureFormat format,
        const TextureDataLayout &layout,
        const Extent3D &size,
        uint64_t stagingRowAlignment);
} // namespace GVM::RHI::Private
