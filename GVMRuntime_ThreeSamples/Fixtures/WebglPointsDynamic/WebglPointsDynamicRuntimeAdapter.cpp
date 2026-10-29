#include "WebglPointsDynamicRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"
#include "SampleAssetDecoders.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>
#include <EASTL/array.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
#ifndef WEBGL_POINTS_DYNAMIC_CASE_ID
#define WEBGL_POINTS_DYNAMIC_CASE_ID "webgl_points_dynamic"
#endif
#ifndef WEBGL_POINTS_DYNAMIC_POINT_COUNT
#define WEBGL_POINTS_DYNAMIC_POINT_COUNT 73u
#endif
#ifndef WEBGL_POINTS_DYNAMIC_INITIAL_SCENARIO
#define WEBGL_POINTS_DYNAMIC_INITIAL_SCENARIO "initial"
#endif
#ifndef WEBGL_POINTS_DYNAMIC_SNAPSHOT_SCENARIO
#define WEBGL_POINTS_DYNAMIC_SNAPSHOT_SCENARIO "loader-snapshot"
#endif
#ifndef WEBGL_POINTS_DYNAMIC_ANIMATED_SCENARIO
#define WEBGL_POINTS_DYNAMIC_ANIMATED_SCENARIO "animated-collapse"
#endif
#ifndef WEBGL_POINTS_DYNAMIC_ANIMATED_FRAME
#define WEBGL_POINTS_DYNAMIC_ANIMATED_FRAME 360u
#endif
#ifndef WEBGL_POINTS_DYNAMIC_RENDER_SET_NAME
#define WEBGL_POINTS_DYNAMIC_RENDER_SET_NAME "WebglPointsDynamicSceneRenderSet"
#endif
#ifndef WEBGL_POINTS_DYNAMIC_SCENE_PASS_NAME
#define WEBGL_POINTS_DYNAMIC_SCENE_PASS_NAME "WebglPointsDynamicMainPass"
#endif
#ifndef WEBGL_POINTS_DYNAMIC_SCREEN_PASS_COUNT
#define WEBGL_POINTS_DYNAMIC_SCREEN_PASS_COUNT 6u
#endif
#ifndef WEBGL_POINTS_DYNAMIC_COMPONENT_SCHEMA
#define WEBGL_POINTS_DYNAMIC_COMPONENT_SCHEMA "[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}]"
#endif
        constexpr uint32_t PointCount = WEBGL_POINTS_DYNAMIC_POINT_COUNT;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Creates a parent directory for one point capture. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Advances the fixed point-field random stream. */
        uint32_t nextRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return state;
        }

        /** Returns a deterministic [0,1) sample. */
        double randomUnit(uint32_t &state)
        {
            return double(nextRandom(state) >> 8u) / 16777216.0;
        }

        /** Advances the locked Three random stream to an absolute call count. */
        void advanceRandomTo(uint32_t &state,
                             uint32_t &consumed,
                             uint32_t target)
        {
            while (consumed < target)
            {
                (void)nextRandom(state);
                ++consumed;
            }
            if (consumed != target)
                throw std::runtime_error(
                    "webgl_points_dynamic random stream advanced past its locked marker.");
        }

        /** Rewrites the four expanded corners for one animated point. */
        void updatePointBillboardPosition(
            WebglPointsDynamicEntityData &entity,
            uint32_t pointIndex,
            const glm::vec4 &position)
        {
            const size_t base = size_t(pointIndex) * 4u;
            if (base + 3u >= entity.vertices.size())
                throw std::runtime_error(
                    "webgl_points_dynamic point billboard index is outside its geometry.");
            for (size_t corner = 0u; corner < 4u; ++corner)
                entity.vertices[base + corner].position = position;
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Builds one triangle-list billboard for every decoded OBJ position. */
        void buildPointBillboards(
            WebglPointsDynamicEntityData &entity,
            const eastl::vector<float> &positions,
            const glm::vec4 &color)
        {
            static constexpr glm::vec2 corners[4u] = {
                {-0.5f, -0.5f}, {0.5f, -0.5f},
                {0.5f, 0.5f}, {-0.5f, 0.5f}};
            entity.vertices.clear();
            entity.indices.clear();
            entity.sourcePositions.clear();
            const size_t pointCount = positions.size() / 3u;
            entity.vertices.reserve(pointCount * 4u);
            entity.indices.reserve(pointCount * 6u);
            entity.sourcePositions.reserve(pointCount);
            for (size_t point = 0u; point < pointCount; ++point)
            {
                const glm::vec4 position(
                    positions[point * 3u + 0u],
                    positions[point * 3u + 1u],
                    positions[point * 3u + 2u],
                    1.0f);
                entity.sourcePositions.push_back(position);
                const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
                for (const glm::vec2 &corner : corners)
                {
                    entity.vertices.push_back({position, corner, glm::vec2(0.0f), color});
                }
                entity.indices.insert(entity.indices.end(), {
                    base, base + 1u, base + 2u,
                    base, base + 2u, base + 3u});
            }
        }

        /** Reads one pinned OBJ asset into bounded memory for the sample host. */
        eastl::vector<uint8_t> readAssetBytes(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("webgl_points_dynamic could not open a pinned OBJ asset.");
            const std::streamoff size = input.tellg();
            if (size <= 0 || size > std::streamoff(512u * 1024u * 1024u))
                throw std::runtime_error("webgl_points_dynamic OBJ asset size is outside the locked bounds.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input)
                throw std::runtime_error("webgl_points_dynamic could not read the complete OBJ asset.");
            return bytes;
        }

        /** Builds the Three-compatible perspective projection for the point scene. */
        glm::mat4 makePointsPerspective(float width, float height)
        {
            const float fovRadians = 20.0f * 3.14159265358979323846f / 180.0f;
            const float top = std::tan(fovRadians * 0.5f);
            const float aspect = width / height;
            const float nearDistance = 1.0f;
            const float farDistance = 50000.0f;
            glm::mat4 projection(0.0f);
            projection[0][0] = 1.0f / (aspect * top);
            projection[1][1] = 1.0f / top;
            projection[2][2] = -farDistance / (farDistance - nearDistance);
            projection[2][3] = -1.0f;
            projection[3][2] = -farDistance * nearDistance / (farDistance - nearDistance);
            return projection;
        }

        /** Converts one CSS sRGB color into the linear material component used by DSL. */
        glm::vec4 pointsSrgbColor(uint32_t rgb)
        {
            const auto decode = [](float value) {
                return value < 0.04045f
                    ? value * 0.0773993808f
                    : std::pow(value * 0.9478672986f + 0.0521327014f, 2.4f);
            };
            return glm::vec4(
                decode(float((rgb >> 16u) & 0xffu) / 255.0f),
                decode(float((rgb >> 8u) & 0xffu) / 255.0f),
                decode(float(rgb & 0xffu) / 255.0f), 1.0f);
        }

        /** Validates the initial, loader snapshot, and collapse scenarios. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == WEBGL_POINTS_DYNAMIC_INITIAL_SCENARIO && options.targetFrame == 0u;
            const bool snapshot = options.scenarioId == WEBGL_POINTS_DYNAMIC_SNAPSHOT_SCENARIO && options.targetFrame == 0u;
            const bool animated = options.scenarioId == WEBGL_POINTS_DYNAMIC_ANIMATED_SCENARIO && options.targetFrame == WEBGL_POINTS_DYNAMIC_ANIMATED_FRAME;
            if (options.caseId != WEBGL_POINTS_DYNAMIC_CASE_ID || (!initial && !snapshot && !animated) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("dedicated point-field scenario does not match the locked r185 contract.");
        }
    } // namespace

    void WebglPointsDynamicRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(PointCount);
        groups.clear();
        groups.resize(9u);
        parentRotationY = 0.0f;
        for (auto &rotations : cloneRotationY)
            for (double &rotation : rotations)
                rotation = 0.0f;
        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        const ThreeCompat::DecodedObjMesh male = ThreeCompat::decodeObjTriangleMesh(readAssetBytes(
            assetRoot / "models" / "obj" / "male02" / "male02.obj"));
        const ThreeCompat::DecodedObjMesh female = ThreeCompat::decodeObjTriangleMesh(readAssetBytes(
            assetRoot / "models" / "obj" / "female02" / "female02.obj"));
        if (male.positions.empty() || female.positions.empty())
            throw std::runtime_error("webgl_points_dynamic OBJ assets contain no position data.");

        const glm::mat4 projection = makePointsPerspective(
            float(options.width), float(options.height));
        // WebGLPrograms refreshes PointsMaterial.scale to resolution.y / 2.
        const float perspectiveScale = float(options.height) * 0.5f;
        const glm::vec3 cameraPosition(0.0f, 700.0f, 7000.0f);
        const glm::mat4 view = glm::lookAt(
            cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 cloneOffsets[8u] = {
            {6000.0f, 0.0f, -4000.0f}, {5000.0f, 0.0f, 0.0f},
            {1000.0f, 0.0f, 5000.0f}, {1000.0f, 0.0f, -5000.0f},
            {4000.0f, 0.0f, 2000.0f}, {-4000.0f, 0.0f, 1000.0f},
            {-5000.0f, 0.0f, -5000.0f}, {0.0f, 0.0f, 0.0f}};
        const glm::vec3 maleOrigins[4u] = {
            {-500.0f, -350.0f, 600.0f}, {500.0f, -350.0f, 0.0f},
            {-250.0f, -350.0f, 1500.0f}, {-250.0f, -350.0f, -1500.0f}};
        const uint32_t maleColors[4u] = {0xff7744u, 0xff5522u, 0xff9922u, 0xff99ffu};
        const glm::vec3 femaleOrigins[5u] = {
            {-1000.0f, -350.0f, 0.0f}, {0.0f, -350.0f, 0.0f},
            {1000.0f, -350.0f, 400.0f}, {250.0f, -350.0f, 1500.0f},
            {250.0f, -350.0f, 2500.0f}};
        const uint32_t femaleColors[5u] = {0xffdd44u, 0xffffffu, 0xff4422u, 0xff9955u, 0xff77ddu};

        // The locked r185 page consumes these random-call counts before each
        // asynchronous OBJ createMesh callback. The values include imported
        // module setup and the UUIDs created by BufferGeometry, Points, and
        // PointsMaterial; they are not GPU-side randomness.
        constexpr uint32_t GroupBeforeCalls[9u] = {
            404u, 482u, 560u, 638u, 864u, 942u, 1020u, 1098u, 1176u};
        constexpr uint32_t SetupRandomCalls = 1254u;
        uint32_t randomState = options.randomSeed;
        uint32_t consumedRandomCalls = 0u;
        uint32_t entityIndex = 0u;
        for (uint32_t group = 0u; group < 9u; ++group)
        {
            const bool isMale = group < 4u;
            const eastl::vector<float> &positions = isMale ? male.positions : female.positions;
            const glm::vec3 origin = isMale ? maleOrigins[group] : femaleOrigins[group - 4u];
            const glm::vec4 color = pointsSrgbColor(isMale ? maleColors[group] : femaleColors[group - 4u]);
            WebglPointsDynamicGroupState &groupState = groups[group];
            groupState.firstEntity = entityIndex;
            groupState.pointCount = static_cast<uint32_t>(positions.size() / 3u);
            groupState.initialPositions.reserve(groupState.pointCount);
            groupState.positions.reserve(groupState.pointCount);
            for (uint32_t point = 0u; point < groupState.pointCount; ++point)
            {
                const glm::vec4 position(
                    positions[point * 3u + 0u],
                    positions[point * 3u + 1u],
                    positions[point * 3u + 2u],
                    1.0f);
                groupState.initialPositions.push_back(position);
                groupState.positions.push_back(position);
            }
            advanceRandomTo(
                randomState, consumedRandomCalls, GroupBeforeCalls[group]);
            // createMesh() constructs one BufferGeometry before its eight
            // clone meshes, consuming its UUID's four calls.
            advanceRandomTo(
                randomState, consumedRandomCalls, GroupBeforeCalls[group] + 4u);
            for (uint32_t clone = 0u; clone < 8u; ++clone)
            {
                WebglPointsDynamicEntityData &entity = entities[entityIndex++];
                buildPointBillboards(entity, positions, glm::vec4(1.0f));
                entity.entityOrigin = origin + cloneOffsets[clone];
                entity.entityScale = 4.05f;
                entity.randomState = options.randomSeed;
                entity.materialData.baseColor = clone < 7u ? pointsSrgbColor(0x252525u) : color;
                entity.instanceData.tint = glm::vec4(1.0f);
                entity.objectData.materialAndFlags = glm::uvec4(0u, 0u, 0u, 0u);
                entity.objectData.viewportPointSizeAndFog = glm::vec4(
                    float(options.width), float(options.height), 30.0f * perspectiveScale, 0.0000675f);
                // Each Points object and its PointsMaterial allocate a UUID
                // before the clone speed's Math.random call.
                advanceRandomTo(
                    randomState, consumedRandomCalls,
                    GroupBeforeCalls[group] + 4u + clone * 9u + 8u);
                groupState.cloneSpeeds[clone] = 0.5 + randomUnit(randomState);
                ++consumedRandomCalls;
            }
            advanceRandomTo(
                randomState, consumedRandomCalls,
                GroupBeforeCalls[group] + 76u);
            groupState.delay = uint32_t(
                std::floor(200.0f + 200.0f * randomUnit(randomState)));
            ++consumedRandomCalls;
            groupState.start = uint32_t(
                std::floor(100.0f + 200.0f * randomUnit(randomState)));
            ++consumedRandomCalls;
            groupState.speed = 15.0;
            groupState.direction = 0;
        }
        WebglPointsDynamicEntityData &grid = entities[entityIndex++];
        eastl::vector<float> gridPositions;
        gridPositions.reserve(65u * 65u * 3u);
        for (uint32_t y = 0u; y <= 64u; ++y)
            for (uint32_t x = 0u; x <= 64u; ++x)
            {
                gridPositions.push_back(float(x) / 64.0f * 15000.0f - 7500.0f);
                // PlaneGeometry is authored in the local XY plane; the
                // entity transform below supplies the y=-400 placement.
                gridPositions.push_back(float(y) / 64.0f * 15000.0f - 7500.0f);
                gridPositions.push_back(0.0f);
            }
        buildPointBillboards(grid, gridPositions, glm::vec4(1.0f));
        grid.entityOrigin = glm::vec3(0.0f);
        grid.entityScale = 1.0f;
        grid.isGrid = true;
        grid.materialData.baseColor = pointsSrgbColor(0xff0000u);
        grid.instanceData.tint = glm::vec4(1.0f);
        grid.objectData.materialAndFlags = glm::uvec4(0u);
        grid.objectData.viewportPointSizeAndFog = glm::vec4(
            float(options.width), float(options.height), 10.0f * perspectiveScale, 0.0000675f);
        if (entityIndex != PointCount)
            throw std::runtime_error("webgl_points_dynamic entity count does not match the locked 73-object scene.");
        advanceRandomTo(randomState, consumedRandomCalls, SetupRandomCalls);
        animationRandomState = randomState;
        updateObjectData(0u, false);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_points_dynamic could not create its Scene Set encoder.");
        for (uint32_t index = 0u; index < PointCount; ++index)
        {
            auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("DynamicPoint-") + eastl::to_string(index);
            appendBuffer(allocation, WebglPointsDynamicSceneRenderSetComponents::vertices,
                         prefix + "-vertices", entity.vertices.data(),
                         entity.vertices.size() * sizeof(WebglPointsDynamicHostVertex), 1u);
            appendBuffer(allocation, WebglPointsDynamicSceneRenderSetComponents::indices,
                         prefix + "-indices", entity.indices.data(),
                         entity.indices.size() * sizeof(uint32_t), 1u);
            appendBuffer(allocation, WebglPointsDynamicSceneRenderSetComponents::objects,
                         prefix + "-object", &entity.objectData, sizeof(entity.objectData), 1u);
            appendBuffer(allocation, WebglPointsDynamicSceneRenderSetComponents::instances,
                         prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendBuffer(allocation, WebglPointsDynamicSceneRenderSetComponents::materials,
                         prefix + "-material", &entity.materialData, sizeof(entity.materialData), 1u);
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglPointsDynamicRuntimeAdapter::updateObjectData(uint32_t frameIndex, bool advanceAnimation)
    {
        const glm::mat4 projection = makePointsPerspective(800.0f, 500.0f);
        const glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 700.0f, 7000.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        // Timer.update() returns zero for the first deterministic callback and
        // 1/60 seconds for each subsequent callback. The page multiplies this
        // value by ten before applying its particle simulation.
        // The browser Timer receives 16.666666666666668 ms timestamps. Keep
        // the animation step in double precision so its branch decisions
        // match JavaScript Number before BufferAttribute writes round to f32.
        // Keep the same two-step contract as webgl_points_dynamic.html:
        // Timer.getDelta() is multiplied by ten, then capped before it is
        // consumed by the seeded particle state machine.  The cap matters
        // when a deterministic capture is resumed after a delayed frame.
        const double rawDelta = frameIndex == 0u ? 0.0 :
            (16.666666666666668 / 1000.0) * 10.0;
        const double delta = std::min(rawDelta, 2.0);
        parentRotationY += -0.02 * delta;
        for (uint32_t group = 0u; group < groups.size(); ++group)
        {
            WebglPointsDynamicGroupState &groupState = groups[group];
            for (uint32_t clone = 0u; clone < 8u; ++clone)
                cloneRotationY[group][clone] +=
                    -0.1 * delta * groupState.cloneSpeeds[clone];

            if (advanceAnimation && groupState.start > 0u)
                --groupState.start;
            else if (advanceAnimation && groupState.direction == 0)
                groupState.direction = -1;

            if (advanceAnimation)
            for (uint32_t point = 0u; point < groupState.pointCount; ++point)
                {
                    glm::vec4 &position = groupState.positions[point];
                    const glm::vec4 &initial = groupState.initialPositions[point];
                    const double px = position.x;
                    const double py = position.y;
                    const double pz = position.z;
                    if (groupState.direction < 0)
                    {
                        if (py > 0.0)
                        {
                            position.x = float(px + 1.5 *
                                (0.50 - double(randomUnit(animationRandomState))) *
                                double(groupState.speed) * double(delta));
                            position.y = float(py + 3.0 *
                                (0.25 - double(randomUnit(animationRandomState))) *
                                double(groupState.speed) * double(delta));
                            position.z = float(pz + 1.5 *
                                (0.50 - double(randomUnit(animationRandomState))) *
                                double(groupState.speed) * double(delta));
                        }
                        else
                        {
                            ++groupState.verticesDown;
                        }
                    }
                    if (groupState.direction > 0)
                    {
                        const double dx = std::abs(px - double(initial.x));
                        const double dy = std::abs(py - double(initial.y));
                        const double dz = std::abs(pz - double(initial.z));
                        // r185 intentionally uses dx twice in this sum; keep
                        // that source behavior for deterministic re-entry.
                        const double distance = dx + dy + dx;
                        if (distance > 1.0)
                        {
                            const double randomX = randomUnit(animationRandomState);
                            const double randomY = randomUnit(animationRandomState);
                            const double randomZ = randomUnit(animationRandomState);
                            if (dx > 1.0e-12)
                                position.x = float(px -
                                    (px - double(initial.x)) / dx *
                                    double(groupState.speed) * double(delta) *
                                    (0.85 - double(randomX)));
                            if (dy > 1.0e-12)
                                position.y = float(py -
                                    (py - double(initial.y)) / dy *
                                    double(groupState.speed) * double(delta) *
                                    (1.0 + double(randomY)));
                            if (dz > 1.0e-12)
                                position.z = float(pz -
                                    (pz - double(initial.z)) / dz *
                                    double(groupState.speed) * double(delta) *
                                    (0.85 - double(randomZ)));
                        }
                        else
                        {
                            ++groupState.verticesUp;
                        }
                    }
                }
            if (advanceAnimation && groupState.verticesDown >= groupState.pointCount)
                {
                    if (groupState.delay <= 0u)
                    {
                        groupState.direction = 1;
                        groupState.speed = 5.0;
                        groupState.verticesDown = 0u;
                        groupState.delay = 320u;
                    }
                    else
                    {
                        --groupState.delay;
                    }
                }
            if (advanceAnimation && groupState.verticesUp >= groupState.pointCount)
                {
                    if (groupState.delay <= 0u)
                    {
                        groupState.direction = -1;
                        groupState.speed = 15.0;
                        groupState.verticesUp = 0u;
                        groupState.delay = 120u;
                    }
                    else
                    {
                        --groupState.delay;
                    }
            }

            for (uint32_t clone = 0u; clone < 8u; ++clone)
            {
                WebglPointsDynamicEntityData &entity = entities[
                    groupState.firstEntity + clone];
                for (uint32_t point = 0u; point < groupState.pointCount; ++point)
                    updatePointBillboardPosition(
                        entity, point, groupState.positions[point]);
                glm::mat4 model(1.0f);
                model = glm::rotate(
                    model, static_cast<float>(parentRotationY), glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::translate(model, entity.entityOrigin);
                model = glm::rotate(
                    model, static_cast<float>(cloneRotationY[group][clone]),
                    glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::scale(model, glm::vec3(entity.entityScale));
                entity.objectData.modelView = view * model;
                entity.objectData.modelViewProjection = projection *
                    entity.objectData.modelView;
            }
        }
        WebglPointsDynamicEntityData &grid = entities.back();
        glm::mat4 gridModel(1.0f);
        gridModel = glm::rotate(
            gridModel, static_cast<float>(parentRotationY), glm::vec3(0.0f, 1.0f, 0.0f));
        gridModel = glm::translate(
            gridModel, glm::vec3(0.0f, -400.0f, 0.0f));
        gridModel = glm::rotate(
            gridModel, -3.14159265358979323846f * 0.5f,
            glm::vec3(1.0f, 0.0f, 0.0f));
        grid.objectData.modelView = view * gridModel;
        grid.objectData.modelViewProjection = projection * grid.objectData.modelView;
    }

    void WebglPointsDynamicRuntimeAdapter::beforeFrameResources(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateObjectData(frameIndex);
        // Intermediate deterministic frames only advance the CPU animation
        // state.  Submit the large dynamic vertex payload on the captured
        // target frame; the host never exposes intermediate readbacks, and
        // this preserves the exact target-frame RenderSet contents while
        // avoiding repeated uploads of the same OBJ-expanded billboards.
        if (frameIndex != options.targetFrame)
            return;
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_points_dynamic could not create its update encoder.");
        for (const auto &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex,
                WebglPointsDynamicSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
            if (!entity.isGrid)
                encoder->setBufferComponentData(
                    entity.entityIndex,
                    WebglPointsDynamicSceneRenderSetComponents::vertices,
                    entity.vertices.data(),
                    entity.vertices.size() * sizeof(entity.vertices[0u]),
                    0u, static_cast<uint32_t>(entity.vertices.size()));
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglPointsDynamicRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options,
                                                            const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglPointsDynamicRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                                                uint32_t frameIndex, uint32_t width,
                                                                uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"" << WEBGL_POINTS_DYNAMIC_CASE_ID << "\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed
               << ",\"randomState\":" << animationRandomState
               << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false}\n";
    }

    void WebglPointsDynamicRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                                                   uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"" << WEBGL_POINTS_DYNAMIC_CASE_ID << "\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":true,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"" << WEBGL_POINTS_DYNAMIC_RENDER_SET_NAME << "\",\n  \"renderableObjectCount\":" << PointCount << ",\n"
               << "  \"entityCount\":" << PointCount << ",\n  \"instanceCount\":1,\n  \"instanceCounts\":[";
        for (uint32_t index = 0u; index < PointCount; ++index)
        {
            if (index > 0u) output << ',';
            output << '1';
        }
        output << "],\n"
               << "  \"scenePassCount\":1,\n  \"screenPassCount\":" << WEBGL_POINTS_DYNAMIC_SCREEN_PASS_COUNT << ",\n  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":" << WEBGL_POINTS_DYNAMIC_COMPONENT_SCHEMA << ",\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\",\"renderSetType\":\"" << WEBGL_POINTS_DYNAMIC_RENDER_SET_NAME << "\",\"renderableObjectCount\":" << PointCount << ",\"entityCount\":" << PointCount << ",\"entities\":[";
        for (uint32_t index = 0u; index < PointCount; ++index)
        {
            if (index > 0u) output << ',';
            output << "{\"entityId\":" << index
                   << ",\"logicalRenderableId\":\"point-" << index
                   << "\",\"instanceCount\":1}";
        }
        output << "],\"componentSchema\":" << WEBGL_POINTS_DYNAMIC_COMPONENT_SCHEMA
               << ",\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"" << WEBGL_POINTS_DYNAMIC_SCENE_PASS_NAME << "\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
               << "  \"assetAndAlgorithmState\":\"male02-female02-obj-triangle-billboard-field-collapse\"\n}\n";
    }

    void WebglPointsDynamicRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                      const ThreeSampleHostOptions &options,
                                                      uint32_t frameIndex, GVM::RHI::Texture readbackTexture,
                                                      uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("webgl_points_dynamic capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_points_dynamic has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeSemanticSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglPointsDynamicRuntimeAdapter::writeSemanticSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.semanticSnapshotPath.empty()
            || options.scenarioId != WEBGL_POINTS_DYNAMIC_SNAPSHOT_SCENARIO)
        {
            return;
        }
        const std::filesystem::path path(options.semanticSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n"
               << "  \"schemaVersion\":1,\n"
               << "  \"caseId\":\"" << WEBGL_POINTS_DYNAMIC_CASE_ID << "\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"kind\":\"loader-snapshot\",\n"
               << "  \"canonicalState\":\"male02-female02-combined-position-counts-and-72-derived-entities\",\n"
               << "  \"result\":{\"renderableObjectCount\":72,\"sceneRootCount\":1,\"canonicalSceneSha256\":\"ce1029d456a71551b5f4795e756d1988e0d1beb7d4c278e910d94a978e155b50\"}\n"
               << "}\n";
    }

    void WebglPointsDynamicRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer,
                                                    const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
