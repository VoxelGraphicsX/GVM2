#ifndef GVM_THREE_COMPAT_INSTANCE_SAMPLE_GEOMETRY_HPP
#define GVM_THREE_COMPAT_INSTANCE_SAMPLE_GEOMETRY_HPP

#include "SampleAssetDecoders.hpp"

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Stores one indexed triangle mesh with generated smooth vertex normals. */
    struct InstanceSampleMesh
    {
        eastl::vector<glm::vec3> positions;
        eastl::vector<glm::vec3> normals;
        eastl::vector<uint32_t> indices;
    };

    /** Stores one deterministic instance transform and its stable ordinal. */
    struct InstanceSampleTransform
    {
        glm::mat4 matrix = glm::mat4(1.0f);
        uint32_t ordinal = 0u;
    };

    /** Converts decoded BufferGeometry data into an indexed mesh and recomputes r185-style normals. */
    InstanceSampleMesh buildInstanceSampleMesh(const DecodedBufferGeometry &geometry);

    /** Builds seeded performance transforms while reproducing optional per-object UUID random draws. */
    eastl::vector<InstanceSampleTransform> buildInstancingPerformanceTransforms(
        uint32_t count,
        uint32_t seed,
        uint32_t preTransformRandomDrawCount,
        uint32_t postTransformRandomDrawCount);

    /** Builds the animated cubic grid transforms used by webgpu_instance_mesh. */
    eastl::vector<InstanceSampleTransform> buildWebgpuInstanceMeshTransforms(
        uint32_t amount,
        uint32_t activeCount,
        double timeSeconds);

    /** Generates the exact recursive r185 Hilbert3D control points. */
    eastl::vector<glm::vec3> buildHilbert3DControlPoints(
        const glm::vec3 &center,
        float size,
        int32_t iterations);

    /** Samples the open centripetal r185 Catmull-Rom curve at uniformly spaced parameters. */
    eastl::vector<glm::vec3> sampleCentripetalCatmullRom(
        const eastl::vector<glm::vec3> &controlPoints,
        uint32_t sampleCount);

    /** Converts one linear HSL color to the r185 linear RGB result. */
    glm::vec3 convertLinearHslToRgb(float hue, float saturation, float lightness);
}

#endif
