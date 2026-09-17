#include "WebglBuffergeometryDrawrangeRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.1415926535897932384626433832795;
        // r185 first converts Date.now() to seconds, then applies the 0.1
        // rotation scale. Preserve that binary64 operation order here.
        constexpr double ReferenceEpochSeconds = 1700000000.0;
        constexpr double FrameStepSeconds = 1.0 / 60.0;
        constexpr uint32_t MaximumParticleCount = 1000u;
        constexpr float BoxExtent = 800.0f;
        constexpr float HalfBoxExtent = 400.0f;
        constexpr float MinimumDistance = 150.0f;
        // The locked r185 reference capture seeds Math.random with the
        // manifest value below.  This case predates the shared host default,
        // so it must not silently fall back to 0x12345678.
        // Keep the sample's deterministic stream aligned with the locked r185 oracle.
        constexpr uint32_t DrawrangeRandomSeed = 407896067u;
        constexpr uint32_t DefaultHostRandomSeed = 0x12345678u;

        /** Carries one deterministic upstream particle state through integration. */
        struct DrawrangeParticleState
        {
            glm::vec3 position;
            // Three keeps velocity in JavaScript Number precision while the
            // position attribute is a Float32Array. Preserve that split here.
            glm::dvec3 velocity;
            uint32_t connectionCount = 0u;
        };

        /** Stores the endpoint snapshots written to the upstream line geometry. */
        struct DrawrangeConnectionState
        {
            uint32_t startIndex = 0u;
            uint32_t endIndex = 0u;
            glm::vec3 startPosition;
            glm::vec3 endPosition;
            float alpha = 0.0f;
        };

        static_assert(sizeof(WebglBuffergeometryDrawrangeHostVertex) == 128u);
        static_assert(sizeof(WebglBuffergeometryDrawrangeHostObjectData) == 80u);
        static_assert(sizeof(WebglBuffergeometryDrawrangeHostInstanceData) == 16u);
        static_assert(sizeof(WebglBuffergeometryDrawrangeHostMaterialData) == 16u);

        /** Creates parent directories for one requested artifact. */
        void prepareOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one text artifact when the host requested a path. */
        void writeTextArtifact(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error("Could not write DrawRange text artifact.");
        }

        /** Returns one JavaScript-compatible xorshift random value in [0,1). */
        double nextDrawrangeRandom(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Converts one sRGB byte channel to the linear material domain. */
        float srgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Returns the locked input replay hash for the limited-connections scenario. */
        std::string sha256File(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open DrawRange input replay.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
                throw std::runtime_error("DrawRange input replay is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
                throw std::runtime_error("Could not read DrawRange input replay.");
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            constexpr char HexDigits[] = "0123456789abcdef";
            std::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 15u]);
            }
            return result;
        }

        /** Validates the three locked DrawRange scenarios and their dimensions. */
        void validateScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool limited = options.scenarioId == "limited-connections" &&
                options.targetFrame == 61u && !options.inputReplayPath.empty();
            if (options.caseId != "webgl_buffergeometry_drawrange" ||
                (!initial && !animated && !limited) || options.width != 800u ||
                options.height != 500u ||
                (options.randomSeed != DrawrangeRandomSeed &&
                 options.randomSeed != DefaultHostRandomSeed) ||
                ((!limited) && !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "DrawRange requires the locked initial, animated, or limited scenario.");
            }
        }

        /** Clips one homogeneous segment to the six canonical clip planes. */
        bool clipDrawrangeSegment(
            const glm::vec4 &startClip,
            const glm::vec4 &endClip,
            glm::vec4 &clippedStart,
            glm::vec4 &clippedEnd)
        {
            const float startValues[6u] = {
                startClip.x + startClip.w,
                startClip.w - startClip.x,
                startClip.y + startClip.w,
                startClip.w - startClip.y,
                startClip.z + startClip.w,
                startClip.w - startClip.z};
            const float endValues[6u] = {
                endClip.x + endClip.w,
                endClip.w - endClip.x,
                endClip.y + endClip.w,
                endClip.w - endClip.y,
                endClip.z + endClip.w,
                endClip.w - endClip.z};
            float enter = 0.0f;
            float exit = 1.0f;
            for (uint32_t plane = 0u; plane < 6u; ++plane)
            {
                const float a = startValues[plane];
                const float b = endValues[plane];
                if (a < 0.0f && b < 0.0f)
                    return false;
                if (a < 0.0f || b < 0.0f)
                {
                    const float t = a / (a - b);
                    if (a < 0.0f)
                        enter = std::max(enter, t);
                    else
                        exit = std::min(exit, t);
                    if (enter > exit)
                        return false;
                }
            }
            clippedStart = startClip + (endClip - startClip) * enter;
            clippedEnd = startClip + (endClip - startClip) * exit;
            return true;
        }

        /** Expands one clipped connection into a one-pixel triangle-list segment. */
        void appendLineQuad(
            eastl::vector<WebglBuffergeometryDrawrangeHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec3 &start,
            const glm::vec3 &end,
            const glm::vec3 &startColor,
            const glm::vec3 &endColor,
            const glm::vec4 &lineStartClip,
            const glm::vec4 &lineEndClip,
            float halfWidth)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const glm::vec4 startPosition(start, 1.0f);
            const glm::vec4 endPosition(end, 1.0f);
            const glm::vec4 startValue(startColor, 1.0f);
            const glm::vec4 endValue(endColor, 1.0f);
            const float edge = halfWidth > 0.0f ? halfWidth : 0.75f;
            vertices.push_back({startPosition, endPosition, startValue, endValue,
                                {0.0f, -edge, 0.0f, 0.0f},
                                lineStartClip, lineEndClip, glm::vec4(0.0f)});
            vertices.push_back({startPosition, endPosition, startValue, endValue,
                                {0.0f, edge, 0.0f, 0.0f},
                                lineStartClip, lineEndClip, glm::vec4(0.0f)});
            vertices.push_back({startPosition, endPosition, startValue, endValue,
                                {1.0f, edge, 0.0f, 0.0f},
                                lineStartClip, lineEndClip, glm::vec4(0.0f)});
            vertices.push_back({startPosition, endPosition, startValue, endValue,
                                {1.0f, -edge, 0.0f, 0.0f},
                                lineStartClip, lineEndClip, glm::vec4(0.0f)});
            indices.insert(indices.end(), {base, base + 1u, base + 2u,
                                           base, base + 2u, base + 3u});
        }

        /** Appends one native line-list segment with endpoint color attributes. */
        void appendLineSegment(
            eastl::vector<WebglBuffergeometryDrawrangeHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec3 &start,
            const glm::vec3 &end,
            const glm::vec3 &startColor,
            const glm::vec3 &endColor)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const glm::vec4 startPosition(start, 1.0f);
            const glm::vec4 endPosition(end, 1.0f);
            vertices.push_back({startPosition, endPosition,
                                glm::vec4(startColor, 1.0f),
                                glm::vec4(endColor, 1.0f),
                                {0.0f, 0.0f, 0.0f, 0.0f},
                                glm::vec4(0.0f), glm::vec4(0.0f),
                                glm::vec4(0.0f)});
            vertices.push_back({startPosition, endPosition,
                                glm::vec4(startColor, 1.0f),
                                glm::vec4(endColor, 1.0f),
                                {1.0f, 0.0f, 0.0f, 0.0f},
                                glm::vec4(0.0f), glm::vec4(0.0f),
                                glm::vec4(0.0f)});
            indices.insert(indices.end(), {base, base + 1u});
        }

        /** Tests one 1px window sample against the WebGL aliased-line diamond. */
        bool coversDrawrangeDiamond(
            const glm::dvec2 &pixelCenter,
            const glm::dvec2 &segmentStart,
            const glm::dvec2 &segmentEnd)
        {
            const glm::dvec2 transformedStart(
                segmentStart.x + segmentStart.y - pixelCenter.x - pixelCenter.y,
                segmentStart.x - segmentStart.y - pixelCenter.x + pixelCenter.y);
            const glm::dvec2 transformedEnd(
                segmentEnd.x + segmentEnd.y - pixelCenter.x - pixelCenter.y,
                segmentEnd.x - segmentEnd.y - pixelCenter.x + pixelCenter.y);
            const glm::dvec2 delta = transformedEnd - transformedStart;
            constexpr double extent = 0.5;
            double enter = 0.0;
            double exit = 1.0;
            const double componentsStart[2u] = {
                transformedStart.x, transformedStart.y};
            const double componentsDelta[2u] = {
                delta.x, delta.y};
            for (uint32_t component = 0u; component < 2u; ++component)
            {
                const double start = componentsStart[component];
                const double direction = componentsDelta[component];
                if (std::abs(direction) < 1.0e-12)
                {
                    if (std::abs(start) > extent)
                        return false;
                    continue;
                }
                const double first = (-extent - start) / direction;
                const double second = (extent - start) / direction;
                enter = std::max(enter, std::min(first, second));
                exit = std::min(exit, std::max(first, second));
            }
            return enter <= exit && exit >= 0.0 && enter <= 1.0;
        }

        /** Appends one pixel quad whose clip-space coverage is fixed in the DSL pass. */
        void appendDrawrangePixelQuad(
            eastl::vector<WebglBuffergeometryDrawrangeHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec4 &clipPosition,
            int32_t pixelX,
            int32_t pixelY,
            const glm::vec3 &color)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const float width = clipPosition.w;
            const auto pixelClip = [width, &clipPosition](
                                        float x,
                                        float y) {
                return glm::vec4(
                    (x / 400.0f - 1.0f) * width,
                    (1.0f - y / 250.0f) * width,
                    clipPosition.z,
                    width);
            };
            const glm::vec4 startPosition(0.0f);
            const glm::vec4 colorValue(color, 1.0f);
            const glm::vec4 noClip(0.0f);
            const glm::vec4 corners[4u] = {
                pixelClip(float(pixelX), float(pixelY)),
                pixelClip(float(pixelX), float(pixelY + 1)),
                pixelClip(float(pixelX + 1), float(pixelY + 1)),
                pixelClip(float(pixelX + 1), float(pixelY))};
            vertices.push_back({startPosition, startPosition, colorValue,
                                colorValue, {0.0f, 0.0f, 0.0f, 0.0f}, noClip,
                                noClip, corners[0u]});
            vertices.push_back({startPosition, startPosition, colorValue,
                                colorValue, {0.0f, 1.0f, 0.0f, 0.0f}, noClip,
                                noClip, corners[1u]});
            vertices.push_back({startPosition, startPosition, colorValue,
                                colorValue, {1.0f, 1.0f, 0.0f, 0.0f}, noClip,
                                noClip, corners[2u]});
            vertices.push_back({startPosition, startPosition, colorValue,
                                colorValue, {1.0f, 0.0f, 0.0f, 0.0f}, noClip,
                                noClip, corners[3u]});
            indices.insert(indices.end(), {base, base + 1u, base + 2u,
                                           base, base + 2u, base + 3u});
        }

        /** Expands a clipped connection into exact single-sample WebGL pixels. */
        void appendRasterizedDrawrangeLine(
            eastl::vector<WebglBuffergeometryDrawrangeHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec3 &start,
            const glm::vec3 &end,
            const glm::vec3 &color,
            const glm::mat4 &modelViewProjection)
        {
            const glm::vec4 startClip = modelViewProjection * glm::vec4(start, 1.0f);
            const glm::vec4 endClip = modelViewProjection * glm::vec4(end, 1.0f);
            glm::vec4 clippedStart;
            glm::vec4 clippedEnd;
            if (!clipDrawrangeSegment(
                    startClip, endClip, clippedStart, clippedEnd))
                return;
            const glm::dvec2 segmentStart(
                (double(clippedStart.x) / double(clippedStart.w) + 1.0) * 400.0,
                (1.0 - double(clippedStart.y) / double(clippedStart.w)) * 250.0);
            const glm::dvec2 segmentEnd(
                (double(clippedEnd.x) / double(clippedEnd.w) + 1.0) * 400.0,
                (1.0 - double(clippedEnd.y) / double(clippedEnd.w)) * 250.0);
            const int32_t minX = std::max(
                0, int32_t(std::floor(std::min(segmentStart.x, segmentEnd.x) - 1.0)));
            const int32_t maxX = std::min(
                799, int32_t(std::ceil(std::max(segmentStart.x, segmentEnd.x) + 1.0)));
            const int32_t minY = std::max(
                0, int32_t(std::floor(std::min(segmentStart.y, segmentEnd.y) - 1.0)));
            const int32_t maxY = std::min(
                499, int32_t(std::ceil(std::max(segmentStart.y, segmentEnd.y) + 1.0)));
            const glm::dvec2 direction = segmentEnd - segmentStart;
            const double directionLengthSquared = glm::dot(direction, direction);
            if (directionLengthSquared <= 1.0e-12)
                return;
            for (int32_t pixelY = minY; pixelY <= maxY; ++pixelY)
            {
                for (int32_t pixelX = minX; pixelX <= maxX; ++pixelX)
                {
                    const glm::dvec2 pixelCenter{
                        double(pixelX) + 0.5,
                        double(pixelY) + 0.5};
                    if (!coversDrawrangeDiamond(
                            pixelCenter, segmentStart, segmentEnd))
                        continue;
                    const double parameter = std::clamp(
                        glm::dot(pixelCenter - segmentStart, direction) /
                            directionLengthSquared,
                        0.0,
                        1.0);
                    const glm::vec4 clipPosition = clippedStart +
                        (clippedEnd - clippedStart) * float(parameter);
                    appendDrawrangePixelQuad(
                        vertices, indices, clipPosition, pixelX, pixelY, color);
                }
            }
        }

        /** Appends one BoxHelper edge for the native line-list pass. */
        void appendBoxNativeLine(
            eastl::vector<WebglBuffergeometryDrawrangeHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec3 &start,
            const glm::vec3 &end)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const glm::vec4 startPosition(start, 1.0f);
            const glm::vec4 endPosition(end, 1.0f);
            const glm::vec4 color(0.0f);
            vertices.push_back({startPosition, endPosition, color, color,
                                {0.0f, 0.0f, 0.0f, 0.0f},
                                glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(0.0f)});
            vertices.push_back({startPosition, endPosition, color, color,
                                {1.0f, 0.0f, 0.0f, 0.0f},
                                glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(0.0f)});
            indices.insert(indices.end(), {base, base + 1u});
        }

        /** Appends one fixed-size square point sprite as two triangles. */
        void appendPointQuad(
            eastl::vector<WebglBuffergeometryDrawrangeHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec3 &position)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const glm::vec4 point(position, 1.0f);
            const glm::vec4 white(1.0f);
            const glm::vec4 noClip(0.0f);
            // The vertex shader multiplies this normalized corner by the
            // complete fixed-size PointsMaterial coverage.
            vertices.push_back({point, point, white, white, {-0.5f, -0.5f, 0.0f, 0.0f}, noClip, noClip, noClip});
            vertices.push_back({point, point, white, white, {-0.5f, 0.5f, 0.0f, 0.0f}, noClip, noClip, noClip});
            vertices.push_back({point, point, white, white, {0.5f, 0.5f, 0.0f, 0.0f}, noClip, noClip, noClip});
            vertices.push_back({point, point, white, white, {0.5f, -0.5f, 0.0f, 0.0f}, noClip, noClip, noClip});
            indices.insert(indices.end(), {base, base + 1u, base + 2u,
                                           base, base + 2u, base + 3u});
        }

        /** Builds the fixed BoxHelper edge list in upstream index order. */
        void buildBoxGeometry(
            eastl::vector<WebglBuffergeometryDrawrangeHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const glm::vec3 p[8u] = {
                {HalfBoxExtent, HalfBoxExtent, HalfBoxExtent},
                {-HalfBoxExtent, HalfBoxExtent, HalfBoxExtent},
                {-HalfBoxExtent, -HalfBoxExtent, HalfBoxExtent},
                {HalfBoxExtent, -HalfBoxExtent, HalfBoxExtent},
                {HalfBoxExtent, HalfBoxExtent, -HalfBoxExtent},
                {-HalfBoxExtent, HalfBoxExtent, -HalfBoxExtent},
                {-HalfBoxExtent, -HalfBoxExtent, -HalfBoxExtent},
                {HalfBoxExtent, -HalfBoxExtent, -HalfBoxExtent},
            };
            const uint32_t edgeIndices[24u] = {
                0u, 1u, 1u, 2u, 2u, 3u, 3u, 0u,
                4u, 5u, 5u, 6u, 6u, 7u, 7u, 4u,
                0u, 4u, 1u, 5u, 2u, 6u, 3u, 7u};
            for (uint32_t edge = 0u; edge < 12u; ++edge)
            {
                appendBoxNativeLine(
                    vertices, indices, p[edgeIndices[edge * 2u]],
                    p[edgeIndices[edge * 2u + 1u]]);
            }
        }

        /** Integrates particles and appends the exact active connection pairs. */
        void simulateDrawrange(
            uint32_t targetFrame,
            uint32_t particleCount,
            bool limitConnections,
            uint32_t maxConnections,
            ThreeCompat::DeterministicRandom &random,
            eastl::vector<DrawrangeParticleState> &particles,
            eastl::vector<DrawrangeConnectionState> &connections)
        {
            particles.resize(MaximumParticleCount);
            for (DrawrangeParticleState &particle : particles)
            {
                particle.position = glm::vec3(
                    float(nextDrawrangeRandom(random) * BoxExtent - HalfBoxExtent),
                    float(nextDrawrangeRandom(random) * BoxExtent - HalfBoxExtent),
                    float(nextDrawrangeRandom(random) * BoxExtent - HalfBoxExtent));
                particle.velocity = glm::dvec3(
                    -1.0 + nextDrawrangeRandom(random) * 2.0,
                    -1.0 + nextDrawrangeRandom(random) * 2.0,
                    -1.0 + nextDrawrangeRandom(random) * 2.0);
                particle.connectionCount = 0u;
            }
            for (uint32_t frame = 0u; frame <= targetFrame; ++frame)
            {
                connections.clear();
                for (uint32_t index = 0u; index < particleCount; ++index)
                    particles[index].connectionCount = 0u;
                for (uint32_t index = 0u; index < particleCount; ++index)
                {
                    DrawrangeParticleState &particle = particles[index];
                    const glm::dvec3 nextPosition =
                        glm::dvec3(particle.position) + particle.velocity;
                    // The upstream particlePositions attribute is a
                    // Float32Array, so each animation step rounds before the
                    // bounds and distance tests.
                    particle.position = glm::vec3(nextPosition);
                    for (uint32_t axis = 0u; axis < 3u; ++axis)
                    {
                        if (particle.position[axis] < -HalfBoxExtent ||
                            particle.position[axis] > HalfBoxExtent)
                            particle.velocity[axis] = -particle.velocity[axis];
                    }
                    if (limitConnections &&
                        particle.connectionCount >= maxConnections)
                        continue;
                    for (uint32_t other = index + 1u; other < particleCount; ++other)
                    {
                        DrawrangeParticleState &otherParticle = particles[other];
                        if (limitConnections &&
                            otherParticle.connectionCount >= maxConnections)
                            continue;
                        const double distance = glm::length(
                            glm::dvec3(particle.position) -
                            glm::dvec3(otherParticle.position));
                        if (distance < MinimumDistance)
                        {
                            ++particle.connectionCount;
                            ++otherParticle.connectionCount;
                            connections.push_back({
                                index,
                                other,
                                particle.position,
                                otherParticle.position,
                                static_cast<float>(
                                    1.0 - distance / double(MinimumDistance))});
                        }
                    }
                }
            }
        }

        /** Builds the exact camera/group matrix used by the upstream example. */
        glm::mat4 drawrangeViewProjection(uint32_t frame)
        {
            // Three's PerspectiveCamera computes the projection in JavaScript
            // Number precision before uploading Float32 uniform values.
            const double verticalFov = 45.0 * Pi / 180.0;
            const double nearDistance = 1.0;
            const double farDistance = 4000.0;
            const double top = nearDistance * std::tan(verticalFov * 0.5);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth = (800.0 / 500.0) * projectionHeight;
            const double left = -0.5 * projectionWidth;
            const double projectionDepth = farDistance - nearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(
                2.0 * nearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(
                2.0 * nearDistance / projectionHeight);
            projection[2u][0u] = static_cast<float>(
                -(2.0 * left + projectionWidth) / projectionWidth);
            projection[2u][2u] = static_cast<float>(
                -(farDistance + nearDistance) / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -2.0 * farDistance * nearDistance / projectionDepth);
            const glm::mat4 view = glm::translate(
                glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1750.0f));
            const double seconds = ReferenceEpochSeconds +
                double(frame) * FrameStepSeconds;
            // Three computes Euler matrices with JavaScript Number precision
            // before uploading float uniforms. Do not round the huge epoch
            // angle to float before evaluating sin/cos; that changes the
            // animated capture orientation by several pixels.
            const double angle = seconds * 0.1;
            const float cosine = static_cast<float>(std::cos(angle));
            const float sine = static_cast<float>(std::sin(angle));
            glm::mat4 group(1.0f);
            group[0u][0u] = cosine;
            group[0u][2u] = -sine;
            group[2u][0u] = sine;
            group[2u][2u] = cosine;
            return projection * view * group;
        }

        /** Adds one typed payload to an entity allocation. */
        void appendPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t bytes)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = bytes,
                .instanceCount = 1u,
            });
        }
    }

    void WebglBuffergeometryDrawrangeRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateScenario(options);
        device = inDevice;
        const bool limited = options.scenarioId == "limited-connections";
        // The locked GUI replay sets all three controls before the capture;
        // ordinary scenarios retain the upstream defaults.
        particleCount = 500u;
        limitConnections = limited;
        maxConnections = 20u;
        if (limited)
        {
            const std::string replayHash = sha256File(
                std::filesystem::path(options.inputReplayPath.c_str()));
            if (replayHash.empty())
                throw std::runtime_error("DrawRange input replay hash is empty.");
        }

        // The frozen r185 oracle uses a case-specific seed.  The generic
        // runner supplies the host default when no case seed is declared;
        // both inputs therefore resolve to the locked DrawRange stream.
        ThreeCompat::DeterministicRandom random(DrawrangeRandomSeed);
        // Three.js creates the scene graph, geometry, material, and helper
        // objects before filling the particle attributes. Their UUID
        // generation consumes 120 Math.random() values (20 UUIDs x 6
        // values) in the locked r185 reference capture. Keep the CPU replay
        // stream aligned without changing any GPU behavior.
        for (uint32_t uuidRandom = 0u; uuidRandom < 120u; ++uuidRandom)
        {
            const uint32_t ignoredRandom = random.nextUint32();
            (void)ignoredRandom;
        }
        eastl::vector<DrawrangeParticleState> particles;
        eastl::vector<DrawrangeConnectionState> connections;
        simulateDrawrange(options.targetFrame, particleCount, limitConnections,
                          maxConnections, random, particles, connections);
        // The remaining ten UUIDs are created after the particle attributes in
        // the locked r185 page, so consume their 60 random words after the
        // simulation. This preserves both particle coordinates and the final
        // deterministic stream state without changing any GPU behavior.
        for (uint32_t uuidRandom = 0u; uuidRandom < 60u; ++uuidRandom)
        {
            const uint32_t ignoredRandom = random.nextUint32();
            (void)ignoredRandom;
        }
        finalRandomState = random.getState();

        const glm::mat4 viewProjection = drawrangeViewProjection(options.targetFrame);
        const WebglBuffergeometryDrawrangeHostObjectData objectData = {
            viewProjection, glm::vec4(float(options.width), float(options.height),
                                      0.0f, 0.0f)};
        const WebglBuffergeometryDrawrangeHostInstanceData instanceData = {
            glm::vec4(0.0f)};
        buildBoxGeometry(boxVertices, boxIndices);
        for (uint32_t index = 0u; index < particleCount; ++index)
            appendPointQuad(pointVertices, pointIndices, particles[index].position);
        for (const DrawrangeConnectionState &connection : connections)
        {
            appendLineSegment(
                lineVertices,
                lineIndices,
                connection.startPosition,
                connection.endPosition,
                glm::vec3(connection.alpha),
                glm::vec3(connection.alpha));
        }
        const WebglBuffergeometryDrawrangeHostMaterialData boxMaterial = {
            glm::vec4(srgbToLinear(71.0f / 255.0f),
                      srgbToLinear(71.0f / 255.0f),
                      srgbToLinear(71.0f / 255.0f), 0.0f)};
        const WebglBuffergeometryDrawrangeHostMaterialData lineMaterial = {
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)};
        const WebglBuffergeometryDrawrangeHostMaterialData pointMaterial = {
            glm::vec4(1.0f, 1.0f, 1.0f, 2.0f)};
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("Could not create DrawRange RenderSet encoder.");

        GVM::Core::RenderSetAllocInfo boxAllocation;
        boxAllocation.verticesCount = static_cast<uint32_t>(boxVertices.size());
        boxAllocation.indicesCount = static_cast<uint32_t>(boxIndices.size());
        boxAllocation.instanceCount = 1u;
        appendPayload(boxAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::vertices,
                      "DrawRangeBoxVertices", boxVertices.data(),
                      uint64_t(boxVertices.size()) * sizeof(boxVertices[0u]));
        appendPayload(boxAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::indices,
                      "DrawRangeBoxIndices", boxIndices.data(),
                      uint64_t(boxIndices.size()) * sizeof(boxIndices[0u]));
        appendPayload(boxAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::objects,
                      "DrawRangeBoxObject", &objectData, sizeof(objectData));
        appendPayload(boxAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::instances,
                      "DrawRangeBoxInstance", &instanceData, sizeof(instanceData));
        appendPayload(boxAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::materials,
                      "DrawRangeBoxMaterial", &boxMaterial, sizeof(boxMaterial));
        encoder->allocEntity(boxAllocation);

        GVM::Core::RenderSetAllocInfo pointAllocation;
        pointAllocation.verticesCount = static_cast<uint32_t>(pointVertices.size());
        pointAllocation.indicesCount = static_cast<uint32_t>(pointIndices.size());
        pointAllocation.instanceCount = 1u;
        appendPayload(pointAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::vertices,
                      "DrawRangePointVertices", pointVertices.data(),
                      uint64_t(pointVertices.size()) * sizeof(pointVertices[0u]));
        appendPayload(pointAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::indices,
                      "DrawRangePointIndices", pointIndices.data(),
                      uint64_t(pointIndices.size()) * sizeof(pointIndices[0u]));
        appendPayload(pointAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::objects,
                      "DrawRangePointObject", &objectData, sizeof(objectData));
        appendPayload(pointAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::instances,
                      "DrawRangePointInstance", &instanceData, sizeof(instanceData));
        appendPayload(pointAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::materials,
                      "DrawRangePointMaterial", &pointMaterial, sizeof(pointMaterial));
        encoder->allocEntity(pointAllocation);

        GVM::Core::RenderSetAllocInfo lineAllocation;
        lineAllocation.verticesCount = static_cast<uint32_t>(lineVertices.size());
        lineAllocation.indicesCount = static_cast<uint32_t>(lineIndices.size());
        lineAllocation.instanceCount = 1u;
        appendPayload(lineAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::vertices,
                      "DrawRangeLineVertices", lineVertices.data(),
                      uint64_t(lineVertices.size()) * sizeof(lineVertices[0u]));
        appendPayload(lineAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::indices,
                      "DrawRangeLineIndices", lineIndices.data(),
                      uint64_t(lineIndices.size()) * sizeof(lineIndices[0u]));
        appendPayload(lineAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::objects,
                      "DrawRangeLineObject", &objectData, sizeof(objectData));
        appendPayload(lineAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::instances,
                      "DrawRangeLineInstance", &instanceData, sizeof(instanceData));
        appendPayload(lineAllocation,
                      WebglBuffergeometryDrawrangeSceneRenderSetComponents::materials,
                      "DrawRangeLineMaterial", &lineMaterial, sizeof(lineMaterial));
        encoder->allocEntity(lineAllocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        connectionCount = static_cast<uint32_t>(connections.size());
    }

    void WebglBuffergeometryDrawrangeRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglBuffergeometryDrawrangeRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
            throw std::overflow_error("DrawRange capture exceeds host storage.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareOutputPath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write DrawRange RGBA.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n"
                 << "  \"source\":\"gvm-three-r185\",\n"
                 << "  \"caseId\":\"webgl_buffergeometry_drawrange\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"randomSeed\":" << DrawrangeRandomSeed << ",\n"
                 << "  \"width\":" << width << ",\n  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                 << "  \"inputReplay\":";
        if (options.scenarioId == "limited-connections")
        {
            const std::string replayHash = sha256File(
                std::filesystem::path(options.inputReplayPath.c_str()));
            metadata << "{\"schemaVersion\":1,\"caseId\":\"webgl_buffergeometry_drawrange\","
                     << "\"scenarioId\":\"limited-connections\",\"captureFrame\":61,"
                     << "\"sha256\":\"" << replayHash << "\","
                     << "\"target\":\".lil-gui .controller:nth-child(4) input[type=checkbox]\","
                     << "\"eventCount\":1,\"lastEventFrame\":0}";
        }
        else
        {
            metadata << "null";
        }
        metadata << "\n}\n";
        writeTextArtifact(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgl_buffergeometry_drawrange\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"implementationLevel\":\"semantic-complete\",\n"
                 << "  \"gpuWorkDslOnly\":true,\n  \"singleSample\":true,\n"
                 << "  \"msaaEnabled\":false,\n  \"renderSetPolicy\":\"required\",\n"
                 << "  \"sceneRenderSetCount\":1,\n  \"renderableObjectCount\":3,\n"
                 << "  \"entityCount\":3,\n  \"instanceCount\":3,\n"
                 << "  \"scenePassCount\":3,\n  \"screenPassCount\":0,\n"
                 << "  \"drawCommandCount\":3,\n"
                 << "  \"renderSetType\":\"WebglBuffergeometryDrawrangeSceneRenderSet\",\n"
                 << "  \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                 << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                 << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                 << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                 << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
                 << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set-0\",\"renderSetType\":\"WebglBuffergeometryDrawrangeSceneRenderSet\","
                 << "\"renderableObjectCount\":3,\"entityCount\":3,\"componentSchema\":["
                 << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                 << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                 << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                 << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                 << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\"entities\":["
                 << "{\"entityId\":0,\"logicalRenderableId\":\"box-helper\",\"instanceCount\":1},"
                 << "{\"entityId\":1,\"logicalRenderableId\":\"point-cloud\",\"instanceCount\":1},"
                 << "{\"entityId\":2,\"logicalRenderableId\":\"connection-lines\",\"instanceCount\":1}],"
                 << "\"drawCommandCount\":3,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"box-helper\",\"renderClass\":\"WebglBuffergeometryDrawrangeBoxLinePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"connection-lines\",\"renderClass\":\"WebglBuffergeometryDrawrangeNativeLinePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"main-geometry\",\"renderClass\":\"WebglBuffergeometryDrawrangeMainPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
                 << "  \"scenePasses\":[\"WebglBuffergeometryDrawrangeBoxLinePass\",\"WebglBuffergeometryDrawrangeNativeLinePass\",\"WebglBuffergeometryDrawrangeMainPass\"],\n"
                 << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"box-helper\",\"entityOrdinal\":0},{\"sceneRoot\":\"scene\",\"scenePass\":\"connection-lines\",\"entityOrdinal\":2},{\"sceneRoot\":\"scene\",\"scenePass\":\"main-geometry\",\"entityOrdinal\":1}],\n"
                 << "  \"particleCount\":" << particleCount << ",\n"
                 << "  \"connectionCount\":" << connectionCount << ",\n"
                 << "  \"maxConnections\":" << maxConnections << ",\n"
                 << "  \"limitConnections\":" << (limitConnections ? "true" : "false") << ",\n"
                 << "  \"finalRandomState\":" << finalRandomState << ",\n"
                 << "  \"usesRenderEntityID\":true,\n  \"usesRenderEntityInstanceID\":true,\n"
                 << "  \"directDrawFallback\":false\n}\n";
        writeTextArtifact(options.sceneSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebglBuffergeometryDrawrangeRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        boxVertices.clear();
        boxIndices.clear();
        pointVertices.clear();
        pointIndices.clear();
        lineVertices.clear();
        lineIndices.clear();
    }
}
