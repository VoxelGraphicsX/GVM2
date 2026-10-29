#pragma once

#include <EASTL/vector.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Stores one indexed ParametricGeometry-compatible triangle mesh. */
    struct ParametricMesh
    {
        eastl::vector<glm::vec3> positions;
        eastl::vector<glm::vec3> normals;
        eastl::vector<glm::vec2> textureCoordinates;
        eastl::vector<uint32_t> indices;
    };

    /** Evaluates one normalized parametric surface point using immutable caller state. */
    using ParametricSurfaceEvaluator =
        glm::dvec3 (*)(double u, double v, const void *context);

    /** Builds Three r185 ParametricGeometry vertices, finite-difference normals, UVs, and indices. */
    ParametricMesh buildParametricMesh(
        ParametricSurfaceEvaluator evaluator,
        const void *context,
        uint32_t slices,
        uint32_t stacks);
}
