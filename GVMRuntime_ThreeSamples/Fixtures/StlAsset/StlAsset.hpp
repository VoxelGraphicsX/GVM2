#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores one non-indexed STL vertex using Three STLLoader position, normal, and optional linear color semantics. */
    struct StlAssetVertex final
    {
        glm::vec4 position{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 normal{0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 color{1.0f};
    };

    /** Stores one deterministic ASCII or binary STL decode result for sample-side geometry preparation. */
    struct StlAsset final
    {
        eastl::vector<StlAssetVertex> vertices;
        uint32_t facetCount = 0u;
        bool binary = false;
        bool hasColors = false;
        float alpha = 1.0f;
        eastl::string sha256;
    };

    /** Parses one frozen STL asset with the same ASCII, binary, and Magics facet-color rules as Three r185 STLLoader. */
    StlAsset loadStlAsset(const std::filesystem::path &assetPath);
} // namespace GVM::ThreeSamples
