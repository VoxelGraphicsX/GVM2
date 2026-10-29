#include "Ktx1ImageDecoder.hpp"

#include <EASTL/array.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    /** Describes one frozen r185 KTX1 asset and its decoded layout. */
    struct Ktx1AssetExpectation final
    {
        const char *fileName;
        uint32_t width;
        uint32_t height;
        uint32_t mipCount;
    };

    /** Throws when one decoder contract is not satisfied. */
    void requireCondition(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    /** Runs deterministic layout and byte-count checks over every r185 KTX1 asset. */
    void testFrozenKtx1Assets()
    {
        constexpr eastl::array<Ktx1AssetExpectation, 9u> Expectations = {{
            {"disturb_PVR2bpp.ktx", 512u, 512u, 10u},
            {"lensflare_PVR4bpp.ktx", 512u, 512u, 10u},
            {"disturb_BC1.ktx", 512u, 512u, 10u},
            {"lensflare_BC3.ktx", 512u, 512u, 10u},
            {"normal.bc5.ktx", 256u, 256u, 1u},
            {"disturb_ETC1.ktx", 512u, 512u, 10u},
            {"normal.eac_rg.ktx", 256u, 256u, 1u},
            {"disturb_ASTC4x4.ktx", 512u, 512u, 10u},
            {"lensflare_ASTC8x8.ktx", 512u, 512u, 10u},
        }};
        const std::filesystem::path root =
            std::filesystem::path(GVM_THREE_KTX1_ASSET_ROOT) /
            "textures" / "compressed";
        for (const Ktx1AssetExpectation &expectation : Expectations)
        {
            const GVM::ThreeSamples::Ktx1Rgba8Texture texture =
                GVM::ThreeSamples::decodeKtx1Rgba8(root / expectation.fileName);
            requireCondition(
                texture.mipLevels.size() == expectation.mipCount,
                "KTX1 mip count does not match the frozen asset contract.");
            uint32_t width = expectation.width;
            uint32_t height = expectation.height;
            for (const GVM::ThreeSamples::RgbaImageData &mip : texture.mipLevels)
            {
                requireCondition(
                    mip.width == width && mip.height == height,
                    "KTX1 decoded mip extent does not match the frozen asset contract.");
                requireCondition(
                    mip.pixels.size() == size_t(width) * height * 4u,
                    "KTX1 decoded mip does not contain tightly packed RGBA8 pixels.");
                width = eastl::max(1u, width / 2u);
                height = eastl::max(1u, height / 2u);
            }
        }
    }
} // namespace

/** Executes the sample-private KTX1 decoder regression suite. */
int main()
{
    try
    {
        testFrozenKtx1Assets();
        std::cout << "KTX1 decoder tests passed for all nine frozen r185 assets.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "KTX1 decoder test failed: " << error.what() << '\n';
        return 1;
    }
}
