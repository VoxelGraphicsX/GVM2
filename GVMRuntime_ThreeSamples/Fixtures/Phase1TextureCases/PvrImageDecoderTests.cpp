#include "PvrImageDecoder.hpp"

#include <EASTL/array.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    /** Describes one frozen r185 PVR asset and its decoded surface layout. */
    struct PvrAssetExpectation final
    {
        const char *fileName;
        uint32_t width;
        uint32_t height;
        uint32_t mipCount;
        uint32_t faceCount;
        uint32_t bitsPerPixel;
        bool hasAlpha;
    };

    /** Throws when one PVR decoder contract is not satisfied. */
    void requirePvrCondition(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    /** Validates all 2D and cube PVR variants used by the frozen example. */
    void testFrozenPvrAssets()
    {
        constexpr eastl::array<PvrAssetExpectation, 8u> Expectations = {{
            {"disturb_4bpp_rgb.pvr", 256u, 256u, 1u, 1u, 4u, false},
            {"disturb_4bpp_rgb_mips.pvr", 256u, 256u, 9u, 1u, 4u, false},
            {"disturb_2bpp_rgb.pvr", 256u, 256u, 1u, 1u, 2u, false},
            {"disturb_4bpp_rgb_v3.pvr", 256u, 256u, 1u, 1u, 4u, false},
            {"flare_4bpp_rgba.pvr", 256u, 256u, 1u, 1u, 4u, true},
            {"flare_2bpp_rgba.pvr", 256u, 256u, 1u, 1u, 2u, true},
            {"park3_cube_nomip_4bpp_rgb.pvr", 256u, 256u, 1u, 6u, 4u, true},
            {"park3_cube_mip_2bpp_rgb_v3.pvr", 256u, 256u, 9u, 6u, 2u, false},
        }};
        const std::filesystem::path root =
            std::filesystem::path(GVM_THREE_PVR_ASSET_ROOT) /
            "textures" / "compressed";
        for (const PvrAssetExpectation &expectation : Expectations)
        {
            const GVM::ThreeSamples::PvrRgba8Texture texture =
                GVM::ThreeSamples::decodePvrRgba8(root / expectation.fileName);
            requirePvrCondition(
                texture.width == expectation.width &&
                    texture.height == expectation.height,
                "PVR base extent does not match the frozen asset contract.");
            requirePvrCondition(
                texture.mipCount == expectation.mipCount &&
                    texture.faceCount == expectation.faceCount,
                "PVR mip or face count does not match the frozen asset contract.");
            requirePvrCondition(
                texture.bitsPerPixel == expectation.bitsPerPixel &&
                    texture.hasAlpha == expectation.hasAlpha,
                "PVR compression metadata does not match the frozen asset contract.");
            requirePvrCondition(
                texture.faceMipImages.size() ==
                    size_t(expectation.faceCount) * expectation.mipCount,
                "PVR decoded surface count is inconsistent.");
            for (uint32_t face = 0u; face < expectation.faceCount; ++face)
            {
                uint32_t width = expectation.width;
                uint32_t height = expectation.height;
                for (uint32_t mip = 0u; mip < expectation.mipCount; ++mip)
                {
                    const GVM::ThreeSamples::RgbaImageData &image =
                        texture.faceMipImages[size_t(face) * expectation.mipCount + mip];
                    requirePvrCondition(
                        image.width == width && image.height == height,
                        "PVR decoded mip extent is inconsistent.");
                    requirePvrCondition(
                        image.pixels.size() == size_t(width) * height * 4u,
                        "PVR decoded mip is not tightly packed RGBA8.");
                    width = eastl::max(1u, width / 2u);
                    height = eastl::max(1u, height / 2u);
                }
            }
        }
    }
} // namespace

/** Executes the sample-private PVR decoder regression suite. */
int main()
{
    try
    {
        testFrozenPvrAssets();
        std::cout << "PVR decoder tests passed for all eight frozen r185 assets.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "PVR decoder test failed: " << error.what() << '\n';
        return 1;
    }
}
