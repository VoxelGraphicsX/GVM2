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
        const char *sha256;
        uint32_t facetCount;
        bool binary;
        bool hasColors;
    };

    /** Verifies every STL asset needed by webgl_loader_stl against its canonical topology and hash. */
    void verifyStlAssets()
    {
        constexpr std::array<StlExpectation, 4u> Expectations = {{
            {"models/stl/ascii/slotted_disk.stl", "5c0d95ca55352ccf5cca12197a5f9fa17eb2e905d1bb3a45c8ba51c9a22699a4", 288u, false, false},
            {"models/stl/binary/pr2_head_pan.stl", "c4ebcc17156642a8e4df8564a09f730b54db236e783320d71af3e51de17e8315", 1000u, true, false},
            {"models/stl/binary/pr2_head_tilt.stl", "4c98307430589fbb86f0310b25b1896bbb0ac4c4f5b97d9dc4f03d3250003533", 1052u, true, false},
            {"models/stl/binary/colored.stl", "0012b62d4bb485f902a972192eef29d6ce4b97a9deb2de67a02d30a5b738f461", 2156u, true, true},
        }};
        const std::filesystem::path root(GVM_THREE_STL_ASSET_ROOT);
        for (const StlExpectation &expectation : Expectations)
        {
            const GVM::ThreeSamples::StlAsset asset =
                GVM::ThreeSamples::loadStlAsset(root / expectation.relativePath);
            if (asset.sha256 != expectation.sha256 ||
                asset.facetCount != expectation.facetCount ||
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
