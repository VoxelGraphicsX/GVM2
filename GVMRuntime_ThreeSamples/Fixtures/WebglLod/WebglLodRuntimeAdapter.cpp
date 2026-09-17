#include "WebglLodRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/string.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t LodRootCount = 1000u;
        constexpr uint32_t LodLevelCount = 5u;
        // Match the frozen r185 capture seed used by the reference harness.
        constexpr uint32_t LodRandomSeed = 0x12345678u;
        constexpr uint32_t LodDetailByLevel[LodLevelCount] = {16u, 8u, 4u, 2u, 1u};
        constexpr float LodDistanceByLevel[LodLevelCount] = {50.0f, 300.0f, 1000.0f, 2000.0f, 8000.0f};
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        // FlyControls applies a normalized incremental quaternion whose z
        // component is `delta * rollSpeed`; the resulting camera angle is
        // therefore twice the scalar roll over the fixed replay interval.
        constexpr double FlyRollFactor = 1.0;

        /** Creates parent directories for one LOD capture artifact. */
        void prepareLodPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Advances the deterministic LOD object random stream. */
        uint32_t nextLodRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return state;
        }

        /** Returns a deterministic unit interval sample. */
        float lodRandom(uint32_t &state)
        {
            return float(nextLodRandom(state) >> 8u) / 16777216.0f;
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendLodBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Appends one smooth r185 polyhedron vertex at the requested radius. */
        void appendLodSolidVertex(
            const glm::dvec3 &unscaledPosition,
            eastl::vector<glm::vec4> &positions,
            eastl::vector<glm::vec4> &normals)
        {
            const glm::dvec3 normalized = glm::normalize(unscaledPosition);
            const glm::vec3 position(static_cast<float>(normalized.x * 100.0),
                                     static_cast<float>(normalized.y * 100.0),
                                     static_cast<float>(normalized.z * 100.0));
            positions.push_back(glm::vec4(position, 1.0f));
            normals.push_back(glm::vec4(glm::normalize(position), 0.0f));
        }

        /** Appends one one-pixel screen-space line envelope to the shared triangle stream. */
        void appendLodWireSegment(
            const glm::vec4 &start,
            const glm::vec4 &end,
            const glm::vec4 &startNormal,
            const glm::vec4 &endNormal,
            eastl::vector<WebglLodHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            // The four corners carry endpoint and signed transverse offsets;
            // the DSL resolves the offset in framebuffer pixels after the
            // perspective divide. This keeps line clipping and coverage
            // deterministic on Metal and Vulkan without relying on backend
            // native LineList rasterization differences.
            vertices.push_back({start, end, startNormal, endNormal, {0.0f, -0.75f, 0.0f, 0.0f}});
            vertices.push_back({start, end, startNormal, endNormal, {0.0f, 0.75f, 0.0f, 0.0f}});
            vertices.push_back({start, end, startNormal, endNormal, {1.0f, 0.75f, 0.0f, 0.0f}});
            vertices.push_back({start, end, startNormal, endNormal, {1.0f, -0.75f, 0.0f, 0.0f}});
            indices.insert(indices.end(), {base, base + 1u, base + 2u,
                                           base, base + 2u, base + 3u});
        }

        /** Builds Three r185's non-indexed IcosahedronGeometry wire stream. */
        void buildLodWireGeometry(
            uint32_t detail,
            eastl::vector<WebglLodHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const double goldenRatio = (1.0 + std::sqrt(5.0)) * 0.5;
            const glm::dvec3 baseVertices[] = {
                {-1.0, goldenRatio, 0.0}, {1.0, goldenRatio, 0.0},
                {-1.0, -goldenRatio, 0.0}, {1.0, -goldenRatio, 0.0},
                {0.0, -1.0, goldenRatio}, {0.0, 1.0, goldenRatio},
                {0.0, -1.0, -goldenRatio}, {0.0, 1.0, -goldenRatio},
                {goldenRatio, 0.0, -1.0}, {goldenRatio, 0.0, 1.0},
                {-goldenRatio, 0.0, -1.0}, {-goldenRatio, 0.0, 1.0},
            };
            constexpr uint32_t BaseIndices[] = {
                0u, 11u, 5u, 0u, 5u, 1u, 0u, 1u, 7u, 0u, 7u, 10u, 0u, 10u, 11u,
                1u, 5u, 9u, 5u, 11u, 4u, 11u, 10u, 2u, 10u, 7u, 6u, 7u, 1u, 8u,
                3u, 9u, 4u, 3u, 4u, 2u, 3u, 2u, 6u, 3u, 6u, 8u, 3u, 8u, 9u,
                4u, 9u, 5u, 2u, 4u, 11u, 6u, 2u, 10u, 8u, 6u, 7u, 9u, 8u, 1u,
            };
            const uint32_t columns = detail + 1u;
            vertices.clear();
            indices.clear();
            for (uint32_t faceOffset = 0u; faceOffset < sizeof(BaseIndices) / sizeof(BaseIndices[0]); faceOffset += 3u)
            {
                const glm::dvec3 a = baseVertices[BaseIndices[faceOffset]];
                const glm::dvec3 b = baseVertices[BaseIndices[faceOffset + 1u]];
                const glm::dvec3 c = baseVertices[BaseIndices[faceOffset + 2u]];
                eastl::vector<glm::dvec3> lattice;
                lattice.resize((columns + 1u) * (columns + 1u));
                const auto latticeAt = [columns, &lattice](uint32_t column, uint32_t row) -> glm::dvec3 & {
                    return lattice[column * (columns + 1u) + row];
                };
                for (uint32_t column = 0u; column <= columns; ++column)
                {
                    const double alpha = double(column) / double(columns);
                    const glm::dvec3 aToC = a + (c - a) * alpha;
                    const glm::dvec3 bToC = b + (c - b) * alpha;
                    const uint32_t rows = columns - column;
                    for (uint32_t row = 0u; row <= rows; ++row)
                    {
                        latticeAt(column, row) = row == 0u && column == columns
                            ? aToC
                            : aToC + (bToC - aToC) * (double(row) / double(rows));
                    }
                }
                for (uint32_t column = 0u; column < columns; ++column)
                {
                    for (uint32_t triangle = 0u; triangle < 2u * (columns - column) - 1u; ++triangle)
                    {
                        const uint32_t row = triangle / 2u;
                        const glm::dvec3 p0 = (triangle & 1u) == 0u
                            ? latticeAt(column, row + 1u) : latticeAt(column, row + 1u);
                        const glm::dvec3 p1 = (triangle & 1u) == 0u
                            ? latticeAt(column + 1u, row) : latticeAt(column + 1u, row + 1u);
                        const glm::dvec3 p2 = latticeAt(column, row);
                        const glm::dvec3 trianglePoints[3] = {p0, p1, p2};
                        const glm::dvec3 trianglePointsAlt[3] = {
                            p0, p1, (triangle & 1u) == 0u ? p2 : latticeAt(column + 1u, row)};
                        const glm::dvec3 *points = (triangle & 1u) == 0u ? trianglePoints : trianglePointsAlt;
                        glm::vec4 positions[3];
                        glm::vec4 normals[3];
                        for (uint32_t i = 0u; i < 3u; ++i)
                        {
                            eastl::vector<glm::vec4> temporaryPositions;
                            eastl::vector<glm::vec4> temporaryNormals;
                            appendLodSolidVertex(points[i], temporaryPositions, temporaryNormals);
                            positions[i] = temporaryPositions.front();
                            normals[i] = temporaryNormals.front();
                        }
                        appendLodWireSegment(positions[0], positions[1], normals[0], normals[1], vertices, indices);
                        appendLodWireSegment(positions[1], positions[2], normals[1], normals[2], vertices, indices);
                        appendLodWireSegment(positions[2], positions[0], normals[2], normals[0], vertices, indices);
                    }
                }
            }
        }

        /** Returns Three's selected LOD index for a camera distance. */
        uint32_t selectLodLevel(float distance)
        {
            // THREE.LOD stores each threshold on the level that starts at
            // that distance.  Level zero is active below the second level's
            // 300-unit threshold; level four becomes active only at 8000
            // units.  Comparing against the current level's distance would
            // incorrectly select the coarsest mesh for every object beyond
            // 2000 units.
            for (uint32_t level = 1u; level < LodLevelCount; ++level)
            {
                if (distance < LodDistanceByLevel[level]) return level - 1u;
            }
            return LodLevelCount - 1u;
        }

        /** Validates the locked r185 LOD scenario identifiers and capture extent. */
        void validateLodOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-lod-field" && options.targetFrame == 0u;
            const bool stable = options.scenarioId == "fixed-step-idle" && options.targetFrame == 60u;
            const bool input = options.scenarioId == "fly-controls" && options.targetFrame == 120u;
            if (options.caseId != "webgl_lod" || (!initial && !stable && !input) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != LodRandomSeed ||
                (input && options.inputReplayPath.empty()))
            {
                throw std::invalid_argument("webgl_lod scenario does not match the locked r185 contract.");
            }
        }

        /** Returns the SHA-256 digest used to bind a replay to its oracle. */
        std::string sha256LodReplay(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open webgl_lod FlyControls replay.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
                throw std::runtime_error("webgl_lod FlyControls replay is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
                throw std::runtime_error("Could not read webgl_lod FlyControls replay.");
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
    } // namespace

    void WebglLodRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateLodOptions(options);
        device = inDevice;
        flyControlsReplay = options.scenarioId == "fly-controls";
        cameraPosition = glm::vec3(0.0f, 0.0f, 1000.0f);
        cameraOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (flyControlsReplay)
            (void)sha256LodReplay(std::filesystem::path(options.inputReplayPath.c_str()));
        captureWidth = options.width;
        captureHeight = options.height;
        entities.clear();
        // Keep the five Mesh children represented as distinct RenderSet
        // entities, matching THREE.LOD's scene graph.  Only the selected
        // child owns a geometry allocation at a time; hidden children retain
        // their object/material/visibility components with zero draw counts.
        // This preserves the 5,000-entity contract without duplicating the
        // highest-detail wire stream for every hidden child.
        entities.resize(LodRootCount * LodLevelCount);
        uint32_t randomState = options.randomSeed;
        levelVertices.clear();
        levelIndices.clear();
        levelVertices.resize(LodLevelCount);
        levelIndices.resize(LodLevelCount);
        for (uint32_t level = 0u; level < LodLevelCount; ++level)
        {
            buildLodWireGeometry(LodDetailByLevel[level], levelVertices[level], levelIndices[level]);
        }
        // Three allocates UUIDs for the camera, scene, lights, geometries,
        // material, renderer-owned Object3D nodes, and the renderer/canvas
        // bookkeeping before the first LOD position.  The locked r185
        // capture consumes 152 seeded Math.random values before the first
        // position.  Twenty additional UUID words are consumed after the
        // roots are created by the browser-side renderer bookkeeping.
        for (uint32_t randomWord = 0u; randomWord < 152u; ++randomWord) nextLodRandom(randomState);
        for (uint32_t rootIndex = 0u; rootIndex < LodRootCount; ++rootIndex)
        {
            const glm::vec3 position(
                5000.0f - lodRandom(randomState) * 10000.0f,
                3750.0f - lodRandom(randomState) * 7500.0f,
                5000.0f - lodRandom(randomState) * 10000.0f);
            const float distance = glm::length(position - glm::vec3(0.0f, 0.0f, 1000.0f));
            const uint32_t selectedLevel = selectLodLevel(distance);
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), position) *
                glm::scale(glm::mat4(1.0f), glm::vec3(1.5f));
            for (uint32_t levelIndex = 0u; levelIndex < LodLevelCount; ++levelIndex)
            {
                WebglLodEntityData &entity = entities[rootIndex * LodLevelCount + levelIndex];
                entity.rootIndex = rootIndex;
                entity.levelIndex = levelIndex;
                entity.requestedLevelIndex = selectedLevel;
                entity.lodLevel = LodDetailByLevel[levelIndex];
                entity.visible = levelIndex == selectedLevel;
                entity.hasGeometry = false;
                entity.vertices.clear();
                entity.indices.clear();
                entity.model = model;
                entity.materialData.baseColor = glm::vec4(1.0f);
                entity.instanceData.reserved = glm::vec4(0.0f);
                entity.lodState = glm::uvec4(levelIndex, entity.visible ? 1u : 0u, rootIndex, 0u);
                entity.objectData.directionalLightDirectionAndIntensity = glm::vec4(
                    glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f)), 2.75f);
            }
            // LOD, five child Mesh nodes, and their matrix bookkeeping each
            // consume the same four-word UUID stream before the next root.
            for (uint32_t randomWord = 0u; randomWord < 24u; ++randomWord) nextLodRandom(randomState);
        }
        for (uint32_t randomWord = 0u; randomWord < 20u; ++randomWord) nextLodRandom(randomState);
        updateObjectData(options.width, options.height, options.targetFrame);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_lod could not create its Scene Set encoder.");
        for (uint32_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
        {
            WebglLodEntityData &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            const auto &vertices = levelVertices[entity.levelIndex];
            const auto &indices = levelIndices[entity.levelIndex];
            allocation.verticesCount = entity.visible ? static_cast<uint32_t>(vertices.size()) : 0u;
            allocation.indicesCount = entity.visible ? static_cast<uint32_t>(indices.size()) : 0u;
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("LodObject-") +
                eastl::to_string(entity.rootIndex) + "-level-" + eastl::to_string(entity.levelIndex);
            if (entity.visible)
            {
                appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::vertices,
                                prefix + "-vertices",
                                vertices.data(),
                                vertices.size() * sizeof(WebglLodHostVertex),
                                1u);
                appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::indices,
                                prefix + "-indices",
                                indices.data(),
                                indices.size() * sizeof(uint32_t),
                                1u);
            }
            appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::objects,
                            prefix + "-object", &entity.objectData, sizeof(entity.objectData), 1u);
            appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::instances,
                            prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::materials,
                            prefix + "-material", &entity.materialData, sizeof(entity.materialData), 1u);
            appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::lodState,
                            prefix + "-lod-state", &entity.lodState, sizeof(entity.lodState), 1u);
            entity.entityIndex = encoder->allocEntity(allocation);
            entity.hasGeometry = entity.visible;
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLodRuntimeAdapter::updateObjectData(uint32_t width, uint32_t height, uint32_t frameIndex)
    {
        if (flyControlsReplay)
        {
            const float elapsedSeconds = std::min(float(frameIndex), 120.0f) / 60.0f;
            // The locked replay holds W and Q from frame zero through frame
            // 120. FlyControls moves along local -Z at 1000 units/s and rolls
            // around local Z at PI/10 radians/s.
            cameraPosition = glm::vec3(
                0.0f, 0.0f, 1000.0f - 1000.0f * elapsedSeconds);
            // FlyControls composes a normalized quaternion whose z component
            // is `delta * rollSpeed` and w is one.  Replaying the fixed
            // 1/60-second clock therefore uses the exact per-frame angle
            // 2*atan(rollSpeed/60), rather than a half-angle shortcut.
            const double perFrameRoll = 2.0 * std::atan(
                (Pi * 0.1) / 60.0);
            cameraOrientation = glm::angleAxis(
                float(double(frameIndex) * perFrameRoll * FlyRollFactor),
                glm::vec3(0.0f, 0.0f, 1.0f));
        }
        const glm::mat4 cameraTransform = glm::translate(
            glm::mat4(1.0f), cameraPosition) * glm::toMat4(cameraOrientation);
        const glm::mat4 view = glm::inverse(cameraTransform);
        const glm::mat4 projection = glm::perspective(
            glm::radians(45.0f), float(width) / float(height), 1.0f, 15000.0f);
        const glm::vec3 viewDirectional = glm::normalize(
            glm::vec3(view * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
        const glm::vec3 viewPoint = glm::vec3(view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        for (WebglLodEntityData &entity : entities)
        {
            const glm::vec3 objectPosition = glm::vec3(entity.model[3]);
            const uint32_t selectedLevel = selectLodLevel(
                glm::length(objectPosition - cameraPosition));
            entity.requestedLevelIndex = selectedLevel;
            entity.visible = entity.levelIndex == selectedLevel;
            entity.lodState.y = entity.visible ? 1u : 0u;
            entity.objectData.modelView = view * entity.model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            // The DSL stores the half-extent used by its NDC-to-pixel mapping;
            // the generated raster position itself remains the full capture size.
            entity.objectData.viewport = glm::vec4(float(width) * 0.5f, float(height) * 0.5f, 0.0f, 0.0f);
            entity.objectData.directionalLightDirectionAndIntensity =
                glm::vec4(viewDirectional, 2.75f);
            entity.objectData.pointLightViewPositionAndIntensity =
                glm::vec4(viewPoint, 2.75f);
            entity.objectData.fogNearFar = glm::vec4(1.0f, 15000.0f, 0.0f, 0.0f);
        }
    }

    void WebglLodRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                                              const ThreeSampleHostOptions &options,
                                              uint32_t frameIndex)
    {
        updateObjectData(options.width, options.height, frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_lod could not create its update encoder.");
        bool hasStructuralChanges = false;
        for (WebglLodEntityData &entity : entities)
        {
            // The deterministic fly replay is initialized at its capture
            // frame, so its final geometry allocation already has the stable
            // scene traversal order.  Keep those allocations in place while
            // simulating intermediate camera states; replacing them every
            // frame would recycle RenderSet entity IDs and change opaque
            // wireframe tie ordering relative to Three's scene graph.
            // FlyControls changes the camera distance continuously.  LOD
            // children therefore have to be reallocated as their thresholds
            // are crossed; retaining only the initial allocations would mark
            // newly selected levels visible without supplying their geometry.
            const bool preserveCaptureAllocation = false;
            if (!preserveCaptureAllocation && entity.visible != entity.hasGeometry)
            {
                encoder->removeEntity(entity.entityIndex);
                const auto &vertices = levelVertices[entity.levelIndex];
                const auto &indices = levelIndices[entity.levelIndex];
                GVM::Core::RenderSetAllocInfo allocation;
                allocation.verticesCount = entity.visible ? static_cast<uint32_t>(vertices.size()) : 0u;
                allocation.indicesCount = entity.visible ? static_cast<uint32_t>(indices.size()) : 0u;
                allocation.instanceCount = 1u;
                const eastl::string prefix = eastl::string("LodObject-") +
                    eastl::to_string(entity.rootIndex) + "-level-" +
                    eastl::to_string(entity.levelIndex);
                if (entity.visible)
                {
                    appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::vertices,
                                    prefix + "-vertices", vertices.data(),
                                    vertices.size() * sizeof(WebglLodHostVertex), 1u);
                    appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::indices,
                                    prefix + "-indices", indices.data(),
                                    indices.size() * sizeof(uint32_t), 1u);
                }
                appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::objects,
                                prefix + "-object", &entity.objectData, sizeof(entity.objectData), 1u);
                appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::instances,
                                prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
                appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::materials,
                                prefix + "-material", &entity.materialData, sizeof(entity.materialData), 1u);
                appendLodBuffer(allocation, WebglLodSceneRenderSetComponents::lodState,
                                prefix + "-lod-state", &entity.lodState, sizeof(entity.lodState), 1u);
                entity.entityIndex = encoder->allocEntity(allocation);
                entity.hasGeometry = entity.visible;
                hasStructuralChanges = true;
            }
            else
            {
                encoder->setBufferComponentData(entity.entityIndex,
                    WebglLodSceneRenderSetComponents::objects,
                    &entity.objectData, sizeof(entity.objectData), 0u, 1u);
                encoder->setBufferComponentData(entity.entityIndex,
                    WebglLodSceneRenderSetComponents::lodState,
                    &entity.lodState, sizeof(entity.lodState), 0u, 1u);
            }
        }
        if (hasStructuralChanges || !entities.empty())
            renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLodRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options,
                                                  const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        prepareLodPath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglLodRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                                      uint32_t frameIndex, uint32_t width,
                                                      uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        prepareLodPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_lod\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false,\"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\"inputReplay\":";
        if (options.scenarioId == "fly-controls")
        {
            const std::string replayHash = sha256LodReplay(
                std::filesystem::path(options.inputReplayPath.c_str()));
            output << "{\"schemaVersion\":1,\"caseId\":\"webgl_lod\",\"scenarioId\":\"fly-controls\",\"captureFrame\":120,\"sha256\":\""
                   << replayHash << "\",\"target\":\"canvas:not([class])\",\"eventCount\":4,\"lastEventFrame\":120}";
        }
        else
        {
            output << "null";
        }
        output << "}\n";
    }

    void WebglLodRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                                         uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        prepareLodPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_lod\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":false,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":5000,\n  \"entityCount\":5000,\n  \"instanceCount\":1,\n  \"lodLevelsPerRoot\":5,\n"
               << "  \"instanceCounts\":[1],\n  \"scenePassCount\":1,\n  \"screenPassCount\":0,\n"
               << "  \"drawCommandCount\":1,\n  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n  \"containsLod\":true,\n"
               << "  \"assetAndAlgorithmState\":\"r185-icosahedron-wireframe-five-level-lod-point-directional-light-fog\",\n"
               << "  \"renderSetType\":\"WebglLodSceneRenderSet\",\n"
               << "  \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"lodState\",\"kind\":\"buffer\",\"role\":\"lod-root-level-distance-and-visibility\"}],\n"
               << "  \"scenePasses\":[{\"name\":\"main-lambert-wireframe-lod\",\"renderClass\":\"WebglLodMainPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-lambert-wireframe-lod\"}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetId\":\"scene-set-0\",\"renderSetCount\":1,\"renderSetType\":\"WebglLodSceneRenderSet\",\"renderableObjectCount\":5000,\"entityCount\":5000,\"drawCommandCount\":1,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"lodState\",\"kind\":\"buffer\",\"role\":\"lod-root-level-distance-and-visibility\"}],\"scenePasses\":[{\"name\":\"main-lambert-wireframe-lod\",\"renderClass\":\"WebglLodMainPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            if (index != 0u) output << ',';
            output << "{\"entityId\":" << entities[index].entityIndex << ",\"logicalRenderableId\":\"lod-"
                   << entities[index].rootIndex << "-level-" << entities[index].levelIndex
                   << "\",\"instanceCount\":1,\"lodLevel\":" << entities[index].lodLevel
                   << ",\"visible\":" << (entities[index].visible ? "true" : "false") << "}";
        }
        output << "]}]}\n";
    }

    void WebglLodRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                                            const ThreeSampleHostOptions &options,
                                            uint32_t frameIndex, GVM::RHI::Texture readbackTexture,
                                            uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("webgl_lod RGBA capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_lod has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLodRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer,
                                          const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
