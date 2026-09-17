#include "CurveExtras.hpp"

#include <cmath>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        constexpr double TwoPi =
            6.283185307179586476925286766559;

        /** Returns the explicit curve scale or a fixed fallback value. */
        double resolveCurveScale(
            const void *context,
            double fallback)
        {
            return context == nullptr
                ? fallback
                : static_cast<
                      const CurveScaleContext *>(
                      context)
                      ->scale;
        }
    }

    glm::dvec3 evaluateGrannyKnot(
        double parameter,
        const void *context)
    {
        const double angle =
            TwoPi * parameter;
        const double scale =
            resolveCurveScale(context, 20.0);
        return {
            scale *
                (-0.22 * std::cos(angle) -
                 1.28 * std::sin(angle) -
                 0.44 * std::cos(3.0 * angle) -
                 0.78 * std::sin(3.0 * angle)),
            scale *
                (-0.1 * std::cos(2.0 * angle) -
                 0.27 * std::sin(2.0 * angle) +
                 0.38 * std::cos(4.0 * angle) +
                 0.46 * std::sin(4.0 * angle)),
            scale *
                (0.7 * std::cos(3.0 * angle) -
                 0.4 * std::sin(3.0 * angle)),
        };
    }

    glm::dvec3 evaluateCurveExtrasTorusKnot(
        double parameter,
        const void *context)
    {
        const double angle =
            TwoPi * parameter;
        const double scale =
            resolveCurveScale(context, 10.0);
        return {
            (2.0 + std::cos(4.0 * angle)) *
                std::cos(3.0 * angle) *
                scale,
            (2.0 + std::cos(4.0 * angle)) *
                std::sin(3.0 * angle) *
                scale,
            std::sin(4.0 * angle) *
                scale,
        };
    }
} // namespace GVM::ThreeSamples::ThreeCompat
