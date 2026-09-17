#pragma once

#include "Fixtures/Phase1TextureCases/TexturedBoxSampleData.hpp"

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one uint4 component with a stable 16-byte UGLC ABI. */
    struct alignas(16) BuffergeometryClosureHostUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    /** Mirrors the interleaved example's per-entity root matrix and material selector. */
    struct alignas(16) BuffergeometryInterleavedHostObjectData
    {
        glm::mat4 rootModelViewProjection;
        BuffergeometryClosureHostUint4 materialAndFlags;
    };

    /** Mirrors one Float32 instance matrix stored as four shader columns. */
    struct alignas(16) BuffergeometryInterleavedHostInstanceData
    {
        TexturedBoxHostFloat4 matrixColumn0;
        TexturedBoxHostFloat4 matrixColumn1;
        TexturedBoxHostFloat4 matrixColumn2;
        TexturedBoxHostFloat4 matrixColumn3;
    };

    /** Mirrors the interleaved example's single MeshBasicMaterial base color. */
    struct alignas(16) BuffergeometryInterleavedHostMaterialData
    {
        TexturedBoxHostFloat4 baseColor;
    };
} // namespace GVM::ThreeSamples
