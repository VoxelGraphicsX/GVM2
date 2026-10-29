#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores one exact r185 geometry attribute set from the generated sample-private bundle. */
    struct WebglGeometriesBundleMesh
    {
        eastl::string name;
        eastl::vector<glm::vec3> positions;
        eastl::vector<glm::vec3> normals;
        eastl::vector<glm::vec2> textureCoordinates;
        eastl::vector<uint32_t> indices;
    };

    /** Decodes the immutable little-endian geometry bundle generated from pinned Three.js r185 sources. */
    [[nodiscard]] eastl::vector<WebglGeometriesBundleMesh>
    decodeWebglGeometriesBundle(
        const std::filesystem::path &bundlePath);
} // namespace GVM::ThreeSamples
