#pragma once

#include <EASTL/vector.h>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Owns one top-down linear RGBA16Float image decoded from an immutable EXR asset. */
    struct RgbaHalfImageData
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        eastl::vector<uint16_t> pixels;
    };

    /** Decodes one OpenEXR image into unpremultiplied linear RGBA16Float texels. */
    [[nodiscard]] RgbaHalfImageData decodeExrRgba16Float(
        const std::filesystem::path &assetPath);
}
