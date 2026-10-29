#pragma once

#include <EASTL/vector.h>

#include <glm/vec3.hpp>

#include <cstdint>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Stores one Cartesian NURBS control point and its rational weight. */
    struct NurbsControlPoint
    {
        glm::dvec3 position{0.0};
        double weight = 1.0;
    };

    /** Stores the degree and immutable knot vector for one NURBS parameter axis. */
    struct NurbsAxis
    {
        uint32_t degree = 0u;
        eastl::vector<double> knots;
    };

    /** Finds the active knot span using the Three r185 NURBSUtils boundary rules. */
    uint32_t findNurbsSpan(
        const NurbsAxis &axis,
        double parameter);

    /** Evaluates the nonzero B-spline basis functions for one knot span. */
    eastl::vector<double> evaluateNurbsBasis(
        const NurbsAxis &axis,
        uint32_t span,
        double parameter);

    /** Evaluates one rational B-spline curve point in Cartesian space. */
    glm::dvec3 evaluateNurbsCurve(
        const NurbsAxis &axis,
        const eastl::vector<NurbsControlPoint> &controlPoints,
        double normalizedParameter);

    /** Evaluates one rational tensor-product surface from U-major control points. */
    glm::dvec3 evaluateNurbsSurface(
        const NurbsAxis &uAxis,
        const NurbsAxis &vAxis,
        const eastl::vector<NurbsControlPoint> &controlPoints,
        uint32_t vControlPointCount,
        double normalizedU,
        double normalizedV);

    /** Evaluates one rational tensor-product volume from U/V/W-major control points. */
    glm::dvec3 evaluateNurbsVolume(
        const NurbsAxis &uAxis,
        const NurbsAxis &vAxis,
        const NurbsAxis &wAxis,
        const eastl::vector<NurbsControlPoint> &controlPoints,
        uint32_t vControlPointCount,
        uint32_t wControlPointCount,
        double normalizedU,
        double normalizedV,
        double normalizedW);
}
