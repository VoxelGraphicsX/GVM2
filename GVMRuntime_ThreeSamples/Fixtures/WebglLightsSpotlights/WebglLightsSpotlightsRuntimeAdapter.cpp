#include "WebglLightsSpotlightsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr uint32_t FloorEntity = 0u;
        constexpr uint32_t BoxEntity = 1u;
        constexpr uint32_t HelperEntityStart = 2u;
        constexpr uint32_t EntityCount = 5u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Creates parent directories for one requested capture artifact. */
        void preparePath(const eastl::string &value)
        {
            if (value.empty()) return;
            const std::filesystem::path path(value.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed RenderSet buffer payload. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                          GVM::Core::RenderComponentHandle component,
                          const char *name,
                          const void *data,
                          uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = data,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }

        /** Appends one flat-shaded triangle to an entity's normalized geometry. */
        void appendTriangle(WebglLightsSpotlightsHostEntity &entity,
                            const glm::vec3 &a,
                            const glm::vec3 &b,
                            const glm::vec3 &c,
                            const glm::vec3 &normal,
                            const glm::vec4 &color)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(a, 1.0f), glm::vec4(normal, 0.0f), color});
            entity.vertices.push_back({glm::vec4(b, 1.0f), glm::vec4(normal, 0.0f), color});
            entity.vertices.push_back({glm::vec4(c, 1.0f), glm::vec4(normal, 0.0f), color});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
            entity.indices.push_back(base + 2u);
        }

        /** Appends one helper segment to the shared line-list geometry. */
        void appendLineSegment(WebglLightsSpotlightsHostEntity &entity,
                            const glm::vec3 &start,
                            const glm::vec3 &end,
                            const glm::vec4 &color)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(start, 1.0f), glm::vec4(0.0f), color});
            entity.vertices.push_back({glm::vec4(end, 1.0f), glm::vec4(0.0f), color});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
        }

        /** Builds the floor plane used by the original SpotLight example. */
        void buildFloor(WebglLightsSpotlightsHostEntity &entity)
        {
            const glm::vec4 color(0.2158605f, 0.2158605f, 0.2158605f, 1.0f);
            appendTriangle(entity, {-50.0f, 0.0f, -50.0f}, {50.0f, 0.0f, -50.0f},
                           {50.0f, 0.0f, 50.0f}, {0.0f, 1.0f, 0.0f}, color);
            appendTriangle(entity, {-50.0f, 0.0f, -50.0f}, {50.0f, 0.0f, 50.0f},
                           {-50.0f, 0.0f, 50.0f}, {0.0f, 1.0f, 0.0f}, color);
        }

        /** Builds a flat-shaded box with six geometry-group-like face ranges. */
        void buildBox(WebglLightsSpotlightsHostEntity &entity)
        {
            const glm::vec3 p[8] = {
                {-0.15f, -0.05f, -0.10f}, {0.15f, -0.05f, -0.10f},
                {0.15f, 0.05f, -0.10f}, {-0.15f, 0.05f, -0.10f},
                {-0.15f, -0.05f, 0.10f}, {0.15f, -0.05f, 0.10f},
                {0.15f, 0.05f, 0.10f}, {-0.15f, 0.05f, 0.10f}};
            const uint32_t face[6][4] = {
                {0u, 1u, 2u, 3u}, {5u, 4u, 7u, 6u}, {4u, 0u, 3u, 7u},
                {1u, 5u, 6u, 2u}, {3u, 2u, 6u, 7u}, {4u, 5u, 1u, 0u}};
            const glm::vec3 normal[6] = {
                {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
            const glm::vec4 color(0.4019778f, 0.4019778f, 0.4019778f, 1.0f);
            for (uint32_t side = 0u; side < 6u; ++side)
            {
                appendTriangle(entity, p[face[side][0]], p[face[side][1]], p[face[side][2]], normal[side], color);
                appendTriangle(entity, p[face[side][0]], p[face[side][2]], p[face[side][3]], normal[side], color);
            }
        }

        /** Builds one SpotLightHelper cone in the helper's local +Z frame. */
        void buildHelper(WebglLightsSpotlightsHostEntity &entity,
                         const glm::vec4 &color)
        {
            constexpr uint32_t SegmentCount = 32u;
            const glm::vec3 apex(0.0f);
            const glm::vec3 axisPoints[] = {
                glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f),
                glm::vec3(-1.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f),
                glm::vec3(0.0f, -1.0f, 1.0f)};
            for (const glm::vec3 &axisPoint : axisPoints)
                appendLineSegment(entity, apex, axisPoint, color);
            for (uint32_t segment = 0u; segment < SegmentCount; ++segment)
            {
                const float a0 = 2.0f * Pi * float(segment) / float(SegmentCount);
                const float a1 = 2.0f * Pi * float(segment + 1u) / float(SegmentCount);
                const glm::vec3 p0(std::cos(a0), std::sin(a0), 1.0f);
                const glm::vec3 p1(std::cos(a1), std::sin(a1), 1.0f);
                appendLineSegment(entity, p0, p1, color);
            }
        }

        /** Returns the deterministic light position after the r185 tween settles. */
        glm::vec3 spotlightPosition(uint32_t index, uint32_t frameIndex)
        {
            if (frameIndex >= 300u)
            {
                if (index == 0u) return glm::vec3(0.40082234f, 1.50536817f, 1.27217227f);
                if (index == 1u) return glm::vec3(1.36555427f, 1.80581802f, 0.31747860f);
                return glm::vec3(-1.28199989f, 1.67207479f, -0.21411502f);
            }
            if (index == 0u) return glm::vec3(1.5f, 4.0f, 4.5f);
            if (index == 1u) return glm::vec3(0.0f, 4.0f, 3.5f);
            return glm::vec3(-1.5f, 4.0f, 4.5f);
        }

        /** Returns the deterministic spotlight angle after the r185 tween settles. */
        float spotlightAngle(uint32_t index, uint32_t frameIndex)
        {
            if (frameIndex < 300u) return 0.3f;
            if (index == 0u) return 0.61946349f;
            if (index == 1u) return 0.32084171f;
            return 0.79910733f;
        }

        /** Returns the deterministic spotlight penumbra after the r185 tween settles. */
        float spotlightPenumbra(uint32_t index, uint32_t frameIndex)
        {
            if (frameIndex < 300u) return 0.2f;
            if (index == 0u) return 1.58710188f;
            if (index == 1u) return 1.40081608f;
            return 1.18841219f;
        }

        /** Builds the world transform used by one SpotLightHelper cone. */
        glm::mat4 spotlightHelperModel(uint32_t index, uint32_t frameIndex)
        {
            const glm::vec3 lightPosition = spotlightPosition(index, frameIndex);
            const glm::vec3 forward = glm::normalize(-lightPosition);
            const glm::vec3 upReference = std::abs(glm::dot(forward, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 right = glm::normalize(glm::cross(upReference, forward));
            const glm::vec3 up = glm::normalize(glm::cross(forward, right));
            glm::mat4 model(1.0f);
            const float coneWidth = 50.0f * std::tan(
                spotlightAngle(index, frameIndex));
            model[0] = glm::vec4(right * coneWidth, 0.0f);
            model[1] = glm::vec4(up * coneWidth, 0.0f);
            model[2] = glm::vec4(forward * 50.0f, 0.0f);
            model[3] = glm::vec4(lightPosition, 1.0f);
            return model;
        }

        /** Validates the frozen SpotLight scenarios and output contract. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool validScenario =
                (options.scenarioId == "initial" && options.targetFrame == 0u) ||
                (options.scenarioId == "tweened" && options.targetFrame == 300u) ||
                (options.scenarioId == "orbit" && options.targetFrame == 301u);
            if (options.caseId != "webgl_lights_spotlights" || !validScenario ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("webgl_lights_spotlights scenario does not match the locked r185 contract.");
        }
    }

    void WebglLightsSpotlightsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        captureWritten = false;
        orbitScenario = options.scenarioId == "orbit";
        entities.clear();
        entities.resize(EntityCount);
        buildFloor(entities[FloorEntity]);
        buildBox(entities[BoxEntity]);
        buildHelper(entities[HelperEntityStart + 0u], glm::vec4(1.0f, 0.21223076f, 0.0f, 1.0f));
        buildHelper(entities[HelperEntityStart + 1u], glm::vec4(0.0f, 1.0f, 0.21223076f, 1.0f));
        buildHelper(entities[HelperEntityStart + 2u], glm::vec4(0.21223076f, 0.0f, 1.0f, 1.0f));
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            entities[index].instanceData.reserved = glm::vec4(0.0f);
            entities[index].materialData.baseColor = index == FloorEntity
                ? glm::vec4(0.2158605f, 0.2158605f, 0.2158605f, 1.0f)
                : (index == BoxEntity
                    ? glm::vec4(0.4019778f, 0.4019778f, 0.4019778f, 1.0f)
                    : glm::vec4(1.0f));
            entities[index].materialData.parameters = glm::vec4(index >= HelperEntityStart ? 1.0f : 0.0f);
        }
        updateObjectData(options.targetFrame);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_lights_spotlights could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            auto &entity = entities[index];
            const std::string suffix = "-" + std::to_string(index);
            const std::string vertexName = "WebglLightsSpotlightsVertices" + suffix;
            const std::string indexName = "WebglLightsSpotlightsIndices" + suffix;
            const std::string objectName = "WebglLightsSpotlightsObject" + suffix;
            const std::string instanceName = "WebglLightsSpotlightsInstance" + suffix;
            const std::string materialName = "WebglLightsSpotlightsMaterial" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendBuffer(allocation, WebglLightsSpotlightsSceneRenderSetComponents::vertices,
                         vertexName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]));
            appendBuffer(allocation, WebglLightsSpotlightsSceneRenderSetComponents::indices,
                         indexName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(uint32_t));
            appendBuffer(allocation, WebglLightsSpotlightsSceneRenderSetComponents::objects,
                         objectName.c_str(), &entity.objectData, sizeof(entity.objectData));
            appendBuffer(allocation, WebglLightsSpotlightsSceneRenderSetComponents::instances,
                         instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData));
            appendBuffer(allocation, WebglLightsSpotlightsSceneRenderSetComponents::materials,
                         materialName.c_str(), &entity.materialData, sizeof(entity.materialData));
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLightsSpotlightsRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        const glm::vec3 cameraTarget(0.0f, 0.5f, 0.0f);
        glm::vec3 cameraPosition(4.6f, 2.2f, -2.1f);
        if (orbitScenario)
        {
            const glm::mat4 orbitRotation = glm::rotate(
                glm::mat4(1.0f), -0.35f, glm::vec3(0.0f, 1.0f, 0.0f));
            cameraPosition = glm::vec3(
                orbitRotation * glm::vec4(cameraPosition - cameraTarget, 1.0f)) + cameraTarget;
        }
        const glm::mat4 view = glm::lookAt(cameraPosition, cameraTarget,
                                           glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(glm::radians(35.0f), 1.6f, 0.1f, 100.0f);
        const glm::vec3 light0Position = spotlightPosition(0u, frameIndex);
        const glm::vec3 light1Position = spotlightPosition(1u, frameIndex);
        const glm::vec3 light2Position = spotlightPosition(2u, frameIndex);
        const glm::vec4 light0PositionAngle(
            light0Position, spotlightAngle(0u, frameIndex));
        const glm::vec4 light1PositionAngle(
            light1Position, spotlightAngle(1u, frameIndex));
        const glm::vec4 light2PositionAngle(
            light2Position, spotlightAngle(2u, frameIndex));
        const glm::vec4 lightPenumbra(
            spotlightPenumbra(0u, frameIndex),
            spotlightPenumbra(1u, frameIndex),
            spotlightPenumbra(2u, frameIndex), 0.0f);
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            glm::mat4 model(1.0f);
            glm::vec4 kind(0.0f);
            if (index == FloorEntity) model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.05f, 0.0f));
            else if (index == BoxEntity) model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f));
            else
            {
                const uint32_t lightIndex = index - HelperEntityStart;
                model = spotlightHelperModel(lightIndex, frameIndex);
                kind.w = 2.0f;
            }
            auto &entity = entities[index];
            entity.objectData.model = model;
            entity.objectData.modelView = view * model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.positionAndKind = kind;
            entity.objectData.light0PositionAngle = light0PositionAngle;
            entity.objectData.light1PositionAngle = light1PositionAngle;
            entity.objectData.light2PositionAngle = light2PositionAngle;
            entity.objectData.lightPenumbra = lightPenumbra;
            entity.baseObjectData = entity.objectData;
        }
    }

    void WebglLightsSpotlightsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_lights_spotlights could not create its update encoder.");
        for (const auto &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                                             WebglLightsSpotlightsSceneRenderSetComponents::objects,
                                             &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        (void)options;
    }

    void WebglLightsSpotlightsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        preparePath(options.captureRgbaPath);
        std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglLightsSpotlightsRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frame, uint32_t width, uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        preparePath(options.captureMetadataPath);
        std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_lights_spotlights\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend)
               << "\",\"frame\":" << frame << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false"
               << ",\"randomSeed\":" << options.randomSeed
               << ",\"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\"inputReplay\":";
        if (options.inputReplayPath.empty()) output << "null";
        else output << "\"" << options.inputReplayPath.c_str() << "\"";
        output << "}\n";
    }

    void WebglLightsSpotlightsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frame) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        preparePath(options.sceneSnapshotPath);
        std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_lights_spotlights\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frame
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglLightsSpotlightsSceneRenderSet\",\n  \"renderableObjectCount\":5,\n"
               << "  \"entityCount\":5,\n  \"instanceCounts\":[1,1,1,1,1],\n  \"scenePassCount\":3,\n"
               << "  \"screenPassCount\":0,\n  \"drawCommandCount\":3,\n  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
               << "  \"sceneRoots\":[{\"name\":\"scene\",\"renderSetRuntimeInstanceCount\":1,\"renderSetType\":\"WebglLightsSpotlightsSceneRenderSet\",\"renderSetId\":\"webgl-lights-spotlights-scene-set\",\"renderableObjectCount\":5,\"entityCount\":5,\"entities\":[{\"id\":0,\"logicalId\":\"floor\",\"instanceCount\":1},{\"id\":1,\"logicalId\":\"box\",\"instanceCount\":1},{\"id\":2,\"logicalId\":\"spotlight-helper-0\",\"instanceCount\":1},{\"id\":3,\"logicalId\":\"spotlight-helper-1\",\"instanceCount\":1},{\"id\":4,\"logicalId\":\"spotlight-helper-2\",\"instanceCount\":1}],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\"drawCommandCount\":3,\"directDrawFallback\":false}],\n"
               << "  \"scenePasses\":[{\"name\":\"three-spot-shadow-depth\",\"renderClass\":\"WebglLightsSpotlightsShadowDepthPass\",\"sceneRoot\":\"scene\",\"renderSetBindingCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"opaque-lit\",\"renderClass\":\"WebglLightsSpotlightsOpaqueLitPass\",\"sceneRoot\":\"scene\",\"renderSetBindingCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"helper-lines\",\"renderClass\":\"WebglLightsSpotlightsHelperLinesPass\",\"sceneRoot\":\"scene\",\"renderSetBindingCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n}\n";
    }

    void WebglLightsSpotlightsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &options,
        uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("spotlight capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_lights_spotlights has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLightsSpotlightsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &)
    {
        entities.clear();
    }
}
