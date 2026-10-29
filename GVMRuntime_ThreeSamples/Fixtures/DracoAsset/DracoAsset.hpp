#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores the position-only Draco mesh and Three-compatible generated normals. */
    struct DracoMeshAsset final
    {
        eastl::string sourceSha256;
        eastl::vector<glm::vec3> positions;
        eastl::vector<glm::vec3> normals;
        eastl::vector<uint32_t> indices;
    };

    /** Decodes one locked triangular Draco mesh and generates indexed vertex normals. */
    DracoMeshAsset loadDracoMeshAsset(const std::filesystem::path &path);
} // namespace GVM::ThreeSamples
