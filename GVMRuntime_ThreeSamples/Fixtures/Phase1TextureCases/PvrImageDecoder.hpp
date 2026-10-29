#pragma once

#include "GifImageDecoder.hpp"

#include <EASTL/vector.h>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores every decoded face and mip from one PowerVR v2 or v3 texture. */
    struct PvrRgba8Texture final
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t mipCount = 0u;
        uint32_t faceCount = 0u;
        uint32_t bitsPerPixel = 0u;
        bool hasAlpha = false;
        eastl::vector<RgbaImageData> faceMipImages;
    };

    /** Decodes one frozen PVRTC1 PVR v2/v3 asset into straight RGBA8 faces and mips. */
    PvrRgba8Texture decodePvrRgba8(const std::filesystem::path &assetPath);
} // namespace GVM::ThreeSamples
