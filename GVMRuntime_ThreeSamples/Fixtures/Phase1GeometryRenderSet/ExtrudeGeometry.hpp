#pragma once

#include <EASTL/vector.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Stores one non-indexed r185 extrusion vertex and its material group slot. */
    struct ExtrudeCpuVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 materialSlotAndReserved;
    };

    /** Stores one complete non-indexed extrusion result and source group counts. */
    struct ExtrudeCpuGeometry
    {
        eastl::vector<ExtrudeCpuVertex> vertices;
        uint32_t lidVertexCount = 0u;
        uint32_t sideVertexCount = 0u;
    };

    /** Describes one Catmull-Rom path extrusion without public renderer coupling. */
    struct PathExtrudeParameters
    {
        eastl::vector<glm::dvec3> controlPoints;
        eastl::vector<glm::dvec2> shape;
        uint32_t steps = 1u;
        bool closed = false;
        bool uniformCatmullRom = false;
        double tension = 0.5;
        uint32_t materialSlot = 0u;
    };

    /** Describes one straight-depth extrusion with optional one-segment bevel. */
    struct DepthExtrudeParameters
    {
        eastl::vector<glm::dvec2> shape;
        double depth = 1.0;
        uint32_t steps = 1u;
        bool bevelEnabled = false;
        double bevelThickness = 0.2;
        double bevelSize = 0.1;
        uint32_t bevelSegments = 0u;
        uint32_t lidMaterialSlot = 0u;
        uint32_t sideMaterialSlot = 1u;
    };

    /** Builds the exact r185 non-indexed path extrusion for one simple contour. */
    ExtrudeCpuGeometry buildPathExtrudeGeometry(
        const PathExtrudeParameters &parameters);

    /** Builds the exact r185 non-indexed depth and bevel extrusion for one contour. */
    ExtrudeCpuGeometry buildDepthExtrudeGeometry(
        const DepthExtrudeParameters &parameters);
} // namespace GVM::ThreeSamples
