#include "TubeMesh.hpp"

#include <EASTL/algorithm.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        constexpr double TangentDelta = 0.0001;
        constexpr double TwoPi = 6.283185307179586476925286766559;

        /** Stores cumulative curve lengths and their shared evaluator contract. */
        struct CurveArcLengthTable
        {
            CurvePointFunction evaluatePoint = nullptr;
            const void *context = nullptr;
            eastl::vector<double> lengths;
        };

        /** Samples the curve callback after validating its immutable contract. */
        glm::dvec3 evaluateCurvePoint(
            const TubeMeshParameters &parameters,
            double parameter)
        {
            return parameters.evaluatePoint(
                parameter,
                parameters.curveContext);
        }

        /** Builds Three's default 200-division cumulative arc-length table. */
        CurveArcLengthTable buildCurveArcLengthTable(
            const TubeMeshParameters &parameters)
        {
            CurveArcLengthTable table;
            table.evaluatePoint = parameters.evaluatePoint;
            table.context = parameters.curveContext;
            table.lengths.reserve(
                static_cast<size_t>(
                    parameters.arcLengthDivisions) +
                1u);
            table.lengths.push_back(0.0);
            glm::dvec3 previous =
                evaluateCurvePoint(parameters, 0.0);
            double cumulativeLength = 0.0;
            for (uint32_t division = 1u;
                 division <= parameters.arcLengthDivisions;
                 ++division)
            {
                const glm::dvec3 current =
                    evaluateCurvePoint(
                        parameters,
                        double(division) /
                            double(parameters.arcLengthDivisions));
                cumulativeLength +=
                    glm::length(current - previous);
                table.lengths.push_back(
                    cumulativeLength);
                previous = current;
            }
            return table;
        }

        /** Maps uniform arc-length space back to the curve parameter. */
        double mapCurveArcLength(
            const CurveArcLengthTable &table,
            double normalizedDistance)
        {
            const double targetLength =
                normalizedDistance *
                table.lengths.back();
            int32_t low = 0;
            int32_t high =
                static_cast<int32_t>(
                    table.lengths.size()) -
                1;
            int32_t index = 0;
            while (low <= high)
            {
                index =
                    low + (high - low) / 2;
                const double comparison =
                    table.lengths[
                        static_cast<size_t>(index)] -
                    targetLength;
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
            if (index < 0)
            {
                return 0.0;
            }
            const size_t beforeIndex =
                static_cast<size_t>(index);
            if (table.lengths[beforeIndex] ==
                targetLength)
            {
                return double(beforeIndex) /
                    double(table.lengths.size() - 1u);
            }
            const double segmentLength =
                table.lengths[beforeIndex + 1u] -
                table.lengths[beforeIndex];
            const double segmentFraction =
                (targetLength -
                 table.lengths[beforeIndex]) /
                segmentLength;
            return
                (double(beforeIndex) +
                 segmentFraction) /
                double(table.lengths.size() - 1u);
        }

        /** Evaluates Three's finite-difference tangent in arc-length space. */
        glm::dvec3 evaluateCurveTangentAt(
            const TubeMeshParameters &parameters,
            const CurveArcLengthTable &table,
            double normalizedDistance)
        {
            const double parameter =
                mapCurveArcLength(
                    table,
                    normalizedDistance);
            const double before =
                eastl::max(
                    parameter - TangentDelta,
                    0.0);
            const double after =
                eastl::min(
                    parameter + TangentDelta,
                    1.0);
            return glm::normalize(
                evaluateCurvePoint(parameters, after) -
                evaluateCurvePoint(parameters, before));
        }

        /** Rotates one vector around one normalized axis with Rodrigues' formula. */
        glm::dvec3 rotateAroundAxis(
            const glm::dvec3 &value,
            const glm::dvec3 &axis,
            double angle)
        {
            return value * std::cos(angle) +
                glm::cross(axis, value) *
                    std::sin(angle) +
                axis *
                    glm::dot(axis, value) *
                    (1.0 - std::cos(angle));
        }
    }

    /** Validates the immutable curve and TubeGeometry tessellation contract. */
    void validateTubeMeshParameters(
        const TubeMeshParameters &parameters)
    {
        if (parameters.evaluatePoint == nullptr ||
            parameters.tubularSegments == 0u ||
            parameters.radialSegments == 0u ||
            parameters.radius <= 0.0 ||
            parameters.arcLengthDivisions == 0u)
        {
            throw std::invalid_argument(
                "TubeGeometry requires a curve, positive radius, and nonzero segment counts.");
        }
    }

    TubeCurveSample sampleTubeCurve(
        const TubeMeshParameters &parameters,
        double normalizedDistance)
    {
        validateTubeMeshParameters(parameters);
        const CurveArcLengthTable arcLengths =
            buildCurveArcLengthTable(parameters);
        const double clampedDistance =
            eastl::clamp(
                normalizedDistance,
                0.0,
                1.0);
        const double parameter =
            mapCurveArcLength(
                arcLengths,
                clampedDistance);
        return {
            .point =
                evaluateCurvePoint(
                    parameters,
                    parameter),
            .tangent =
                evaluateCurveTangentAt(
                    parameters,
                    arcLengths,
                    clampedDistance),
            .totalLength =
                arcLengths.lengths.back(),
        };
    }

    TubeMesh buildTubeMesh(
        const TubeMeshParameters &parameters)
    {
        validateTubeMeshParameters(parameters);

        const CurveArcLengthTable arcLengths =
            buildCurveArcLengthTable(parameters);
        eastl::vector<glm::dvec3> tangents;
        eastl::vector<glm::dvec3> frameNormals;
        eastl::vector<glm::dvec3> frameBinormals;
        const size_t frameCount =
            static_cast<size_t>(
                parameters.tubularSegments) +
            1u;
        tangents.reserve(frameCount);
        frameNormals.resize(frameCount);
        frameBinormals.resize(frameCount);
        for (uint32_t segment = 0u;
             segment <= parameters.tubularSegments;
             ++segment)
        {
            tangents.push_back(
                evaluateCurveTangentAt(
                    parameters,
                    arcLengths,
                    double(segment) /
                        double(parameters.tubularSegments)));
        }

        const glm::dvec3 firstTangent =
            tangents.front();
        const double tangentX =
            std::abs(firstTangent.x);
        const double tangentY =
            std::abs(firstTangent.y);
        const double tangentZ =
            std::abs(firstTangent.z);
        glm::dvec3 initialAxis(1.0, 0.0, 0.0);
        double minimumComponent = tangentX;
        if (tangentY <= minimumComponent)
        {
            minimumComponent = tangentY;
            initialAxis =
                glm::dvec3(0.0, 1.0, 0.0);
        }
        if (tangentZ <= minimumComponent)
        {
            initialAxis =
                glm::dvec3(0.0, 0.0, 1.0);
        }
        const glm::dvec3 perpendicular =
            glm::normalize(
                glm::cross(
                    firstTangent,
                    initialAxis));
        frameNormals[0u] =
            glm::cross(
                firstTangent,
                perpendicular);
        frameBinormals[0u] =
            glm::cross(
                firstTangent,
                frameNormals[0u]);

        glm::dvec3 frameCross(0.0);
        for (uint32_t segment = 1u;
             segment <= parameters.tubularSegments;
             ++segment)
        {
            frameNormals[segment] =
                frameNormals[segment - 1u];
            frameBinormals[segment] =
                frameBinormals[segment - 1u];
            frameCross =
                glm::cross(
                    tangents[segment - 1u],
                    tangents[segment]);
            if (glm::length(frameCross) >
                std::numeric_limits<double>::epsilon())
            {
                frameCross =
                    glm::normalize(frameCross);
                const double angle =
                    std::acos(
                        eastl::clamp(
                            glm::dot(
                                tangents[segment - 1u],
                                tangents[segment]),
                            -1.0,
                            1.0));
                frameNormals[segment] =
                    rotateAroundAxis(
                        frameNormals[segment],
                        frameCross,
                        angle);
            }
            frameBinormals[segment] =
                glm::cross(
                    tangents[segment],
                    frameNormals[segment]);
        }

        if (parameters.closed)
        {
            double angle =
                std::acos(
                    eastl::clamp(
                        glm::dot(
                            frameNormals.front(),
                            frameNormals.back()),
                        -1.0,
                        1.0)) /
                double(parameters.tubularSegments);
            if (glm::dot(
                    tangents.front(),
                    glm::cross(
                        frameNormals.front(),
                        frameNormals.back())) >
                0.0)
            {
                angle = -angle;
            }
            for (uint32_t segment = 1u;
                 segment <= parameters.tubularSegments;
                 ++segment)
            {
                frameNormals[segment] =
                    rotateAroundAxis(
                        frameNormals[segment],
                        tangents[segment],
                        angle * double(segment));
                frameBinormals[segment] =
                    glm::cross(
                        tangents[segment],
                        frameNormals[segment]);
            }
        }

        TubeMesh mesh;
        mesh.tangents = tangents;
        mesh.frameNormals = frameNormals;
        mesh.frameBinormals = frameBinormals;
        const size_t ringVertexCount =
            static_cast<size_t>(
                parameters.radialSegments) +
            1u;
        const size_t vertexCount =
            frameCount * ringVertexCount;
        mesh.positions.reserve(vertexCount);
        mesh.normals.reserve(vertexCount);
        mesh.textureCoordinates.reserve(vertexCount);
        mesh.indices.reserve(
            static_cast<size_t>(
                parameters.tubularSegments) *
            parameters.radialSegments *
            6u);
        for (uint32_t segment = 0u;
             segment <= parameters.tubularSegments;
             ++segment)
        {
            const uint32_t sampledSegment =
                parameters.closed &&
                        segment ==
                            parameters.tubularSegments
                    ? 0u
                    : segment;
            const glm::dvec3 center =
                evaluateCurvePoint(
                    parameters,
                    mapCurveArcLength(
                        arcLengths,
                        double(sampledSegment) /
                            double(parameters.tubularSegments)));
            for (uint32_t radial = 0u;
                 radial <= parameters.radialSegments;
                 ++radial)
            {
                const double angle =
                    double(radial) /
                    double(parameters.radialSegments) *
                    TwoPi;
                const glm::dvec3 normal =
                    glm::normalize(
                        -std::cos(angle) *
                            frameNormals[sampledSegment] +
                        std::sin(angle) *
                            frameBinormals[sampledSegment]);
                mesh.positions.push_back(
                    glm::vec3(
                        center +
                        parameters.radius * normal));
                mesh.normals.push_back(
                    glm::vec3(normal));
                mesh.textureCoordinates.push_back({
                    float(segment) /
                        float(parameters.tubularSegments),
                    float(radial) /
                        float(parameters.radialSegments),
                });
            }
        }
        for (uint32_t segment = 1u;
             segment <= parameters.tubularSegments;
             ++segment)
        {
            for (uint32_t radial = 1u;
                 radial <= parameters.radialSegments;
                 ++radial)
            {
                const uint32_t a =
                    (parameters.radialSegments + 1u) *
                        (segment - 1u) +
                    radial - 1u;
                const uint32_t b =
                    (parameters.radialSegments + 1u) *
                        segment +
                    radial - 1u;
                const uint32_t c =
                    (parameters.radialSegments + 1u) *
                        segment +
                    radial;
                const uint32_t d =
                    (parameters.radialSegments + 1u) *
                        (segment - 1u) +
                    radial;
                mesh.indices.insert(
                    mesh.indices.end(),
                    {a, b, d, b, c, d});
            }
        }
        return mesh;
    }
} // namespace GVM::ThreeSamples::ThreeCompat
