#include "StlAsset.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifndef GVM_THREE_STL_ASSET_ROOT
#error GVM_THREE_STL_ASSET_ROOT must identify the frozen Three r185 examples directory.
#endif

namespace
{
    /** Describes one immutable STL asset expectation from the r185 loader example. */
    struct StlExpectation final
    {
        const char *relativePath;
        uint32_t facetCount;
        bool binary;
        bool hasColors;
    };

    /** Verifies every STL asset needed by webgl_loader_stl against its canonical topology and vertex packing. */
    void verifyStlAssets()
    {
        constexpr std::array<StlExpectation, 4u> Expectations = {{
            {"models/stl/ascii/slotted_disk.stl", 288u, false, false},
            {"models/stl/binary/pr2_head_pan.stl", 1000u, true, false},
            {"models/stl/binary/pr2_head_tilt.stl", 1052u, true, false},
            {"models/stl/binary/colored.stl", 2156u, true, true},
        }};
        const std::filesystem::path root(GVM_THREE_STL_ASSET_ROOT);
        for (const StlExpectation &expectation : Expectations)
        {
            const GVM::ThreeSamples::StlAsset asset =
                GVM::ThreeSamples::loadStlAsset(root / expectation.relativePath);
            if (asset.facetCount != expectation.facetCount ||
                asset.vertices.size() != size_t(expectation.facetCount) * 3u ||
                asset.binary != expectation.binary ||
                asset.hasColors != expectation.hasColors)
                throw std::runtime_error("STL asset evidence differs from the frozen r185 expectation.");
            for (const GVM::ThreeSamples::StlAssetVertex &vertex : asset.vertices)
            {
                if (vertex.position.w != 1.0f || vertex.normal.w != 0.0f ||
                    vertex.color.w != 1.0f)
                    throw std::runtime_error("STL vertex packing violates the sample ABI.");
            }
        }
    }
} // namespace

int main()
{
    try
    {
        verifyStlAssets();
        std::cout << "STL asset tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
