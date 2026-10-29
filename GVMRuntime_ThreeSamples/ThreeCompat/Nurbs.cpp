#include "Nurbs.hpp"

#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        /** Returns the control-point count implied by one valid clamped axis. */
        uint32_t nurbsControlPointCount(const NurbsAxis &axis)
        {
            if (axis.knots.size() < static_cast<size_t>(axis.degree) + 2u)
            {
                throw std::invalid_argument(
                    "A NURBS axis must contain at least degree + 2 knots.");
            }
            const size_t count =
                axis.knots.size() - static_cast<size_t>(axis.degree) - 1u;
            if (count > UINT32_MAX)
            {
                throw std::overflow_error(
                    "A NURBS axis control-point count exceeds uint32.");
            }
            return static_cast<uint32_t>(count);
        }

        /** Maps a normalized parameter onto the complete knot interval. */
        double mapNurbsParameter(
            const NurbsAxis &axis,
            double normalizedParameter)
        {
            if (!std::isfinite(normalizedParameter))
            {
                throw std::invalid_argument(
                    "A normalized NURBS parameter must be finite.");
            }
            const double clamped =
                std::clamp(normalizedParameter, 0.0, 1.0);
            return axis.knots.front() +
                clamped *
                    (axis.knots.back() - axis.knots.front());
        }

        /** Returns one homogeneous control point after applying its weight. */
        glm::dvec4 makeHomogeneousPoint(
            const NurbsControlPoint &controlPoint)
        {
            return glm::dvec4(
                controlPoint.position * controlPoint.weight,
                controlPoint.weight);
        }

        /** Projects one nonzero homogeneous point into Cartesian space. */
        glm::dvec3 projectNurbsPoint(const glm::dvec4 &point)
        {
            if (point.w == 0.0)
            {
                throw std::runtime_error(
                    "A NURBS evaluation produced zero homogeneous weight.");
            }
            return glm::dvec3(point) / point.w;
        }
    }

    uint32_t findNurbsSpan(
        const NurbsAxis &axis,
        double parameter)
    {
        const uint32_t controlPointCount =
            nurbsControlPointCount(axis);
        const uint32_t n = controlPointCount;
        if (parameter >= axis.knots[n])
        {
            return n - 1u;
        }
        if (parameter <= axis.knots[axis.degree])
        {
            return axis.degree;
        }

        uint32_t low = axis.degree;
        uint32_t high = n;
        uint32_t middle = (low + high) / 2u;
        while (parameter < axis.knots[middle] ||
               parameter >= axis.knots[middle + 1u])
        {
            if (parameter < axis.knots[middle])
            {
                high = middle;
            }
            else
            {
                low = middle;
            }
            middle = (low + high) / 2u;
        }
        return middle;
    }

    eastl::vector<double> evaluateNurbsBasis(
        const NurbsAxis &axis,
        uint32_t span,
        double parameter)
    {
        nurbsControlPointCount(axis);
        if (span < axis.degree ||
            static_cast<size_t>(span) + axis.degree >=
                axis.knots.size())
        {
            throw std::out_of_range(
                "The NURBS span cannot address the requested degree.");
        }

        eastl::vector<double> basis(axis.degree + 1u, 0.0);
        eastl::vector<double> left(axis.degree + 1u, 0.0);
        eastl::vector<double> right(axis.degree + 1u, 0.0);
        basis[0u] = 1.0;
        for (uint32_t column = 1u;
             column <= axis.degree;
             ++column)
        {
            left[column] =
                parameter - axis.knots[span + 1u - column];
            right[column] =
                axis.knots[span + column] - parameter;
            double saved = 0.0;
            for (uint32_t row = 0u; row < column; ++row)
            {
                const double denominator =
                    right[row + 1u] +
                    left[column - row];
                if (denominator == 0.0)
                {
                    throw std::runtime_error(
                        "A NURBS basis denominator is zero.");
                }
                const double temporary =
                    basis[row] / denominator;
                basis[row] =
                    saved + right[row + 1u] * temporary;
                saved =
                    left[column - row] * temporary;
            }
            basis[column] = saved;
        }
        return basis;
    }

    glm::dvec3 evaluateNurbsCurve(
        const NurbsAxis &axis,
        const eastl::vector<NurbsControlPoint> &controlPoints,
        double normalizedParameter)
    {
        const uint32_t expectedCount =
            nurbsControlPointCount(axis);
        if (controlPoints.size() != expectedCount)
        {
            throw std::invalid_argument(
                "A NURBS curve control-point count does not match its axis.");
        }
        const double parameter =
            mapNurbsParameter(axis, normalizedParameter);
        const uint32_t span =
            findNurbsSpan(axis, parameter);
        const eastl::vector<double> basis =
            evaluateNurbsBasis(axis, span, parameter);
        glm::dvec4 result(0.0);
        for (uint32_t index = 0u;
             index <= axis.degree;
             ++index)
        {
            result +=
                makeHomogeneousPoint(
                    controlPoints[span - axis.degree + index]) *
                basis[index];
        }
        return projectNurbsPoint(result);
    }

    glm::dvec3 evaluateNurbsSurface(
        const NurbsAxis &uAxis,
        const NurbsAxis &vAxis,
        const eastl::vector<NurbsControlPoint> &controlPoints,
        uint32_t vControlPointCount,
        double normalizedU,
        double normalizedV)
    {
        const uint32_t uControlPointCount =
            nurbsControlPointCount(uAxis);
        const uint32_t expectedVCount =
            nurbsControlPointCount(vAxis);
        if (vControlPointCount != expectedVCount ||
            controlPoints.size() !=
                static_cast<size_t>(uControlPointCount) *
                    vControlPointCount)
        {
            throw std::invalid_argument(
                "A NURBS surface control grid does not match its axes.");
        }
        const double u =
            mapNurbsParameter(uAxis, normalizedU);
        const double v =
            mapNurbsParameter(vAxis, normalizedV);
        const uint32_t uSpan =
            findNurbsSpan(uAxis, u);
        const uint32_t vSpan =
            findNurbsSpan(vAxis, v);
        const eastl::vector<double> uBasis =
            evaluateNurbsBasis(uAxis, uSpan, u);
        const eastl::vector<double> vBasis =
            evaluateNurbsBasis(vAxis, vSpan, v);

        glm::dvec4 result(0.0);
        for (uint32_t vIndex = 0u;
             vIndex <= vAxis.degree;
             ++vIndex)
        {
            glm::dvec4 temporary(0.0);
            for (uint32_t uIndex = 0u;
                 uIndex <= uAxis.degree;
                 ++uIndex)
            {
                const uint32_t controlU =
                    uSpan - uAxis.degree + uIndex;
                const uint32_t controlV =
                    vSpan - vAxis.degree + vIndex;
                temporary +=
                    makeHomogeneousPoint(
                        controlPoints[
                            static_cast<size_t>(controlU) *
                                vControlPointCount +
                            controlV]) *
                    uBasis[uIndex];
            }
            result += temporary * vBasis[vIndex];
        }
        return projectNurbsPoint(result);
    }

    glm::dvec3 evaluateNurbsVolume(
        const NurbsAxis &uAxis,
        const NurbsAxis &vAxis,
        const NurbsAxis &wAxis,
        const eastl::vector<NurbsControlPoint> &controlPoints,
        uint32_t vControlPointCount,
        uint32_t wControlPointCount,
        double normalizedU,
        double normalizedV,
        double normalizedW)
    {
        const uint32_t uControlPointCount =
            nurbsControlPointCount(uAxis);
        const uint32_t expectedVCount =
            nurbsControlPointCount(vAxis);
        const uint32_t expectedWCount =
            nurbsControlPointCount(wAxis);
        if (vControlPointCount != expectedVCount ||
            wControlPointCount != expectedWCount ||
            controlPoints.size() !=
                static_cast<size_t>(uControlPointCount) *
                    vControlPointCount *
                    wControlPointCount)
        {
            throw std::invalid_argument(
                "A NURBS volume control grid does not match its axes.");
        }

        const double u =
            mapNurbsParameter(uAxis, normalizedU);
        const double v =
            mapNurbsParameter(vAxis, normalizedV);
        const double w =
            mapNurbsParameter(wAxis, normalizedW);
        const uint32_t uSpan =
            findNurbsSpan(uAxis, u);
        const uint32_t vSpan =
            findNurbsSpan(vAxis, v);
        const uint32_t wSpan =
            findNurbsSpan(wAxis, w);
        const eastl::vector<double> uBasis =
            evaluateNurbsBasis(uAxis, uSpan, u);
        const eastl::vector<double> vBasis =
            evaluateNurbsBasis(vAxis, vSpan, v);
        const eastl::vector<double> wBasis =
            evaluateNurbsBasis(wAxis, wSpan, w);

        glm::dvec4 result(0.0);
        for (uint32_t wIndex = 0u;
             wIndex <= wAxis.degree;
             ++wIndex)
        {
            for (uint32_t vIndex = 0u;
                 vIndex <= vAxis.degree;
                 ++vIndex)
            {
                glm::dvec4 temporary(0.0);
                for (uint32_t uIndex = 0u;
                     uIndex <= uAxis.degree;
                     ++uIndex)
                {
                    const uint32_t controlU =
                        uSpan - uAxis.degree + uIndex;
                    const uint32_t controlV =
                        vSpan - vAxis.degree + vIndex;
                    const uint32_t controlW =
                        wSpan - wAxis.degree + wIndex;
                    temporary +=
                        makeHomogeneousPoint(
                            controlPoints[
                                (static_cast<size_t>(controlU) *
                                     vControlPointCount +
                                 controlV) *
                                    wControlPointCount +
                                controlW]) *
                        uBasis[uIndex];
                }
                result +=
                    temporary *
                    vBasis[vIndex] *
                    wBasis[wIndex];
            }
        }
        return projectNurbsPoint(result);
    }
}
