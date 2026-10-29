#include "DdsImageDecoder.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    /** Describes one frozen DDS asset and its decoded surface topology. */
    struct DdsAssetExpectation final
    {
        const char *relativePath;
        uint32_t width;
        uint32_t height;
        uint32_t mipCount;
        uint32_t faceCount;
        bool floatingPoint;
    };

    /** Throws when one DDS decoder contract is not satisfied. */
    void requireDdsCondition(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    /** Validates every DDS asset consumed by the r185 loader example. */
    void testFrozenDdsAssets()
    {
        constexpr eastl::array<DdsAssetExpectation, 14u> Expectations = {{
            {"textures/compressed/disturb_dxt1_nomip.dds", 512u, 512u, 1u, 1u, false},
            {"textures/compressed/disturb_dxt1_mip.dds", 512u, 512u, 10u, 1u, false},
            {"textures/compressed/hepatica_dxt3_mip.dds", 256u, 256u, 9u, 1u, false},
            {"textures/compressed/explosion_dxt5_mip.dds", 256u, 256u, 9u, 1u, false},
            {"textures/compressed/disturb_argb_nomip.dds", 256u, 256u, 1u, 1u, false},
            {"textures/compressed/disturb_argb_mip.dds", 256u, 256u, 9u, 1u, false},
            {"textures/compressed/disturb_dx10_bc6h_signed_nomip.dds", 512u, 512u, 1u, 1u, true},
            {"textures/compressed/disturb_dx10_bc6h_signed_mip.dds", 512u, 512u, 10u, 1u, true},
            {"textures/compressed/disturb_dx10_bc6h_unsigned_nomip.dds", 512u, 512u, 1u, 1u, true},
            {"textures/compressed/disturb_dx10_bc6h_unsigned_mip.dds", 512u, 512u, 10u, 1u, true},
            {"textures/wave_normals_24bit_uncompressed.dds", 256u, 256u, 9u, 1u, false},
            {"textures/compressed/Mountains.dds", 512u, 512u, 1u, 6u, false},
            {"textures/compressed/Mountains_argb_mip.dds", 128u, 128u, 8u, 6u, false},
            {"textures/compressed/Mountains_argb_nomip.dds", 128u, 128u, 1u, 6u, false},
        }};
        const std::filesystem::path root(GVM_THREE_DDS_ASSET_ROOT);
        for (const DdsAssetExpectation &expectation : Expectations)
        {
            const GVM::ThreeSamples::DdsRgba8Texture texture =
                GVM::ThreeSamples::decodeDdsRgba8(
                    root / expectation.relativePath);
            requireDdsCondition(
                texture.width == expectation.width &&
                texture.height == expectation.height,
                "DDS base extent differs from the frozen contract.");
            requireDdsCondition(
                texture.authoredMipCount == expectation.mipCount,
                "DDS authored mip count differs from the frozen contract.");
            requireDdsCondition(
                texture.faces.size() == expectation.faceCount,
                "DDS face count differs from the frozen contract.");
            requireDdsCondition(
                texture.floatingPoint == expectation.floatingPoint,
                "DDS floating-point classification is incorrect.");
            for (const GVM::ThreeSamples::DdsRgba8Face &face : texture.faces)
            {
                requireDdsCondition(
                    face.mipLevels.size() == expectation.mipCount,
                    "DDS decoded mip count differs from the frozen contract.");
                uint32_t width = expectation.width;
                uint32_t height = expectation.height;
                for (const GVM::ThreeSamples::RgbaImageData &mip : face.mipLevels)
                {
                    requireDdsCondition(
                        mip.width == width && mip.height == height,
                        "DDS decoded mip extent is incorrect.");
                    requireDdsCondition(
                        mip.pixels.size() == size_t(width) * height * 4u,
                        "DDS decoded mip is not tightly packed RGBA8.");
                    width = eastl::max(1u, width / 2u);
                    height = eastl::max(1u, height / 2u);
                }
            }
        }
    }
} // namespace

/** Executes the sample-private DDS decoder regression suite. */
int main()
{
    try
    {
        testFrozenDdsAssets();
        std::cout << "DDS decoder tests passed for all fourteen frozen r185 assets.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "DDS decoder test failed: " << error.what() << '\n';
        return 1;
    }
}
