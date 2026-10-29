#pragma once

#include <glm/vec3.hpp>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Stores the scale applied by one r185 CurveExtras parametric curve. */
    struct CurveScaleContext
    {
        double scale = 1.0;
    };

    /** Evaluates the fixed r185 GrannyKnot curve used by the spline extrusion example. */
    [[nodiscard]] glm::dvec3 evaluateGrannyKnot(
        double parameter,
        const void *context);

    /** Evaluates the fixed p=3, q=4 r185 TorusKnot curve at the requested scale. */
    [[nodiscard]] glm::dvec3 evaluateCurveExtrasTorusKnot(
        double parameter,
        const void *context);
} // namespace GVM::ThreeSamples::ThreeCompat
