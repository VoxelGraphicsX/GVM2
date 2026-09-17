#pragma once

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Owns one tightly packed, top-down RGBA8 image used by CPU asset and canvas preparation. */
    struct RgbaImageData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        eastl::vector<uint8_t> pixels;
    };
} // namespace GVM::ThreeSamples
