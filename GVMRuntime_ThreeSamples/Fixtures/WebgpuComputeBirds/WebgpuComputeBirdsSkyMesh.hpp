#pragma once

#include <EASTL/vector.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>

namespace GVM::ThreeSamples
{
    /** Stores the exact r185 detail-six sky geometry used by webgpu_compute_birds. */
    struct WebgpuComputeBirdsSkyMesh
    {
        eastl::vector<glm::vec3> positions;
        eastl::vector<uint32_t> indices;
    };

    /** Decodes the immutable sample-private sky mesh generated from Three.js r185. */
    [[nodiscard]] WebgpuComputeBirdsSkyMesh
    decodeWebgpuComputeBirdsSkyMesh(
        const std::filesystem::path &path);
} // namespace GVM::ThreeSamples
