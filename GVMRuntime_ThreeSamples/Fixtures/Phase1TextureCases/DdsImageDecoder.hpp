#pragma once

#include "RgbaImageData.hpp"

#include <EASTL/vector.h>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores one decoded DDS face with all explicitly available mip levels. */
    struct DdsRgba8Face final
    {
        eastl::vector<RgbaImageData> mipLevels;
    };

    /** Stores the frozen DDS container metadata and decoded RGBA8 faces. */
    struct DdsRgba8Texture final
    {
        uint32_t fourCc = 0u;
        uint32_t dxgiFormat = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t authoredMipCount = 0u;
        bool cube = false;
        bool floatingPoint = false;
        eastl::vector<DdsRgba8Face> faces;
    };

    /** Decodes the exact DDS variants used by the frozen r185 loader example. */
    [[nodiscard]] DdsRgba8Texture decodeDdsRgba8(
        const std::filesystem::path &assetPath);
} // namespace GVM::ThreeSamples
