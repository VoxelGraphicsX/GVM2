#include "ExtrudeGeometry.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>

#include <glm/geometric.hpp>
#include <glm/gtx/norm.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;

        /** Stores one sampled Frenet frame using JavaScript-equivalent doubles. */
        struct ExtrudeFrenetFrame
        {
            glm::dvec3 tangent;
            glm::dvec3 normal;
            glm::dvec3 binormal;
        };

        /** Stores one triangle over the source contour vertex array. */
        struct ExtrudeTriangle
        {
            uint32_t a;
            uint32_t b;
            uint32_t c;
        };

        /** Evaluates one Hermite cubic polynomial. */
        double evaluateCubic(double x0,
                             double x1,
                             double tangent0,
                             double tangent1,
                             double t)
        {
            const double c0 = x0;
            const double c1 = tangent0;
            const double c2 = -3.0 * x0 + 3.0 * x1 -
                              2.0 * tangent0 - tangent1;
            const double c3 = 2.0 * x0 - 2.0 * x1 +
                              tangent0 + tangent1;
            return c0 + c1 * t + c2 * t * t + c3 * t * t * t;
        }

        /** Returns one Catmull-Rom control point with closed-curve wrapping. */
        glm::dvec3 catmullControlPoint(
            const eastl::vector<glm::dvec3> &points,
            int32_t index)
        {
            const int32_t count = static_cast<int32_t>(points.size());
            const int32_t wrapped = ((index % count) + count) % count;
            return points[static_cast<size_t>(wrapped)];
        }

        /** Evaluates the r185 CatmullRomCurve3 point function. */
        glm::dvec3 evaluateCatmullRomPoint(
            const eastl::vector<glm::dvec3> &points,
            bool closed,
            bool uniformCatmullRom,
            double tension,
            double t)
        {
            const int32_t count = static_cast<int32_t>(points.size());
            const double scaled = double(count - (closed ? 0 : 1)) * t;
            int32_t integerPoint = static_cast<int32_t>(std::floor(scaled));
            double weight = scaled - double(integerPoint);
            if (closed)
            {
                if (integerPoint <= 0)
                {
                    integerPoint +=
                        (static_cast<int32_t>(std::floor(
                             std::abs(double(integerPoint)) / double(count))) +
                         1) * count;
                }
            }
            else if (weight == 0.0 && integerPoint == count - 1)
            {
                integerPoint = count - 2;
                weight = 1.0;
            }
            const glm::dvec3 p1 =
                catmullControlPoint(points, integerPoint);
            const glm::dvec3 p2 =
                catmullControlPoint(points, integerPoint + 1);
            const glm::dvec3 p0 =
                closed || integerPoint > 0
                    ? catmullControlPoint(points, integerPoint - 1)
                    : points[0u] * 2.0 - points[1u];
            const glm::dvec3 p3 =
                closed || integerPoint + 2 < count
                    ? catmullControlPoint(points, integerPoint + 2)
                    : points.back() * 2.0 - points[points.size() - 2u];
            glm::dvec3 tangent0;
            glm::dvec3 tangent1;
            if (uniformCatmullRom)
            {
                tangent0 = tension * (p2 - p0);
                tangent1 = tension * (p3 - p1);
            }
            else
            {
                double dt0 = std::pow(glm::length2(p0 - p1), 0.25);
                double dt1 = std::pow(glm::length2(p1 - p2), 0.25);
                double dt2 = std::pow(glm::length2(p2 - p3), 0.25);
                if (dt1 < 1e-4)
                {
                    dt1 = 1.0;
                }
                if (dt0 < 1e-4)
                {
                    dt0 = dt1;
                }
                if (dt2 < 1e-4)
                {
                    dt2 = dt1;
                }
                tangent0 =
                    ((p1 - p0) / dt0 -
                     (p2 - p0) / (dt0 + dt1) +
                     (p2 - p1) / dt1) * dt1;
                tangent1 =
                    ((p2 - p1) / dt1 -
                     (p3 - p1) / (dt1 + dt2) +
                     (p3 - p2) / dt2) * dt1;
            }
            return {
                evaluateCubic(p1.x, p2.x, tangent0.x, tangent1.x, weight),
                evaluateCubic(p1.y, p2.y, tangent0.y, tangent1.y, weight),
                evaluateCubic(p1.z, p2.z, tangent0.z, tangent1.z, weight),
            };
        }

        /** Builds the r185 200-division cumulative Catmull-Rom length cache. */
        eastl::vector<double> buildCatmullRomLengths(
            const PathExtrudeParameters &parameters)
        {
            constexpr uint32_t ArcLengthDivisions = 200u;
            eastl::vector<double> lengths;
            lengths.reserve(ArcLengthDivisions + 1u);
            lengths.push_back(0.0);
            glm::dvec3 previous = evaluateCatmullRomPoint(
                parameters.controlPoints, parameters.closed,
                parameters.uniformCatmullRom, parameters.tension, 0.0);
            double sum = 0.0;
            for (uint32_t index = 1u; index <= ArcLengthDivisions; ++index)
            {
                const glm::dvec3 current = evaluateCatmullRomPoint(
                    parameters.controlPoints, parameters.closed,
                    parameters.uniformCatmullRom, parameters.tension,
                    double(index) / double(ArcLengthDivisions));
                sum += glm::length(current - previous);
                lengths.push_back(sum);
                previous = current;
            }
            return lengths;
        }

        /** Maps one unit arc fraction to the Catmull-Rom curve parameter. */
        double mapArcFractionToCurveParameter(
            double unitDistance,
            const eastl::vector<double> &lengths)
        {
            const double target = unitDistance * lengths.back();
            int32_t low = 0;
            int32_t high = static_cast<int32_t>(lengths.size()) - 1;
            int32_t index = 0;
            while (low <= high)
            {
                index = low + (high - low) / 2;
                const double comparison = lengths[index] - target;
                if (comparison < 0.0)
                {
                    low = index + 1;
                }
                else if (comparison > 0.0)
                {
                    high = index - 1;
                }
                else
                {
                    high = index;
                    break;
                }
            }
            index = high;
            if (lengths[index] == target)
            {
                return double(index) / double(lengths.size() - 1u);
            }
            const double segmentLength =
                lengths[index + 1] - lengths[index];
            const double fraction =
                (target - lengths[index]) / segmentLength;
            return (double(index) + fraction) /
                   double(lengths.size() - 1u);
        }

        /** Samples one Catmull-Rom point at an arc-length fraction. */
        glm::dvec3 sampleCatmullRomAt(
            const PathExtrudeParameters &parameters,
            const eastl::vector<double> &lengths,
            double unitDistance)
        {
            return evaluateCatmullRomPoint(
                parameters.controlPoints, parameters.closed,
                parameters.uniformCatmullRom, parameters.tension,
                mapArcFractionToCurveParameter(unitDistance, lengths));
        }

        /** Samples one normalized finite-difference tangent at an arc fraction. */
        glm::dvec3 sampleCatmullRomTangent(
            const PathExtrudeParameters &parameters,
            const eastl::vector<double> &lengths,
            double unitDistance)
        {
            constexpr double Delta = 0.0001;
            const double t =
                mapArcFractionToCurveParameter(unitDistance, lengths);
            const double t0 = eastl::max(0.0, t - Delta);
            const double t1 = eastl::min(1.0, t + Delta);
            return glm::normalize(
                evaluateCatmullRomPoint(
                    parameters.controlPoints, parameters.closed,
                    parameters.uniformCatmullRom, parameters.tension, t1) -
                evaluateCatmullRomPoint(
                    parameters.controlPoints, parameters.closed,
                    parameters.uniformCatmullRom, parameters.tension, t0));
        }

        /** Rotates one vector around a normalized axis using Rodrigues' formula. */
        glm::dvec3 rotateAroundAxis(const glm::dvec3 &value,
                                    const glm::dvec3 &axis,
                                    double angle)
        {
            return value * std::cos(angle) +
                   glm::cross(axis, value) * std::sin(angle) +
                   axis * glm::dot(axis, value) * (1.0 - std::cos(angle));
        }

        /** Reproduces Curve.computeFrenetFrames for one path sample count. */
        eastl::vector<ExtrudeFrenetFrame> buildFrenetFrames(
            const PathExtrudeParameters &parameters,
            const eastl::vector<double> &lengths)
        {
            eastl::vector<ExtrudeFrenetFrame> frames(
                parameters.steps + 1u);
            for (uint32_t index = 0u; index <= parameters.steps; ++index)
            {
                frames[index].tangent = sampleCatmullRomTangent(
                    parameters, lengths,
                    double(index) / double(parameters.steps));
            }
            glm::dvec3 referenceNormal(0.0);
            double minimum = std::numeric_limits<double>::max();
            const glm::dvec3 absoluteTangent =
                glm::abs(frames[0u].tangent);
            if (absoluteTangent.x <= minimum)
            {
                minimum = absoluteTangent.x;
                referenceNormal = glm::dvec3(1.0, 0.0, 0.0);
            }
            if (absoluteTangent.y <= minimum)
            {
                minimum = absoluteTangent.y;
                referenceNormal = glm::dvec3(0.0, 1.0, 0.0);
            }
            if (absoluteTangent.z <= minimum)
            {
                referenceNormal = glm::dvec3(0.0, 0.0, 1.0);
            }
            const glm::dvec3 crossReference = glm::normalize(
                glm::cross(frames[0u].tangent, referenceNormal));
            frames[0u].normal =
                glm::cross(frames[0u].tangent, crossReference);
            frames[0u].binormal =
                glm::cross(frames[0u].tangent, frames[0u].normal);
            for (uint32_t index = 1u; index <= parameters.steps; ++index)
            {
                frames[index].normal = frames[index - 1u].normal;
                frames[index].binormal = frames[index - 1u].binormal;
                glm::dvec3 axis = glm::cross(
                    frames[index - 1u].tangent,
                    frames[index].tangent);
                if (glm::length(axis) >
                    std::numeric_limits<double>::epsilon())
                {
                    axis = glm::normalize(axis);
                    const double angle = std::acos(eastl::clamp(
                        glm::dot(frames[index - 1u].tangent,
                                 frames[index].tangent),
                        -1.0, 1.0));
                    frames[index].normal = rotateAroundAxis(
                        frames[index].normal, axis, angle);
                }
                frames[index].binormal = glm::cross(
                    frames[index].tangent, frames[index].normal);
            }
            if (parameters.closed)
            {
                double angle = std::acos(eastl::clamp(
                    glm::dot(frames[0u].normal,
                             frames[parameters.steps].normal),
                    -1.0, 1.0)) / double(parameters.steps);
                const glm::dvec3 crossNormals = glm::cross(
                    frames[0u].normal, frames[parameters.steps].normal);
                if (glm::dot(frames[0u].tangent, crossNormals) > 0.0)
                {
                    angle = -angle;
                }
                for (uint32_t index = 1u; index <= parameters.steps; ++index)
                {
                    frames[index].normal = rotateAroundAxis(
                        frames[index].normal, frames[index].tangent,
                        angle * double(index));
                    frames[index].binormal = glm::cross(
                        frames[index].tangent, frames[index].normal);
                }
            }
            return frames;
        }

        /** Returns twice the signed polygon area. */
        double polygonSignedArea(const eastl::vector<glm::dvec2> &points)
        {
            double area = 0.0;
            for (size_t current = 0u, previous = points.size() - 1u;
                 current < points.size(); previous = current++)
            {
                area += points[previous].x * points[current].y -
                        points[current].x * points[previous].y;
            }
            return area * 0.5;
        }

        /** Returns the signed two-dimensional cross product. */
        double crossExtrudeVector2(const glm::dvec2 &left,
                                   const glm::dvec2 &right)
        {
            return left.x * right.y - left.y * right.x;
        }

        /** Tests whether one point lies in a triangle including its boundary. */
        bool pointInsideTriangle(const glm::dvec2 &point,
                                 const glm::dvec2 &a,
                                 const glm::dvec2 &b,
                                 const glm::dvec2 &c)
        {
            const double c0 =
                crossExtrudeVector2(b - a, point - a);
            const double c1 =
                crossExtrudeVector2(c - b, point - b);
            const double c2 =
                crossExtrudeVector2(a - c, point - c);
            return (c0 <= 0.0 && c1 <= 0.0 && c2 <= 0.0) ||
                   (c0 >= 0.0 && c1 >= 0.0 && c2 >= 0.0);
        }

        /** Triangulates one clockwise simple contour with deterministic ear clipping. */
        eastl::vector<ExtrudeTriangle> triangulateContour(
            const eastl::vector<glm::dvec2> &contour)
        {
            eastl::vector<uint32_t> remaining;
            remaining.reserve(contour.size());
            for (uint32_t index = 0u; index < contour.size(); ++index)
            {
                remaining.push_back(index);
            }
            eastl::vector<ExtrudeTriangle> triangles;
            while (remaining.size() > 3u)
            {
                bool clipped = false;
                for (size_t index = 0u; index < remaining.size(); ++index)
                {
                    const uint32_t previous =
                        remaining[(index + remaining.size() - 1u) %
                                  remaining.size()];
                    const uint32_t current = remaining[index];
                    const uint32_t next =
                        remaining[(index + 1u) % remaining.size()];
                    const glm::dvec2 edge0 =
                        contour[current] - contour[previous];
                    const glm::dvec2 edge1 =
                        contour[next] - contour[current];
                    if (edge0.x * edge1.y - edge0.y * edge1.x >= 0.0)
                    {
                        continue;
                    }
                    bool containsPoint = false;
                    for (uint32_t candidate : remaining)
                    {
                        if (candidate == previous || candidate == current ||
                            candidate == next)
                        {
                            continue;
                        }
                        if (pointInsideTriangle(
                                contour[candidate], contour[previous],
                                contour[current], contour[next]))
                        {
                            containsPoint = true;
                            break;
                        }
                    }
                    if (!containsPoint)
                    {
                        triangles.push_back({next, current, previous});
                        remaining.erase(remaining.begin() +
                                        static_cast<ptrdiff_t>(index));
                        clipped = true;
                        break;
                    }
                }
                if (!clipped)
                {
                    throw std::runtime_error(
                        "Extrude contour could not be triangulated.");
                }
            }
            triangles.push_back(
                {remaining[2u], remaining[1u], remaining[0u]});
            return triangles;
        }

        /** Reverses a counter-clockwise contour to r185 clockwise winding. */
        eastl::vector<glm::dvec2> makeClockwiseContour(
            const eastl::vector<glm::dvec2> &source)
        {
            eastl::vector<glm::dvec2> contour = source;
            if (polygonSignedArea(contour) >= 0.0)
            {
                eastl::reverse(contour.begin(), contour.end());
            }
            return contour;
        }

        /** Computes the r185 one-unit bevel movement for a contour vertex. */
        glm::dvec2 computeBevelMovement(const glm::dvec2 &point,
                                        const glm::dvec2 &previous,
                                        const glm::dvec2 &next)
        {
            const glm::dvec2 previousEdge = point - previous;
            const glm::dvec2 nextEdge = next - point;
            const double previousLengthSquared =
                glm::dot(previousEdge, previousEdge);
            const double collinear =
                previousEdge.x * nextEdge.y -
                previousEdge.y * nextEdge.x;
            double translatedX = 0.0;
            double translatedY = 0.0;
            double shrinkBy = 1.0;
            if (std::abs(collinear) >
                std::numeric_limits<double>::epsilon())
            {
                const double previousLength = std::sqrt(previousLengthSquared);
                const double nextLength = glm::length(nextEdge);
                const glm::dvec2 shiftedPrevious(
                    previous.x - previousEdge.y / previousLength,
                    previous.y + previousEdge.x / previousLength);
                const glm::dvec2 shiftedNext(
                    next.x - nextEdge.y / nextLength,
                    next.y + nextEdge.x / nextLength);
                const double scale =
                    ((shiftedNext.x - shiftedPrevious.x) * nextEdge.y -
                     (shiftedNext.y - shiftedPrevious.y) * nextEdge.x) /
                    collinear;
                translatedX =
                    shiftedPrevious.x + previousEdge.x * scale - point.x;
                translatedY =
                    shiftedPrevious.y + previousEdge.y * scale - point.y;
                const double lengthSquared =
                    translatedX * translatedX +
                    translatedY * translatedY;
                if (lengthSquared <= 2.0)
                {
                    return {translatedX, translatedY};
                }
                shrinkBy = std::sqrt(lengthSquared / 2.0);
            }
            else
            {
                bool sameDirection = false;
                if (previousEdge.x >
                    std::numeric_limits<double>::epsilon())
                {
                    sameDirection =
                        nextEdge.x >
                        std::numeric_limits<double>::epsilon();
                }
                else if (previousEdge.x <
                         -std::numeric_limits<double>::epsilon())
                {
                    sameDirection =
                        nextEdge.x <
                        -std::numeric_limits<double>::epsilon();
                }
                else
                {
                    sameDirection =
                        std::signbit(previousEdge.y) ==
                        std::signbit(nextEdge.y);
                }
                if (sameDirection)
                {
                    translatedX = -previousEdge.y;
                    translatedY = previousEdge.x;
                    shrinkBy = std::sqrt(previousLengthSquared);
                }
                else
                {
                    translatedX = previousEdge.x;
                    translatedY = previousEdge.y;
                    shrinkBy = std::sqrt(previousLengthSquared / 2.0);
                }
            }
            return {translatedX / shrinkBy, translatedY / shrinkBy};
        }

        /** Appends one flat-shaded triangle after Float32 position quantization. */
        void appendExtrudeTriangle(ExtrudeCpuGeometry &geometry,
                                   const glm::dvec3 &a,
                                   const glm::dvec3 &b,
                                   const glm::dvec3 &c,
                                   uint32_t materialSlot,
                                   bool lid)
        {
            const glm::vec3 positions[3u] = {
                glm::vec3(a), glm::vec3(b), glm::vec3(c)};
            glm::dvec3 normal = glm::cross(
                glm::dvec3(positions[2u] - positions[1u]),
                glm::dvec3(positions[0u] - positions[1u]));
            const double length = glm::length(normal);
            normal = length > 0.0 ? normal / length : glm::dvec3(0.0);
            for (const glm::vec3 &position : positions)
            {
                geometry.vertices.push_back({
                    .position = glm::vec4(position, 1.0f),
                    .normal = glm::vec4(glm::vec3(normal), 0.0f),
                    .materialSlotAndReserved =
                        glm::vec4(float(materialSlot), 0.0f, 0.0f, 0.0f),
                });
            }
            if (lid)
            {
                geometry.lidVertexCount += 3u;
            }
            else
            {
                geometry.sideVertexCount += 3u;
            }
        }

        /** Appends r185 lid triangles for the first and last placeholder layers. */
        void appendExtrudeLids(
            ExtrudeCpuGeometry &geometry,
            const eastl::vector<glm::dvec3> &placeholder,
            const eastl::vector<ExtrudeTriangle> &faces,
            uint32_t verticesPerLayer,
            uint32_t topLayer,
            uint32_t materialSlot)
        {
            const uint32_t topOffset = verticesPerLayer * topLayer;
            for (const ExtrudeTriangle &face : faces)
            {
                appendExtrudeTriangle(
                    geometry, placeholder[face.c], placeholder[face.b],
                    placeholder[face.a], materialSlot, true);
            }
            for (const ExtrudeTriangle &face : faces)
            {
                appendExtrudeTriangle(
                    geometry, placeholder[topOffset + face.a],
                    placeholder[topOffset + face.b],
                    placeholder[topOffset + face.c],
                    materialSlot, true);
            }
        }

        /** Appends all sidewall quads using r185's reverse contour iteration. */
        void appendExtrudeSidewalls(
            ExtrudeCpuGeometry &geometry,
            const eastl::vector<glm::dvec3> &placeholder,
            uint32_t contourLength,
            uint32_t layerTransitionCount,
            uint32_t materialSlot)
        {
            for (int32_t index = static_cast<int32_t>(contourLength) - 1;
                 index >= 0; --index)
            {
                const uint32_t current = static_cast<uint32_t>(index);
                const uint32_t previous =
                    current == 0u ? contourLength - 1u : current - 1u;
                for (uint32_t layer = 0u;
                     layer < layerTransitionCount; ++layer)
                {
                    const uint32_t firstOffset = contourLength * layer;
                    const uint32_t secondOffset =
                        contourLength * (layer + 1u);
                    const glm::dvec3 &a =
                        placeholder[firstOffset + current];
                    const glm::dvec3 &b =
                        placeholder[firstOffset + previous];
                    const glm::dvec3 &c =
                        placeholder[secondOffset + previous];
                    const glm::dvec3 &d =
                        placeholder[secondOffset + current];
                    appendExtrudeTriangle(
                        geometry, a, b, d, materialSlot, false);
                    appendExtrudeTriangle(
                        geometry, b, c, d, materialSlot, false);
                }
            }
        }
    } // namespace

    ExtrudeCpuGeometry buildPathExtrudeGeometry(
        const PathExtrudeParameters &parameters)
    {
        if (parameters.controlPoints.size() < 2u ||
            parameters.shape.size() < 3u || parameters.steps == 0u)
        {
            throw std::invalid_argument(
                "Path extrusion requires a curve, contour, and positive steps.");
        }
        const eastl::vector<glm::dvec2> contour =
            makeClockwiseContour(parameters.shape);
        const eastl::vector<ExtrudeTriangle> faces =
            triangulateContour(contour);
        const eastl::vector<double> lengths =
            buildCatmullRomLengths(parameters);
        const eastl::vector<ExtrudeFrenetFrame> frames =
            buildFrenetFrames(parameters, lengths);
        eastl::vector<glm::dvec3> placeholder;
        placeholder.reserve(
            contour.size() * (parameters.steps + 1u));
        for (uint32_t layer = 0u; layer <= parameters.steps; ++layer)
        {
            const glm::dvec3 center = sampleCatmullRomAt(
                parameters, lengths,
                double(layer) / double(parameters.steps));
            for (const glm::dvec2 &point : contour)
            {
                placeholder.push_back(
                    center + frames[layer].normal * point.x +
                    frames[layer].binormal * point.y);
            }
        }
        ExtrudeCpuGeometry geometry;
        appendExtrudeLids(
            geometry, placeholder, faces,
            static_cast<uint32_t>(contour.size()), parameters.steps,
            parameters.materialSlot);
        appendExtrudeSidewalls(
            geometry, placeholder,
            static_cast<uint32_t>(contour.size()), parameters.steps,
            parameters.materialSlot);
        return geometry;
    }

    ExtrudeCpuGeometry buildDepthExtrudeGeometry(
        const DepthExtrudeParameters &parameters)
    {
        if (parameters.shape.size() < 3u || parameters.steps == 0u ||
            (parameters.bevelEnabled && parameters.bevelSegments == 0u))
        {
            throw std::invalid_argument(
                "Depth extrusion requires a contour, positive steps, and a positive bevel segment count.");
        }
        const eastl::vector<glm::dvec2> contour =
            makeClockwiseContour(parameters.shape);
        eastl::vector<glm::dvec2> movements;
        movements.reserve(contour.size());
        for (size_t index = 0u; index < contour.size(); ++index)
        {
            movements.push_back(computeBevelMovement(
                contour[index],
                contour[(index + contour.size() - 1u) % contour.size()],
                contour[(index + 1u) % contour.size()]));
        }
        eastl::vector<glm::dvec3> placeholder;
        const uint32_t bevelSegments =
            parameters.bevelEnabled ? parameters.bevelSegments : 0u;
        const uint32_t layerCount =
            parameters.steps + bevelSegments * 2u + 1u;
        placeholder.reserve(contour.size() * layerCount);
        if (parameters.bevelEnabled)
        {
            // ExtrudeGeometry emits the back bevel from the outermost
            // contour toward the full-depth body.  Each layer follows the
            // quarter-circle profile used by the r185 implementation.
            for (uint32_t bevel = 0u; bevel < bevelSegments; ++bevel)
            {
                const double t = double(bevel) / double(bevelSegments);
                const double z = parameters.bevelThickness *
                    std::cos(t * Pi * 0.5);
                const double bevelScale = parameters.bevelSize *
                    std::sin(t * Pi * 0.5);
                for (size_t index = 0u; index < contour.size(); ++index)
                {
                    const glm::dvec2 point = contour[index] +
                        movements[index] * bevelScale;
                    placeholder.push_back(glm::dvec3(point, -z));
                }
            }
        }
        const double bevelScale =
            parameters.bevelEnabled ? parameters.bevelSize : 0.0;
        for (uint32_t layer = 0u; layer <= parameters.steps; ++layer)
        {
            for (size_t index = 0u; index < contour.size(); ++index)
            {
                const glm::dvec2 point =
                    contour[index] + movements[index] * bevelScale;
                placeholder.push_back(glm::dvec3(
                    point,
                    parameters.depth / double(parameters.steps) *
                        double(layer)));
            }
        }
        if (parameters.bevelEnabled)
        {
            // The front bevel is emitted in reverse layer order, matching
            // ExtrudeGeometry's side-wall layer indexing.
            for (int32_t bevel = static_cast<int32_t>(bevelSegments) - 1;
                 bevel >= 0; --bevel)
            {
                const double t = double(bevel) / double(bevelSegments);
                const double z = parameters.bevelThickness *
                    std::cos(t * Pi * 0.5);
                const double bevelScale = parameters.bevelSize *
                    std::sin(t * Pi * 0.5);
                for (size_t index = 0u; index < contour.size(); ++index)
                {
                    const glm::dvec2 point = contour[index] +
                        movements[index] * bevelScale;
                    placeholder.push_back(
                        glm::dvec3(point, parameters.depth + z));
                }
            }
        }
        const eastl::vector<ExtrudeTriangle> faces =
            triangulateContour(contour);
        ExtrudeCpuGeometry geometry;
        appendExtrudeLids(
            geometry, placeholder, faces,
            static_cast<uint32_t>(contour.size()), layerCount - 1u,
            parameters.lidMaterialSlot);
        appendExtrudeSidewalls(
            geometry, placeholder,
            static_cast<uint32_t>(contour.size()), layerCount - 1u,
            parameters.sideMaterialSlot);
        return geometry;
    }
} // namespace GVM::ThreeSamples
