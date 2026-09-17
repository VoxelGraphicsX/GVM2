#include "WebglInstancingRaycastRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t CanonicalAmount = 10u;
        constexpr uint32_t CanonicalInstanceCount = 1000u;
        constexpr uint32_t ReducedInstanceCount = 125u;
        constexpr uint32_t IcosahedronDetail = 3u;
        constexpr uint32_t IcosahedronVertexCount = 960u;
        constexpr uint32_t IcosahedronTriangleCount = 320u;
        constexpr uint32_t UpstreamRandomDrawsBeforeFirstHit = 136u;
        constexpr uint32_t UpstreamRandomDrawsBetweenCanonicalHits = 8u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *HitReplaySha256 = "8c5ebf0a5c4eb4b2dc08dfea7af73321aba540ac7e520f192e5e1ed80c355370";
        constexpr const char *ReducedCountReplaySha256 = "9e631bfee387b057ed1d7a46cc61a37a440b3eaeed71c47c9c6387c5ed5b90f5";
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        static_assert(sizeof(InstancingRaycastHostFloat4) == 16u);
        static_assert(sizeof(InstancingRaycastHostUint4) == 16u);
        static_assert(sizeof(InstancingRaycastHostVertex) == 32u);
        static_assert(sizeof(InstancingRaycastHostObjectData) == 96u);
        static_assert(sizeof(InstancingRaycastHostInstanceData) == 32u);
        static_assert(sizeof(InstancingRaycastHostMaterialData) == 48u);

        /** Creates parent directories for one explicitly requested capture artifact. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Reads one bounded file as exact bytes for immutable replay identity validation. */
        eastl::vector<uint8_t> readFileBytes(const std::filesystem::path &inputPath, const char *label)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open " + std::string(label) + ": " + inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(std::string(label) + " has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete " + std::string(label) + ".");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte sequence. */
        eastl::string calculateSha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Resolves one explicit replay path without consulting environment configuration. */
        std::filesystem::path resolveReplayPath(const ThreeSampleHostOptions &options)
        {
            const std::filesystem::path requested(options.inputReplayPath.c_str());
            if (requested.is_absolute() && std::filesystem::is_regular_file(requested))
            {
                return requested;
            }
            if (!requested.empty() && std::filesystem::is_regular_file(requested))
            {
                return std::filesystem::absolute(requested);
            }
            if (!options.assetRoot.empty())
            {
                const std::filesystem::path assetPath = std::filesystem::path(options.assetRoot.c_str()) / requested;
                if (std::filesystem::is_regular_file(assetPath))
                {
                    return assetPath;
                }
            }
            throw std::invalid_argument("webgl_instancing_raycast could not resolve its explicit --input-replay.");
        }

        /** Builds and validates the initial, ray-hit, or active-count scenario contract. */
        InstancingRaycastScenarioState makeScenarioState(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_instancing_raycast")
            {
                throw std::invalid_argument("Instancing-raycast adapter requires case-id webgl_instancing_raycast.");
            }
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool rayHit = options.scenarioId == "ray-hit" && options.targetFrame == 60u;
            const bool reducedCount = options.scenarioId == "reduced-count" && options.targetFrame == 1u;
            if (!initial && !rayHit && !reducedCount)
            {
                throw std::invalid_argument(
                    "webgl_instancing_raycast requires initial/frame 0, ray-hit/frame 60, "
                    "or reduced-count/frame 1.");
            }
            if (initial && !options.inputReplayPath.empty())
            {
                throw std::invalid_argument("The initial instancing-raycast scenario must not consume input replay.");
            }
            if (!initial && options.inputReplayPath.empty())
            {
                throw std::invalid_argument("Interactive instancing-raycast scenarios require --input-replay.");
            }

            InstancingRaycastScenarioState state;
            if (initial)
            {
                return state;
            }

            const eastl::vector<uint8_t> replayBytes = readFileBytes(resolveReplayPath(options), "instancing-raycast input replay");
            state.replaySha256 = calculateSha256(replayBytes);
            const char *expectedDigest = rayHit ? HitReplaySha256 : ReducedCountReplaySha256;
            if (state.replaySha256 != expectedDigest)
            {
                throw std::invalid_argument("Instancing-raycast replay SHA-256 differs from the locked sequence.");
            }
            state.replayTarget = "body > canvas";
            state.replayEventCount = rayHit ? 3u : 2u;
            state.replayLastEventFrame = 0u;
            state.usesInputReplay = true;
            state.usesRaycast = rayHit;
            state.requiresEntityReallocation = reducedCount;
            state.activeInstanceCount = reducedCount ? ReducedInstanceCount : CanonicalInstanceCount;
            return state;
        }

        /** Appends one normalized r185 polyhedron vertex with reflected GVM Y coordinates. */
        void appendIcosahedronVertex(eastl::vector<InstancingRaycastHostVertex> &vertices, const glm::dvec3 &unscaledPosition)
        {
            const glm::dvec3 normalizedPosition = glm::normalize(unscaledPosition) * 0.5;
            const glm::vec3 threePosition(static_cast<float>(normalizedPosition.x), static_cast<float>(normalizedPosition.y), static_cast<float>(normalizedPosition.z));
            const glm::dvec3 normalizedNormal = glm::normalize(glm::dvec3(threePosition));
            const glm::vec3 threeNormal(static_cast<float>(normalizedNormal.x), static_cast<float>(normalizedNormal.y), static_cast<float>(normalizedNormal.z));
            vertices.push_back({
                .position = {threePosition.x, -threePosition.y, threePosition.z, 1.0f},
                .normal = {threeNormal.x, -threeNormal.y, threeNormal.z, 0.0f},
            });
        }

        /** Builds Three r185's exact radius-0.5 detail-3 non-indexed IcosahedronGeometry. */
        void buildIcosahedronGeometry(eastl::vector<InstancingRaycastHostVertex> &vertices, eastl::vector<uint32_t> &indices)
        {
            const double goldenRatio = (1.0 + std::sqrt(5.0)) * 0.5;
            const eastl::array<glm::dvec3, 12u> baseVertices = {
                glm::dvec3(-1.0, goldenRatio, 0.0),
                glm::dvec3(1.0, goldenRatio, 0.0),
                glm::dvec3(-1.0, -goldenRatio, 0.0),
                glm::dvec3(1.0, -goldenRatio, 0.0),
                glm::dvec3(0.0, -1.0, goldenRatio),
                glm::dvec3(0.0, 1.0, goldenRatio),
                glm::dvec3(0.0, -1.0, -goldenRatio),
                glm::dvec3(0.0, 1.0, -goldenRatio),
                glm::dvec3(goldenRatio, 0.0, -1.0),
                glm::dvec3(goldenRatio, 0.0, 1.0),
                glm::dvec3(-goldenRatio, 0.0, -1.0),
                glm::dvec3(-goldenRatio, 0.0, 1.0),
            };
            constexpr eastl::array<uint32_t, 60u> BaseIndices = {
                0u, 11u, 5u, 0u, 5u, 1u, 0u, 1u, 7u, 0u, 7u, 10u, 0u, 10u, 11u, 1u, 5u, 9u, 5u, 11u, 4u, 11u, 10u, 2u, 10u, 7u, 6u, 7u, 1u, 8u, 3u, 9u, 4u, 3u, 4u, 2u, 3u, 2u, 6u, 3u, 6u, 8u, 3u, 8u, 9u, 4u, 9u, 5u, 2u, 4u, 11u, 6u, 2u, 10u, 8u, 6u, 7u, 9u, 8u, 1u,
            };
            constexpr uint32_t Columns = IcosahedronDetail + 1u;

            vertices.clear();
            indices.clear();
            vertices.reserve(IcosahedronVertexCount);
            indices.reserve(IcosahedronVertexCount);
            for (size_t faceOffset = 0u; faceOffset < BaseIndices.size(); faceOffset += 3u)
            {
                const glm::dvec3 a = baseVertices[BaseIndices[faceOffset]];
                const glm::dvec3 b = baseVertices[BaseIndices[faceOffset + 1u]];
                const glm::dvec3 c = baseVertices[BaseIndices[faceOffset + 2u]];
                glm::dvec3 lattice[Columns + 1u][Columns + 1u] = {};
                for (uint32_t column = 0u; column <= Columns; ++column)
                {
                    const double columnAlpha = double(column) / double(Columns);
                    const glm::dvec3 aToC = a + (c - a) * columnAlpha;
                    const glm::dvec3 bToC = b + (c - b) * columnAlpha;
                    const uint32_t rows = Columns - column;
                    for (uint32_t row = 0u; row <= rows; ++row)
                    {
                        if (row == 0u && column == Columns)
                        {
                            lattice[column][row] = aToC;
                        }
                        else
                        {
                            const double rowAlpha = double(row) / double(rows);
                            lattice[column][row] = aToC + (bToC - aToC) * rowAlpha;
                        }
                    }
                }
                for (uint32_t column = 0u; column < Columns; ++column)
                {
                    for (uint32_t rowTriangle = 0u; rowTriangle < 2u * (Columns - column) - 1u; ++rowTriangle)
                    {
                        const uint32_t row = rowTriangle / 2u;
                        if ((rowTriangle & 1u) == 0u)
                        {
                            appendIcosahedronVertex(vertices, lattice[column][row + 1u]);
                            appendIcosahedronVertex(vertices, lattice[column + 1u][row]);
                            appendIcosahedronVertex(vertices, lattice[column][row]);
                        }
                        else
                        {
                            appendIcosahedronVertex(vertices, lattice[column][row + 1u]);
                            appendIcosahedronVertex(vertices, lattice[column + 1u][row + 1u]);
                            appendIcosahedronVertex(vertices, lattice[column + 1u][row]);
                        }
                    }
                }
            }
            if (vertices.size() != IcosahedronVertexCount)
            {
                throw std::logic_error("Canonical detail-3 IcosahedronGeometry produced an invalid vertex count.");
            }
            for (uint32_t vertexIndex = 0u; vertexIndex < IcosahedronVertexCount; vertexIndex += 3u)
            {
                indices.push_back(vertexIndex);
                indices.push_back(vertexIndex + 2u);
                indices.push_back(vertexIndex + 1u);
            }
        }

        /** Builds the canonical x-major, y-middle, z-minor instance prefix used by InstancedMesh. */
        void buildCanonicalInstances(eastl::vector<InstancingRaycastHostInstanceData> &instances)
        {
            instances.clear();
            instances.reserve(CanonicalInstanceCount);
            constexpr float Offset = (float(CanonicalAmount) - 1.0f) * 0.5f;
            for (uint32_t x = 0u; x < CanonicalAmount; ++x)
            {
                for (uint32_t y = 0u; y < CanonicalAmount; ++y)
                {
                    for (uint32_t z = 0u; z < CanonicalAmount; ++z)
                    {
                        instances.push_back({
                            .translation =
                                {
                                    Offset - float(x),
                                    -(Offset - float(y)),
                                    Offset - float(z),
                                    1.0f,
                                },
                            .color = {1.0f, 1.0f, 1.0f, 1.0f},
                        });
                    }
                }
            }
        }

        /** Reproduces Three ColorManagement's sRGB-to-linear transfer for one 8-bit channel. */
        float srgbByteToLinear(uint32_t channel)
        {
            const double srgb = double(channel) / 255.0;
            const double linear = srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
            return static_cast<float>(linear);
        }

        /** Returns the exact Color.setHex result for the next deterministic Math.random value. */
        InstancingRaycastHostFloat4 nextInstanceColor(ThreeCompat::DeterministicRandom &random)
        {
            const uint32_t upperTwentyFourBits = random.nextUint32() >> 8u;
            const uint32_t hexadecimalColor = upperTwentyFourBits == 0u ? 0u : upperTwentyFourBits - 1u;
            return {
                srgbByteToLinear((hexadecimalColor >> 16u) & 0xffu),
                srgbByteToLinear((hexadecimalColor >> 8u) & 0xffu),
                srgbByteToLinear(hexadecimalColor & 0xffu),
                1.0f,
            };
        }

        /** Intersects one front-facing triangle using Three r185 Ray.intersectTriangle equations. */
        bool intersectFrontFacingTriangle(const glm::dvec3 &rayOrigin, const glm::dvec3 &rayDirection, const glm::dvec3 &a, const glm::dvec3 &b, const glm::dvec3 &c, double &distance)
        {
            const glm::dvec3 edge1 = b - a;
            const glm::dvec3 edge2 = c - a;
            const glm::dvec3 normal = glm::cross(edge1, edge2);
            double directionDotNormal = glm::dot(rayDirection, normal);
            if (directionDotNormal >= 0.0)
            {
                return false;
            }
            const double sign = -1.0;
            directionDotNormal = -directionDotNormal;
            const glm::dvec3 difference = rayOrigin - a;
            const double directionDotDifferenceCrossEdge2 = sign * glm::dot(rayDirection, glm::cross(difference, edge2));
            if (directionDotDifferenceCrossEdge2 < 0.0)
            {
                return false;
            }
            const double directionDotEdge1CrossDifference = sign * glm::dot(rayDirection, glm::cross(edge1, difference));
            if (directionDotEdge1CrossDifference < 0.0 || directionDotDifferenceCrossEdge2 + directionDotEdge1CrossDifference > directionDotNormal)
            {
                return false;
            }
            const double rayNumerator = -sign * glm::dot(difference, normal);
            if (rayNumerator < 0.0)
            {
                return false;
            }
            distance = rayNumerator / directionDotNormal;
            return true;
        }

        /** Returns the nearest canonical instance hit by the center-camera ray, or UINT32_MAX. */
        uint32_t raycastCanonicalInstances(const eastl::vector<InstancingRaycastHostVertex> &vertices, const eastl::vector<InstancingRaycastHostInstanceData> &instances, const glm::dvec3 &cameraPosition, uint32_t activeInstanceCount)
        {
            const glm::dvec3 rayDirection = glm::normalize(-cameraPosition);
            double closestDistance = std::numeric_limits<double>::infinity();
            uint32_t closestInstance = UINT32_MAX;
            for (uint32_t instanceIndex = 0u; instanceIndex < activeInstanceCount; ++instanceIndex)
            {
                const InstancingRaycastHostFloat4 &translation = instances[instanceIndex].translation;
                const glm::dvec3 threeTranslation(translation.x, -translation.y, translation.z);
                const glm::dvec3 localRayOrigin = cameraPosition - threeTranslation;
                for (uint32_t vertexIndex = 0u; vertexIndex < IcosahedronVertexCount; vertexIndex += 3u)
                {
                    const InstancingRaycastHostFloat4 &hostA = vertices[vertexIndex].position;
                    const InstancingRaycastHostFloat4 &hostB = vertices[vertexIndex + 1u].position;
                    const InstancingRaycastHostFloat4 &hostC = vertices[vertexIndex + 2u].position;
                    double distance = 0.0;
                    if (intersectFrontFacingTriangle(localRayOrigin, rayDirection, glm::dvec3(hostA.x, -hostA.y, hostA.z), glm::dvec3(hostB.x, -hostB.y, hostB.z), glm::dvec3(hostC.x, -hostC.y, hostC.z), distance) && distance < closestDistance)
                    {
                        closestDistance = distance;
                        closestInstance = instanceIndex;
                    }
                }
            }
            return closestInstance;
        }

        /** Simulates the locked OrbitControls damping and CPU raycast sequence through frame 60. */
        void simulateRayHitScenario(uint32_t randomSeed, const eastl::vector<InstancingRaycastHostVertex> &vertices, const eastl::vector<InstancingRaycastHostInstanceData> &instances, InstancingRaycastScenarioState &state, eastl::vector<uint32_t> &hitInstanceIds, eastl::vector<uint32_t> &hitFrames, eastl::vector<InstancingRaycastHostFloat4> &hitColors)
        {
            const double radius = std::sqrt(300.0);
            double theta = Pi * 0.25;
            double phi = std::acos(1.0 / std::sqrt(3.0));
            double thetaDelta = -2.0 * Pi * 10.0 / 500.0;
            double phiDelta = 2.0 * Pi * 5.0 / 500.0;
            constexpr double DampingFactor = 0.05;
            constexpr double DampingRetention = 1.0 - DampingFactor;
            ThreeCompat::DeterministicRandom random(randomSeed);

            hitInstanceIds.clear();
            hitFrames.clear();
            hitColors.clear();
            for (uint32_t upstreamInitializationDraw = 0u; upstreamInitializationDraw < UpstreamRandomDrawsBeforeFirstHit; ++upstreamInitializationDraw)
            {
                (void)random.nextUint32();
            }
            theta += thetaDelta * DampingFactor;
            phi += phiDelta * DampingFactor;
            thetaDelta *= DampingRetention;
            phiDelta *= DampingRetention;
            for (uint32_t frameIndex = 0u; frameIndex <= 60u; ++frameIndex)
            {
                theta += thetaDelta * DampingFactor;
                phi += phiDelta * DampingFactor;
                thetaDelta *= DampingRetention;
                phiDelta *= DampingRetention;
                const glm::dvec3 cameraPosition(radius * std::sin(phi) * std::sin(theta), radius * std::cos(phi), radius * std::sin(phi) * std::cos(theta));
                const uint32_t hitInstance = raycastCanonicalInstances(vertices, instances, cameraPosition, CanonicalInstanceCount);
                if (hitInstance != UINT32_MAX && eastl::find(hitInstanceIds.begin(), hitInstanceIds.end(), hitInstance) == hitInstanceIds.end())
                {
                    if (hitInstanceIds.size() == 1u)
                    {
                        for (uint32_t firstRenderInitializationDraw = 0u; firstRenderInitializationDraw < UpstreamRandomDrawsBetweenCanonicalHits; ++firstRenderInitializationDraw)
                        {
                            (void)random.nextUint32();
                        }
                    }
                    hitInstanceIds.push_back(hitInstance);
                    hitFrames.push_back(frameIndex);
                    hitColors.push_back(nextInstanceColor(random));
                }
                if (frameIndex == 60u)
                {
                    state.cameraX = cameraPosition.x;
                    state.cameraY = cameraPosition.y;
                    state.cameraZ = cameraPosition.z;
                }
            }
            if (hitInstanceIds.size() != 2u || hitInstanceIds[0u] != 0u || hitInstanceIds[1u] != 110u || hitFrames[0u] != 0u || hitFrames[1u] != 13u)
            {
                throw std::runtime_error("Canonical center ray did not reproduce the locked r185 instance-hit sequence.");
            }
        }

        /** Builds Three's symmetric negative-one-to-one perspective projection with double intermediates. */
        glm::mat4 makePerspectiveProjection(uint32_t width, uint32_t height)
        {
            if (width == 0u || height == 0u)
            {
                throw std::invalid_argument("Instancing-raycast capture dimensions must be positive.");
            }
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 100.0;
            const double top = NearDistance * std::tan(Pi / 6.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth = double(width) / double(height) * projectionHeight;
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(-(FarDistance + NearDistance) / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(-2.0 * FarDistance * NearDistance / depth);
            return projection;
        }

        /** Builds the reflected GVM camera view that preserves Three's final canvas orientation. */
        glm::mat4 makeCameraView(const InstancingRaycastScenarioState &state)
        {
            const glm::dvec3 position(state.cameraX, -state.cameraY, state.cameraZ);
            return glm::mat4(glm::lookAt(position, glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(0.0, 1.0, 0.0)));
        }

        /** Appends one typed payload to a RenderSet buffer component allocation. */
        void appendBufferPayload(GVM::Core::RenderSetAllocInfo &allocation, GVM::Core::RenderComponentHandle component, const char *name, const void *value, uint64_t byteCount, uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("Instancing-raycast RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebglInstancingRaycastRuntimeAdapter::initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        scenarioState = makeScenarioState(options);
        device = inDevice;
        buildIcosahedronGeometry(vertices, indices);
        buildCanonicalInstances(instances);
        if (scenarioState.usesRaycast)
        {
            simulateRayHitScenario(options.randomSeed, vertices, instances, scenarioState, raycastHitInstanceIds, raycastHitFrames, raycastHitColors);
        }

        objectData = {
            .viewProjection = makePerspectiveProjection(options.width, options.height) * makeCameraView(scenarioState),
            .hemisphereDirection = {0.0f, -1.0f, 0.0f, 0.0f},
            .materialAndFlags = {0u, 0u, 0u, 0u},
        };
        const float ground = srgbByteToLinear(0x88u) * 3.0f;
        materialData = {
            .baseColor = {1.0f, 1.0f, 1.0f, 1.0f},
            .skyIrradiance = {3.0f, 3.0f, 3.0f, 1.0f},
            .groundIrradiance = {ground, ground, ground, 1.0f},
        };

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Instancing-raycast case could not create its Scene RenderSet encoder.");
        }
        initialEntityIndex = allocateSceneEntity(*encoder, CanonicalInstanceCount);
        activeEntityIndex = initialEntityIndex;
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex WebglInstancingRaycastRuntimeAdapter::allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, uint32_t instanceCount) const
    {
        if (instanceCount == 0u || instanceCount > instances.size())
        {
            throw std::out_of_range("Instancing-raycast entity allocation has an invalid active instance count.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = instanceCount;
        appendBufferPayload(allocation, WebglInstancingRaycastSceneRenderSetComponents::vertices, "WebglInstancingRaycastVertices", vertices.data(), vertices.size() * sizeof(InstancingRaycastHostVertex), 1u);
        appendBufferPayload(allocation, WebglInstancingRaycastSceneRenderSetComponents::indices, "WebglInstancingRaycastIndices", indices.data(), indices.size() * sizeof(uint32_t), 1u);
        appendBufferPayload(allocation, WebglInstancingRaycastSceneRenderSetComponents::objects, "WebglInstancingRaycastObject", &objectData, sizeof(objectData), 1u);
        appendBufferPayload(allocation, WebglInstancingRaycastSceneRenderSetComponents::instances, instanceCount == ReducedInstanceCount ? "WebglInstancingRaycastInstances125" : "WebglInstancingRaycastInstances1000", instances.data(), uint64_t(instanceCount) * sizeof(InstancingRaycastHostInstanceData), instanceCount);
        appendBufferPayload(allocation, WebglInstancingRaycastSceneRenderSetComponents::materials, "WebglInstancingRaycastMaterial", &materialData, sizeof(materialData), 1u);
        return encoder.allocEntity(allocation);
    }

    void WebglInstancingRaycastRuntimeAdapter::reallocateReducedCountEntity(GVM::Core::AbstractRendererImpl &renderer)
    {
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Reduced-count scenario could not create its RenderSet encoder.");
        }
        encoder->removeEntity(activeEntityIndex);
        activeEntityIndex = allocateSceneEntity(*encoder, ReducedInstanceCount);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        entityReallocated = true;
    }

    void WebglInstancingRaycastRuntimeAdapter::updateHitInstance(GVM::Core::AbstractRendererImpl &renderer, uint32_t instanceIndex)
    {
        if (appliedHitUpdateCount >= raycastHitColors.size() || instanceIndex >= instances.size())
        {
            throw std::out_of_range("Instancing-raycast scheduled instance update is invalid.");
        }
        instances[instanceIndex].color = raycastHitColors[appliedHitUpdateCount];
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Ray-hit scenario could not create its RenderSet update encoder.");
        }
        encoder->setBufferComponentData(activeEntityIndex, WebglInstancingRaycastSceneRenderSetComponents::instances, &instances[instanceIndex], sizeof(InstancingRaycastHostInstanceData), instanceIndex, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        ++appliedHitUpdateCount;
    }

    void WebglInstancingRaycastRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        (void)options;
        if (scenarioState.requiresEntityReallocation && frameIndex == 0u && !entityReallocated)
        {
            reallocateReducedCountEntity(renderer);
        }
        while (scenarioState.usesRaycast && appliedHitUpdateCount < raycastHitFrames.size() && raycastHitFrames[appliedHitUpdateCount] == frameIndex)
        {
            updateHitInstance(renderer, raycastHitInstanceIds[appliedHitUpdateCount]);
        }
    }

    void WebglInstancingRaycastRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computeRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("Instancing-raycast capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglInstancingRaycastRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglInstancingRaycastRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open instancing-raycast RGBA output path.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete instancing-raycast RGBA capture.");
        }
    }

    void WebglInstancingRaycastRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open instancing-raycast metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_instancing_raycast\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"inputReplay\": ";
        if (!scenarioState.usesInputReplay)
        {
            output << "null\n";
        }
        else
        {
            output << "{\n"
                   << "    \"schemaVersion\": 1,\n"
                   << "    \"sha256\": \"" << scenarioState.replaySha256.c_str() << "\",\n"
                   << "    \"caseId\": \"webgl_instancing_raycast\",\n"
                   << "    \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "    \"captureFrame\": " << options.targetFrame << ",\n"
                   << "    \"eventCount\": " << scenarioState.replayEventCount << ",\n"
                   << "    \"lastEventFrame\": " << scenarioState.replayLastEventFrame << ",\n"
                   << "    \"target\": \"" << scenarioState.replayTarget.c_str() << "\"\n"
                   << "  }\n";
        }
        output << "}\n";
    }

    void WebglInstancingRaycastRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open instancing-raycast snapshot output path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_instancing_raycast\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"entityCount\": 1,\n"
               << "  \"instanceCount\": " << scenarioState.activeInstanceCount << ",\n"
               << "  \"containsInstancing\": true,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"screenPasses\": [],\n"
               << "  \"scenePassSequence\": [{\"sceneRoot\": \"scene\", \"scenePass\": \"main-lit\", \"entityOrdinal\": 0}],\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"antialiasResolve\": \"disabled-single-sample\",\n"
               << "  \"icosahedronRadius\": 0.5,\n"
               << "  \"icosahedronDetail\": " << IcosahedronDetail << ",\n"
               << "  \"vertexCount\": " << vertices.size() << ",\n"
               << "  \"indexCount\": " << indices.size() << ",\n"
               << "  \"triangleCount\": " << IcosahedronTriangleCount << ",\n"
               << "  \"cameraFovDegrees\": 60,\n"
               << "  \"cameraNear\": 0.10000000000000001,\n"
               << "  \"cameraFar\": 100,\n"
               << "  \"cameraPosition\": [" << scenarioState.cameraX << ", " << scenarioState.cameraY << ", " << scenarioState.cameraZ << "],\n"
               << "  \"activeCountMutation\": \"" << (scenarioState.requiresEntityReallocation ? "remove-reallocate-same-render-set" : "none") << "\",\n"
               << "  \"initialEntityId\": " << initialEntityIndex << ",\n"
               << "  \"activeEntityId\": " << activeEntityIndex << ",\n"
               << "  \"entityReallocated\": " << (entityReallocated ? "true" : "false") << ",\n"
               << "  \"instanceComponentUpdateCount\": " << appliedHitUpdateCount << ",\n"
               << "  \"raycastHitInstanceIds\": [";
        for (size_t index = 0u; index < raycastHitInstanceIds.size(); ++index)
        {
            if (index != 0u)
            {
                output << ", ";
            }
            output << raycastHitInstanceIds[index];
        }
        output << "],\n"
               << "  \"raycastHitFrames\": [";
        for (size_t index = 0u; index < raycastHitFrames.size(); ++index)
        {
            if (index != 0u)
            {
                output << ", ";
            }
            output << raycastHitFrames[index];
        }
        output << "],\n"
               << "  \"inputReplayEventCount\": " << scenarioState.replayEventCount << ",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sceneRoots\": [\n"
               << "    {\n"
               << "      \"id\": \"scene\",\n"
               << "      \"renderSetCount\": 1,\n"
               << "      \"renderSetId\": \"scene\",\n"
               << "      \"renderSetType\": \"WebglInstancingRaycastSceneRenderSet\",\n"
               << "      \"renderableObjectCount\": 1,\n"
               << "      \"entityCount\": 1,\n"
               << "      \"entities\": [\n"
               << "        {\"entityId\": " << activeEntityIndex << ", \"logicalRenderableId\": \"instanced-icosahedra\", \"instanceCount\": " << scenarioState.activeInstanceCount << "}\n"
               << "      ],\n"
               << "      \"componentSchema\": [\n"
               << "        {\"name\": \"vertices\", \"kind\": \"buffer\", \"role\": \"vertex\"},\n"
               << "        {\"name\": \"indices\", \"kind\": \"buffer\", \"role\": \"index\"},\n"
               << "        {\"name\": \"objects\", \"kind\": \"buffer\", \"role\": \"object\"},\n"
               << "        {\"name\": \"instances\", \"kind\": \"buffer\", \"role\": \"instance\"},\n"
               << "        {\"name\": \"materials\", \"kind\": \"buffer\", \"role\": \"material\"}\n"
               << "      ],\n"
               << "      \"drawCommandCount\": 1,\n"
               << "      \"directDrawFallback\": false,\n"
               << "      \"scenePasses\": [\n"
               << "        {\"name\": \"main-lit\", \"renderClass\": \"WebglInstancingRaycastMainPass\", "
                  "\"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, "
                  "\"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 1, "
                  "\"drawCommandCount\": 1, \"usesStandaloneGeometry\": false, "
                  "\"usesExplicitDrawCount\": false}\n"
               << "      ]\n"
               << "    }\n"
               << "  ]\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
