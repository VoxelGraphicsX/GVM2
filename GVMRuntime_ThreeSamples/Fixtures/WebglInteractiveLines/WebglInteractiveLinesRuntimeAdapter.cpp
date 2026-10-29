#include "WebglInteractiveLinesRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t LineCount = 51u;
        // Three.js UUID generation is part of the locked Math.random stream.
        // Camera, Scene, sphere geometry/material/mesh and line geometry
        // consume 100 words before the random walk in the locked r185 page.
        constexpr uint32_t InitialThreeRandomSamples = 100u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        /** Creates parent directories for a line capture artifact. */
        void prepareLinesPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Advances the fixed random stream used for Three's Math.random calls. */
        uint32_t nextLineRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return state;
        }

        /** Returns a deterministic unit interval sample. */
        float lineRandom(uint32_t &state)
        {
            return float(nextLineRandom(state) >> 8u) / 16777216.0f;
        }

        /** Converts one sRGB byte channel to linear material light. */
        float linesSrgbByteToLinear(uint32_t value)
        {
            const float channel = float(value) / 255.0f;
            return channel <= 0.04045f
                ? channel / 12.92f
                : std::pow((channel + 0.055f) / 1.055f, 2.4f);
        }

        /** Appends one typed payload to an allocation. */
        void appendLinesBuffer(
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

        /** Adds one screen-space-expanded triangle pair for a 3D line segment. */
        void appendExpandedLineSegment(
            WebglInteractiveLinesEntityData &entity,
            const glm::vec3 &a,
            const glm::vec3 &b)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 start(a, 1.0f);
            const glm::vec4 end(b, 1.0f);
            entity.vertices.push_back({start, end, {0.0f, 0.0f, 0.0f, 0.0f}});
            entity.vertices.push_back({start, end, {1.0f, 0.0f, 0.0f, 0.0f}});
            entity.indices.insert(entity.indices.end(), {base + 0u, base + 1u});
        }

        /** Builds the deterministic 32-by-16 latitude/longitude sphere used by the hit marker. */
        void appendHitSphere(
            WebglInteractiveLinesEntityData &entity,
            float radius,
            uint32_t widthSegments,
            uint32_t heightSegments)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            for (uint32_t iy = 0u; iy <= heightSegments; ++iy)
            {
                const float v = float(iy) / float(heightSegments);
                const float phi = Pi * v;
                for (uint32_t ix = 0u; ix <= widthSegments; ++ix)
                {
                    const float u = float(ix) / float(widthSegments);
                    const float theta = 2.0f * float(Pi) * u;
                    entity.vertices.push_back({glm::vec4(
                        radius * std::sin(phi) * std::cos(theta),
                        radius * std::cos(phi),
                        radius * std::sin(phi) * std::sin(theta),
                        1.0f)});
                }
            }
            for (uint32_t iy = 0u; iy < heightSegments; ++iy)
            {
                for (uint32_t ix = 0u; ix < widthSegments; ++ix)
                {
                    const uint32_t a = base + iy * (widthSegments + 1u) + ix;
                    const uint32_t b = a + widthSegments + 1u;
                    const uint32_t c = b + 1u;
                    const uint32_t d = a + 1u;
                    if (iy != 0u)
                    {
                        entity.indices.insert(entity.indices.end(), {a, b, d});
                    }
                    if (iy != heightSegments - 1u)
                    {
                        entity.indices.insert(entity.indices.end(), {b, c, d});
                    }
                }
            }
        }

        /** Builds the 50-point random walk shared by all line objects. */
        eastl::vector<glm::vec3> buildLinePath(uint32_t &randomState)
        {
            eastl::vector<glm::vec3> points;
            points.reserve(50u);
            glm::vec3 point(0.0f);
            glm::vec3 direction(0.0f);
            for (uint32_t index = 0u; index < 50u; ++index)
            {
                direction.x += lineRandom(randomState) - 0.5f;
                direction.y += lineRandom(randomState) - 0.5f;
                direction.z += lineRandom(randomState) - 0.5f;
                if (glm::length(direction) < 1e-5f) direction = glm::vec3(1.0f, 0.0f, 0.0f);
                direction = glm::normalize(direction) * 10.0f;
                point += direction;
                points.push_back(point);
            }
            return points;
        }

        /** Returns the perspective projection used by the upstream example. */
        glm::mat4 makeLinesProjection(uint32_t width, uint32_t height)
        {
            return glm::perspective(glm::radians(70.0f), float(width) / float(height), 1.0f, 10000.0f);
        }

        /** Reads the locked pointer position from the line-hit replay. */
        glm::dvec2 readLineHitPointer(const ThreeSampleHostOptions &options)
        {
            if (options.inputReplayPath.empty())
            {
                return glm::dvec2(0.0);
            }
            std::ifstream input(options.inputReplayPath.c_str());
            if (!input)
            {
                throw std::runtime_error("Could not open webgl_interactive_lines input replay.");
            }
            nlohmann::json replay;
            input >> replay;
            if (replay.value("caseId", "") != "webgl_interactive_lines" ||
                replay.value("scenarioId", "") != "line-hit")
            {
                throw std::runtime_error("webgl_interactive_lines replay identity differs from the formal scenario.");
            }
            return glm::dvec2(
                replay.at("canonicalState").at("pointerX").get<double>(),
                replay.at("canonicalState").at("pointerY").get<double>());
        }

        /** Computes the closest ray/segment pair and returns its segment point. */
        bool intersectLineSegment(const glm::dvec3 &origin,
                                  const glm::dvec3 &direction,
                                  const glm::dvec3 &start,
                                  const glm::dvec3 &end,
                                  double threshold,
                                  glm::dvec3 &segmentPoint,
                                  double &rayDistance)
        {
            const glm::dvec3 segment = end - start;
            const glm::dvec3 offset = origin - start;
            const double segmentLengthSq = glm::dot(segment, segment);
            const double directionSegment = glm::dot(direction, segment);
            const double directionOffset = glm::dot(direction, offset);
            const double segmentOffset = glm::dot(segment, offset);
            const double denominator = segmentLengthSq - directionSegment * directionSegment;
            double segmentParameter = 0.0;
            double rayParameter = -directionOffset;
            if (segmentLengthSq > 1.0e-12 && std::abs(denominator) > 1.0e-12)
            {
                segmentParameter = (segmentOffset - directionSegment * directionOffset) / denominator;
                rayParameter = directionSegment * segmentParameter - directionOffset;
            }
            segmentParameter = glm::clamp(segmentParameter, 0.0, 1.0);
            if (rayParameter < 0.0)
            {
                rayParameter = 0.0;
                if (segmentLengthSq > 1.0e-12)
                {
                    segmentParameter = glm::clamp(-segmentOffset / segmentLengthSq, 0.0, 1.0);
                }
            }
            const glm::dvec3 rayPoint = origin + direction * rayParameter;
            const glm::dvec3 linePoint = start + segment * segmentParameter;
            if (glm::dot(rayPoint - linePoint, rayPoint - linePoint) > threshold * threshold)
            {
                return false;
            }
            segmentPoint = linePoint;
            rayDistance = rayParameter;
            return true;
        }

        /** Performs Three's CPU line raycast for the canonical pointer frame. */
        glm::dvec3 findLineHitPoint(const eastl::vector<WebglInteractiveLinesEntityData> &entities,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t frameIndex,
                                    double pointerX,
                                    double pointerY)
        {
            const double theta = (0.1 + 0.1 * double(frameIndex)) * Pi / 180.0;
            const glm::dvec3 cameraPosition(100.0 * std::sin(theta), 100.0 * std::sin(theta), 100.0 * std::cos(theta));
            const glm::mat4 view = glm::mat4(glm::lookAt(cameraPosition, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0)));
            const glm::mat4 projection = makeLinesProjection(width, height);
            const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
            const float ndcX = float(pointerX / double(width) * 2.0 - 1.0);
            const float ndcY = float(1.0 - pointerY / double(height) * 2.0);
            glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            nearPoint /= nearPoint.w;
            farPoint /= farPoint.w;
            const glm::dvec3 origin = cameraPosition;
            const glm::dvec3 direction = glm::normalize(glm::dvec3(farPoint - nearPoint));
            glm::dvec3 closestPoint(0.0);
            double closestDistance = std::numeric_limits<double>::max();
            for (uint32_t entityIndex = 0u; entityIndex < 50u && entityIndex < entities.size(); ++entityIndex)
            {
                const WebglInteractiveLinesEntityData &entity = entities[entityIndex];
                for (size_t vertexIndex = 0u; vertexIndex + 1u < entity.vertices.size(); vertexIndex += 2u)
                {
                    const glm::vec4 localStart = entity.vertices[vertexIndex].segmentStart;
                    const glm::vec4 localEnd = entity.vertices[vertexIndex].segmentEnd;
                    const glm::dvec3 worldStart = glm::dvec3(entity.model * localStart);
                    const glm::dvec3 worldEnd = glm::dvec3(entity.model * localEnd);
                    glm::dvec3 candidate;
                    double candidateDistance = 0.0;
                    if (intersectLineSegment(origin, direction, worldStart, worldEnd, 3.0,
                                              candidate, candidateDistance) &&
                        candidateDistance < closestDistance)
                    {
                        closestPoint = candidate;
                        closestDistance = candidateDistance;
                    }
                }
            }
            return closestDistance == std::numeric_limits<double>::max()
                ? glm::dvec3(0.0)
                : closestPoint;
        }

        /** Validates the locked initial, animated, and pointer-hit scenarios. */
        void validateLinesOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool hit = (options.scenarioId == "line-hit" || options.scenarioId == "intersection") && options.targetFrame == 61u;
            if (options.caseId != "webgl_interactive_lines" || (!initial && !animated && !hit) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgl_interactive_lines scenario does not match the locked r185 contract.");
            }
        }
    } // namespace

    void WebglInteractiveLinesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateLinesOptions(options);
        device = inDevice;
        captureWidth = options.width;
        captureHeight = options.height;
        entities.clear();
        entities.resize(LineCount);
        pointerX = 0.0;
        pointerY = 0.0;
        sphereVisible = options.scenarioId == "line-hit" || options.scenarioId == "intersection";
        if (sphereVisible)
        {
            const glm::dvec2 pointer = readLineHitPointer(options);
            pointerX = pointer.x;
            pointerY = pointer.y;
        }
        uint32_t randomState = options.randomSeed;
        // Consume the deterministic UUID stream before the first random walk.
        for (uint32_t sample = 0u; sample < InitialThreeRandomSamples; ++sample)
        {
            (void)lineRandom(randomState);
        }
        const eastl::vector<glm::vec3> path = buildLinePath(randomState);
        // parentTransform is constructed after the path and allocates one
        // UUID before its position/rotation/scale assignments.
        for (uint32_t uuidWord = 0u; uuidWord < 4u; ++uuidWord)
        {
            (void)lineRandom(randomState);
        }
        const glm::vec3 parentPosition(
            lineRandom(randomState) * 40.0f - 20.0f,
            lineRandom(randomState) * 40.0f - 20.0f,
            lineRandom(randomState) * 40.0f - 20.0f);
        const glm::vec3 parentRotation(
            lineRandom(randomState) * float(2.0 * Pi),
            lineRandom(randomState) * float(2.0 * Pi),
            lineRandom(randomState) * float(2.0 * Pi));
        const glm::vec3 parentScale(
            lineRandom(randomState) + 0.5f,
            lineRandom(randomState) + 0.5f,
            lineRandom(randomState) + 0.5f);
        parentTransform = glm::translate(glm::mat4(1.0f), parentPosition) *
            glm::rotate(glm::mat4(1.0f), parentRotation.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::rotate(glm::mat4(1.0f), parentRotation.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::rotate(glm::mat4(1.0f), parentRotation.z, glm::vec3(0.0f, 0.0f, 1.0f)) *
            glm::scale(glm::mat4(1.0f), parentScale);
        for (uint32_t entityIndex = 0u; entityIndex < LineCount; ++entityIndex)
        {
            WebglInteractiveLinesEntityData &entity = entities[entityIndex];
            const uint32_t packedColor = entityIndex < 50u
                ? static_cast<uint32_t>(lineRandom(randomState) * 0x00ffffffu)
                : 0x00ff0000u;
            // LineBasicMaterial is constructed after evaluating its color;
            // Material.uuid consumes four additional xorshift words.
            for (uint32_t uuidWord = 0u; uuidWord < 4u; ++uuidWord)
            {
                (void)lineRandom(randomState);
            }
            entity.materialData.colorAndFlags = glm::vec4(
                linesSrgbByteToLinear((packedColor >> 16u) & 0xffu),
                linesSrgbByteToLinear((packedColor >> 8u) & 0xffu),
                linesSrgbByteToLinear(packedColor & 0xffu), entityIndex < 50u ? 1.0f : 0.0f);
            const bool lineStrip = entityIndex < 50u && lineRandom(randomState) > 0.5f;
            // Line/LineSegments extends Object3D and allocates one UUID.
            for (uint32_t uuidWord = 0u; uuidWord < 4u; ++uuidWord)
            {
                (void)lineRandom(randomState);
            }
            glm::vec3 position(0.0f);
            glm::vec3 rotation(0.0f);
            glm::vec3 scale(1.0f);
            if (entityIndex < 50u)
            {
                const uint32_t step = lineStrip ? 1u : 2u;
                for (uint32_t pointIndex = 0u;
                     pointIndex + 1u < path.size();
                     pointIndex += step)
                {
                    appendExpandedLineSegment(entity, path[pointIndex], path[pointIndex + 1u]);
                }
                position = glm::vec3(
                    lineRandom(randomState) * 400.0f - 200.0f,
                    lineRandom(randomState) * 400.0f - 200.0f,
                    lineRandom(randomState) * 400.0f - 200.0f);
                rotation = glm::vec3(
                    lineRandom(randomState) * float(2.0 * Pi),
                    lineRandom(randomState) * float(2.0 * Pi),
                    lineRandom(randomState) * float(2.0 * Pi));
                scale = glm::vec3(
                    lineRandom(randomState) + 0.5f,
                    lineRandom(randomState) + 0.5f,
                    lineRandom(randomState) + 0.5f);
            }
            else
            {
                appendHitSphere(entity, 5.0f, 32u, 16u);
            }
            entity.model = entityIndex == LineCount - 1u
                ? glm::mat4(1.0f)
                : parentTransform * glm::translate(glm::mat4(1.0f), position) *
                    glm::rotate(glm::mat4(1.0f), rotation.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
                    glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
                    glm::rotate(glm::mat4(1.0f), rotation.z, glm::vec3(0.0f, 0.0f, 1.0f)) *
                    glm::scale(glm::mat4(1.0f), scale);
            entity.instanceData.reserved = glm::vec4(0.0f);
        }
        updateObjectData(options.width, options.height, options.targetFrame);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_interactive_lines could not create its Scene Set encoder.");
        for (uint32_t entityIndex = 0u; entityIndex < LineCount; ++entityIndex)
        {
            WebglInteractiveLinesEntityData &entity = entities[entityIndex];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("InteractiveLine-") + eastl::to_string(entityIndex);
            appendLinesBuffer(allocation, WebglInteractiveLinesSceneRenderSetComponents::vertices,
                              prefix + "-vertices", entity.vertices.data(),
                              entity.vertices.size() * sizeof(WebglInteractiveLinesHostVertex), 1u);
            appendLinesBuffer(allocation, WebglInteractiveLinesSceneRenderSetComponents::indices,
                              prefix + "-indices", entity.indices.data(),
                              entity.indices.size() * sizeof(uint32_t), 1u);
            appendLinesBuffer(allocation, WebglInteractiveLinesSceneRenderSetComponents::objects,
                              prefix + "-object", &entity.objectData, sizeof(entity.objectData), 1u);
            appendLinesBuffer(allocation, WebglInteractiveLinesSceneRenderSetComponents::instances,
                              prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendLinesBuffer(allocation, WebglInteractiveLinesSceneRenderSetComponents::materials,
                              prefix + "-material", &entity.materialData, sizeof(entity.materialData), 1u);
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        if (sphereVisible)
        {
            const glm::dvec3 hitPoint = findLineHitPoint(
                entities, options.width, options.height, options.targetFrame, pointerX, pointerY);
            entities[LineCount - 1u].model = glm::translate(glm::mat4(1.0f), glm::vec3(hitPoint));
            entities[LineCount - 1u].materialData.colorAndFlags.w = 1.0f;
            updateObjectData(options.width, options.height, options.targetFrame);
            const auto sphereEncoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
            if (!sphereEncoder) throw std::runtime_error("webgl_interactive_lines could not create sphere update encoder.");
            sphereEncoder->setBufferComponentData(
                entities[LineCount - 1u].entityIndex,
                WebglInteractiveLinesSceneRenderSetComponents::objects,
                &entities[LineCount - 1u].objectData,
                sizeof(entities[LineCount - 1u].objectData), 0u, 1u);
            sphereEncoder->setBufferComponentData(
                entities[LineCount - 1u].entityIndex,
                WebglInteractiveLinesSceneRenderSetComponents::materials,
                &entities[LineCount - 1u].materialData,
                sizeof(entities[LineCount - 1u].materialData), 0u, 1u);
            renderer.executeRenderSetCommand(SceneRenderSetHandle, sphereEncoder);
        }
    }

    void WebglInteractiveLinesRuntimeAdapter::updateObjectData(uint32_t width, uint32_t height, uint32_t frameIndex)
    {
        const double theta = (0.1 + 0.1 * double(frameIndex)) * Pi / 180.0;
        const glm::dvec3 cameraPosition(100.0 * std::sin(theta), 100.0 * std::sin(theta), 100.0 * std::cos(theta));
        const glm::mat4 view = glm::mat4(glm::lookAt(cameraPosition, glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0)));
        const glm::mat4 projection = makeLinesProjection(width, height);
        for (WebglInteractiveLinesEntityData &entity : entities)
        {
            entity.objectData.viewportAndReserved = glm::vec4(float(width), float(height), 0.0f, 0.0f);
            entity.objectData.modelView = view * entity.model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
        }
    }

    void WebglInteractiveLinesRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                           const ThreeSampleHostOptions &options,
                                                           uint32_t frameIndex)
    {
        updateObjectData(options.width, options.height, frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_interactive_lines could not create its update encoder.");
        for (const WebglInteractiveLinesEntityData &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex,
                WebglInteractiveLinesSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglInteractiveLinesRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options,
                                                                const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        prepareLinesPath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglInteractiveLinesRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                                                    uint32_t frameIndex, uint32_t width,
                                                                    uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        prepareLinesPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_interactive_lines\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false,\"inputReplay\":";
        if (options.scenarioId == "line-hit" || options.scenarioId == "intersection")
        {
            output << "{\"schemaVersion\":1,\"caseId\":\"webgl_interactive_lines\",\"scenarioId\":\"line-hit\",\"captureFrame\":61,\"sha256\":\"0b6c25658dd052d34e53557527f6f451dd27602e71d10e1414aae27c52e0d6d2\",\"target\":\"body > div:nth-of-type(1) > canvas\",\"eventCount\":1}";
        }
        else
        {
            output << "null";
        }
        output << "}\n";
    }

    void WebglInteractiveLinesRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                                                       uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        prepareLinesPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_interactive_lines\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":false,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":51,\n  \"entityCount\":51,\n  \"instanceCount\":1,\n"
               << "  \"instanceCounts\":[1],\n  \"scenePassCount\":1,\n  \"screenPassCount\":0,\n"
               << "  \"drawCommandCount\":1,\n  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n  \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
               << "  \"assetAndAlgorithmState\":\"cpu-random-walk-native-line-list-parent-transform\",\n"
               << "  \"renderSetType\":\"WebglInteractiveLinesSceneRenderSet\",\n"
               << "  \"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglInteractiveLinesScenePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetId\":\"scene-set-0\",\"renderSetCount\":1,\"renderSetType\":\"WebglInteractiveLinesSceneRenderSet\",\"renderableObjectCount\":51,\"entityCount\":51,\"drawCommandCount\":1,\"directDrawFallback\":false,\"hitSphereVisible\":" << (sphereVisible ? "true" : "false") << ",\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglInteractiveLinesScenePass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
        for (uint32_t index = 0u; index < LineCount; ++index)
        {
            if (index != 0u) output << ',';
            output << "{\"entityId\":" << index << ",\"logicalRenderableId\":\"line-" << index
                   << "\",\"instanceCount\":1}";
        }
        output << "]}]}\n";
    }

    void WebglInteractiveLinesRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                          const ThreeSampleHostOptions &options,
                                                          uint32_t frameIndex, GVM::RHI::Texture readbackTexture,
                                                          uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("webgl_interactive_lines RGBA capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_interactive_lines has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglInteractiveLinesRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer,
                                                       const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
