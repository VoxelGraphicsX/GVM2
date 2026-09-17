#pragma once

#include "RgbaImageData.hpp"

#include <EASTL/vector.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Decodes one bounded BC1 surface into top-down RGBA8 texels. */
    [[nodiscard]] RgbaImageData decodeBc1Rgba8(
        const uint8_t *blocks,
        size_t byteCount,
        uint32_t width,
        uint32_t height,
        bool forceFourColors = false);

    /** Decodes one bounded BC3 surface into top-down straight RGBA8 texels. */
    [[nodiscard]] RgbaImageData decodeBc3Rgba8(
        const uint8_t *blocks,
        size_t byteCount,
        uint32_t width,
        uint32_t height);

    /** Stores one KTX1 texture decoded to explicit top-down RGBA8 mip levels. */
    struct Ktx1Rgba8Texture final
    {
        uint32_t internalFormat = 0u;
        bool colorData = true;
        eastl::vector<RgbaImageData> mipLevels;
    };

    /** Decodes the frozen KTX1 formats used by the r185 compressed-texture example. */
    [[nodiscard]] Ktx1Rgba8Texture decodeKtx1Rgba8(
        const std::filesystem::path &assetPath);
} // namespace GVM::ThreeSamples
