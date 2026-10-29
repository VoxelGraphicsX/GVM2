#include "InstanceSampleGeometry.hpp"

#include "DeterministicRandom.hpp"

#include <EASTL/array.h>
#include <EASTL/algorithm.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <stdexcept>

namespace GVM::ThreeSamples::ThreeCompat
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;

        /** Returns one deterministic JavaScript-compatible random unit value. */
        double nextInstanceRandomUnit(DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Composes one matrix with Three.js double arithmetic before Float32 storage. */
        glm::mat4 composeThreeInstanceMatrix(
            const glm::dvec3 &position,
            double quaternionX,
            double quaternionY,
            double quaternionZ,
            double quaternionW,
            double scale)
        {
            const double x2 = quaternionX + quaternionX;
            const double y2 = quaternionY + quaternionY;
            const double z2 = quaternionZ + quaternionZ;
            const double xx = quaternionX * x2;
            const double xy = quaternionX * y2;
            const double xz = quaternionX * z2;
            const double yy = quaternionY * y2;
            const double yz = quaternionY * z2;
            const double zz = quaternionZ * z2;
            const double wx = quaternionW * x2;
            const double wy = quaternionW * y2;
            const double wz = quaternionW * z2;
            return glm::mat4(
                glm::vec4(
                    float((1.0 - (yy + zz)) * scale),
                    float((xy + wz) * scale),
                    float((xz - wy) * scale),
                    0.0f),
                glm::vec4(
                    float((xy - wz) * scale),
                    float((1.0 - (xx + zz)) * scale),
                    float((yz + wx) * scale),
                    0.0f),
                glm::vec4(
                    float((xz + wy) * scale),
                    float((yz - wx) * scale),
                    float((1.0 - (xx + yy)) * scale),
                    0.0f),
                glm::vec4(
                    float(position.x),
                    float(position.y),
                    float(position.z),
                    1.0f));
        }

        /** Recursively appends one r185 Hilbert3D branch. */
        void appendHilbert3D(
            eastl::vector<glm::vec3> &output,
            const glm::vec3 &center,
            float size,
            int32_t iterations,
            const eastl::array<uint32_t, 8u> &order)
        {
            const float half = size * 0.5f;
            const eastl::array<glm::vec3, 8u> source = {{
                center + glm::vec3(-half, half, -half),
                center + glm::vec3(-half, half, half),
                center + glm::vec3(-half, -half, half),
                center + glm::vec3(-half, -half, -half),
                center + glm::vec3(half, -half, -half),
                center + glm::vec3(half, -half, half),
                center + glm::vec3(half, half, half),
                center + glm::vec3(half, half, -half),
            }};
            eastl::array<glm::vec3, 8u> points = {};
            for (uint32_t index = 0u; index < 8u; ++index)
            {
                points[index] = source[order[index]];
            }
            if (--iterations >= 0)
            {
                appendHilbert3D(output, points[0u], half, iterations,
                                {{order[0u], order[3u], order[4u], order[7u],
                                  order[6u], order[5u], order[2u], order[1u]}});
                appendHilbert3D(output, points[1u], half, iterations,
                                {{order[0u], order[7u], order[6u], order[1u],
                                  order[2u], order[5u], order[4u], order[3u]}});
                appendHilbert3D(output, points[2u], half, iterations,
                                {{order[0u], order[7u], order[6u], order[1u],
                                  order[2u], order[5u], order[4u], order[3u]}});
                appendHilbert3D(output, points[3u], half, iterations,
                                {{order[2u], order[3u], order[0u], order[1u],
                                  order[6u], order[7u], order[4u], order[5u]}});
                appendHilbert3D(output, points[4u], half, iterations,
                                {{order[2u], order[3u], order[0u], order[1u],
                                  order[6u], order[7u], order[4u], order[5u]}});
                appendHilbert3D(output, points[5u], half, iterations,
                                {{order[4u], order[3u], order[2u], order[5u],
                                  order[6u], order[1u], order[0u], order[7u]}});
                appendHilbert3D(output, points[6u], half, iterations,
                                {{order[4u], order[3u], order[2u], order[5u],
                                  order[6u], order[1u], order[0u], order[7u]}});
                appendHilbert3D(output, points[7u], half, iterations,
                                {{order[6u], order[5u], order[2u], order[1u],
                                  order[0u], order[3u], order[4u], order[7u]}});
                return;
            }
            output.insert(output.end(), points.begin(), points.end());
        }

        /** Initializes one nonuniform Catmull-Rom cubic polynomial. */
        glm::vec3 evaluateCentripetalSegment(
            const glm::vec3 &p0,
            const glm::vec3 &p1,
            const glm::vec3 &p2,
            const glm::vec3 &p3,
            float weight)
        {
            float dt0 = std::pow(glm::length2(p0 - p1), 0.25f);
            float dt1 = std::pow(glm::length2(p1 - p2), 0.25f);
            float dt2 = std::pow(glm::length2(p2 - p3), 0.25f);
            if (dt1 < 1.0e-4f) dt1 = 1.0f;
            if (dt0 < 1.0e-4f) dt0 = dt1;
            if (dt2 < 1.0e-4f) dt2 = dt1;
            glm::vec3 tangent1 =
                (p1 - p0) / dt0 -
                (p2 - p0) / (dt0 + dt1) +
                (p2 - p1) / dt1;
            glm::vec3 tangent2 =
                (p2 - p1) / dt1 -
                (p3 - p1) / (dt1 + dt2) +
                (p3 - p2) / dt2;
            tangent1 *= dt1;
            tangent2 *= dt1;
            const glm::vec3 c0 = p1;
            const glm::vec3 c1 = tangent1;
            const glm::vec3 c2 = -3.0f * p1 + 3.0f * p2 - 2.0f * tangent1 - tangent2;
            const glm::vec3 c3 = 2.0f * p1 - 2.0f * p2 + tangent1 + tangent2;
            return ((c3 * weight + c2) * weight + c1) * weight + c0;
        }

        /** Returns the clamped endpoint extrapolation used by an open r185 curve. */
        glm::vec3 extrapolateEndpoint(const glm::vec3 &first, const glm::vec3 &second)
        {
            return first * 2.0f - second;
        }

        /** Returns the hue helper used by Three Color.setHSL. */
        float hueToRgb(float p, float q, float t)
        {
            if (t < 0.0f) t += 1.0f;
            if (t > 1.0f) t -= 1.0f;
            if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
            if (t < 1.0f / 2.0f) return q;
            if (t < 2.0f / 3.0f) return p + (q - p) * 6.0f * (2.0f / 3.0f - t);
            return p;
        }

        /** Converts one Three.js XYZ Euler rotation into its compose quaternion. */
        glm::quat makeThreeXyzQuaternion(const glm::vec3 &rotation)
        {
            const float cosineX = std::cos(rotation.x * 0.5f);
            const float cosineY = std::cos(rotation.y * 0.5f);
            const float cosineZ = std::cos(rotation.z * 0.5f);
            const float sineX = std::sin(rotation.x * 0.5f);
            const float sineY = std::sin(rotation.y * 0.5f);
            const float sineZ = std::sin(rotation.z * 0.5f);
            return glm::quat(
                cosineX * cosineY * cosineZ -
                    sineX * sineY * sineZ,
                sineX * cosineY * cosineZ +
                    cosineX * sineY * sineZ,
                cosineX * sineY * cosineZ -
                    sineX * cosineY * sineZ,
                cosineX * cosineY * sineZ +
                    sineX * sineY * cosineZ);
        }
    }

    InstanceSampleMesh buildInstanceSampleMesh(const DecodedBufferGeometry &geometry)
    {
        if (geometry.positions.size() % 3u != 0u ||
            geometry.indices.size() % 3u != 0u)
        {
            throw std::invalid_argument("Instance sample geometry must contain indexed triangles.");
        }
        InstanceSampleMesh result;
        result.positions.reserve(geometry.positions.size() / 3u);
        for (size_t index = 0u; index < geometry.positions.size(); index += 3u)
        {
            result.positions.emplace_back(
                geometry.positions[index],
                geometry.positions[index + 1u],
                geometry.positions[index + 2u]);
        }
        result.indices.assign(geometry.indices.begin(), geometry.indices.end());
        result.normals.resize(result.positions.size(), glm::vec3(0.0f));
        for (size_t index = 0u; index < result.indices.size(); index += 3u)
        {
            const uint32_t a = result.indices[index];
            const uint32_t b = result.indices[index + 1u];
            const uint32_t c = result.indices[index + 2u];
            if (a >= result.positions.size() ||
                b >= result.positions.size() ||
                c >= result.positions.size())
            {
                throw std::out_of_range("Instance sample index exceeds the position array.");
            }
            const glm::dvec3 positionA(result.positions[a]);
            const glm::dvec3 positionB(result.positions[b]);
            const glm::dvec3 positionC(result.positions[c]);
            const glm::dvec3 faceNormal = glm::cross(
                positionC - positionB,
                positionA - positionB);
            for (uint32_t vertexIndex : {a, b, c})
            {
                glm::vec3 &normal = result.normals[vertexIndex];
                normal.x = float(double(normal.x) + faceNormal.x);
                normal.y = float(double(normal.y) + faceNormal.y);
                normal.z = float(double(normal.z) + faceNormal.z);
            }
        }
        for (glm::vec3 &normal : result.normals)
        {
            const glm::dvec3 preciseNormal(normal);
            const double lengthSquared =
                glm::dot(preciseNormal, preciseNormal);
            normal = lengthSquared > 0.0
                ? glm::vec3(
                      preciseNormal / std::sqrt(lengthSquared))
                : glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return result;
    }

    eastl::vector<InstanceSampleTransform> buildInstancingPerformanceTransforms(
        uint32_t count,
        uint32_t seed,
        uint32_t preTransformRandomDrawCount,
        uint32_t postTransformRandomDrawCount)
    {
        DeterministicRandom random(seed);
        for (uint32_t draw = 0u;
             draw < preTransformRandomDrawCount;
             ++draw)
        {
            (void)random.nextUint32();
        }
        eastl::vector<InstanceSampleTransform> result;
        result.reserve(count);
        for (uint32_t index = 0u; index < count; ++index)
        {
            const glm::dvec3 position(
                nextInstanceRandomUnit(random) * 40.0 - 20.0,
                nextInstanceRandomUnit(random) * 40.0 - 20.0,
                nextInstanceRandomUnit(random) * 40.0 - 20.0);
            const double theta1 = 2.0 * Pi * nextInstanceRandomUnit(random);
            const double theta2 = 2.0 * Pi * nextInstanceRandomUnit(random);
            const double x0 = nextInstanceRandomUnit(random);
            const double radius1 = std::sqrt(1.0 - x0);
            const double radius2 = std::sqrt(x0);
            const double quaternionX = radius1 * std::sin(theta1);
            const double quaternionY = radius1 * std::cos(theta1);
            const double quaternionZ = radius2 * std::sin(theta2);
            const double quaternionW = radius2 * std::cos(theta2);
            const double scale = nextInstanceRandomUnit(random);
            InstanceSampleTransform transform;
            transform.matrix = composeThreeInstanceMatrix(
                position,
                quaternionX,
                quaternionY,
                quaternionZ,
                quaternionW,
                scale);
            transform.ordinal = index;
            result.push_back(transform);
            for (uint32_t draw = 0u;
                 draw < postTransformRandomDrawCount;
                 ++draw)
            {
                (void)random.nextUint32();
            }
        }
        return result;
    }

    eastl::vector<InstanceSampleTransform> buildWebgpuInstanceMeshTransforms(
        uint32_t amount,
        uint32_t activeCount,
        double timeSeconds)
    {
        const uint64_t capacity = uint64_t(amount) * amount * amount;
        if (activeCount > capacity)
        {
            throw std::invalid_argument("Active instance count exceeds the cubic grid capacity.");
        }
        eastl::vector<InstanceSampleTransform> result;
        result.reserve(activeCount);
        const float offset = float(amount - 1u) * 0.5f;
        uint32_t ordinal = 0u;
        for (uint32_t x = 0u; x < amount && ordinal < activeCount; ++x)
        {
            for (uint32_t y = 0u; y < amount && ordinal < activeCount; ++y)
            {
                for (uint32_t z = 0u; z < amount && ordinal < activeCount; ++z)
                {
                    const float rotationY =
                        static_cast<float>(
                            std::sin(double(x) * 0.25 + timeSeconds) +
                            std::sin(double(y) * 0.25 + timeSeconds) +
                            std::sin(double(z) * 0.25 + timeSeconds));
                    InstanceSampleTransform transform;
                    transform.matrix =
                        glm::translate(
                            glm::mat4(1.0f),
                            glm::vec3(offset - float(x),
                                      offset - float(y),
                                      offset - float(z))) *
                        glm::mat4_cast(makeThreeXyzQuaternion(glm::vec3(
                            0.0f,
                            rotationY,
                            rotationY * 2.0f)));
                    transform.ordinal = ordinal++;
                    result.push_back(transform);
                }
            }
        }
        return result;
    }

    eastl::vector<glm::vec3> buildHilbert3DControlPoints(
        const glm::vec3 &center,
        float size,
        int32_t iterations)
    {
        eastl::vector<glm::vec3> result;
        appendHilbert3D(result, center, size, iterations,
                        {{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u}});
        return result;
    }

    eastl::vector<glm::vec3> sampleCentripetalCatmullRom(
        const eastl::vector<glm::vec3> &controlPoints,
        uint32_t sampleCount)
    {
        if (controlPoints.size() < 2u || sampleCount == 0u)
        {
            throw std::invalid_argument("Catmull-Rom sampling requires two points and a positive sample count.");
        }
        eastl::vector<glm::vec3> result;
        result.reserve(sampleCount);
        const uint32_t segmentCount = static_cast<uint32_t>(controlPoints.size() - 1u);
        for (uint32_t index = 0u; index < sampleCount; ++index)
        {
            const float parameter = float(index) / float(sampleCount);
            const float scaled = parameter * float(segmentCount);
            const uint32_t segment =
                eastl::min(static_cast<uint32_t>(std::floor(scaled)), segmentCount - 1u);
            const float weight = scaled - float(segment);
            const glm::vec3 &p1 = controlPoints[segment];
            const glm::vec3 &p2 = controlPoints[segment + 1u];
            const glm::vec3 p0 = segment == 0u
                ? extrapolateEndpoint(p1, p2)
                : controlPoints[segment - 1u];
            const glm::vec3 p3 = segment + 2u >= controlPoints.size()
                ? extrapolateEndpoint(p2, p1)
                : controlPoints[segment + 2u];
            result.push_back(evaluateCentripetalSegment(p0, p1, p2, p3, weight));
        }
        return result;
    }

    glm::vec3 convertLinearHslToRgb(float hue, float saturation, float lightness)
    {
        hue -= std::floor(hue);
        saturation = glm::clamp(saturation, 0.0f, 1.0f);
        lightness = glm::clamp(lightness, 0.0f, 1.0f);
        if (saturation == 0.0f) return glm::vec3(lightness);
        const float q = lightness <= 0.5f
            ? lightness * (1.0f + saturation)
            : lightness + saturation - lightness * saturation;
        const float p = 2.0f * lightness - q;
        return glm::vec3(
            hueToRgb(p, q, hue + 1.0f / 3.0f),
            hueToRgb(p, q, hue),
            hueToRgb(p, q, hue - 1.0f / 3.0f));
    }
}
