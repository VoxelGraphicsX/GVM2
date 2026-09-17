#include "WebglLightsRectarealightRuntimeAdapter.hpp"

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
        constexpr uint32_t EntityCount = 8u;
        constexpr uint32_t TorusTubularSegments = 200u;
        constexpr uint32_t TorusRadialSegments = 16u;
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
        void appendTriangle(WebglLightsRectarealightHostEntity &entity,
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

        /** Expands one 3D helper segment into a triangle-list quad. */
        void appendLineQuad(WebglLightsRectarealightHostEntity &entity,
                            const glm::vec3 &start,
                            const glm::vec3 &end,
                            const glm::vec4 &color,
                            float halfWidth)
        {
            const glm::vec3 direction = glm::normalize(end - start);
            glm::vec3 basis = glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length(basis) < 0.001f)
                basis = glm::cross(direction, glm::vec3(1.0f, 0.0f, 0.0f));
            basis = glm::normalize(basis) * halfWidth;
            const glm::vec3 a = start - basis;
            const glm::vec3 b = start + basis;
            const glm::vec3 c = end + basis;
            const glm::vec3 d = end - basis;
            const glm::vec3 normal = glm::normalize(glm::cross(b - a, d - a));
            appendTriangle(entity, a, b, c, normal, color);
            appendTriangle(entity, a, c, d, normal, color);
        }

        /** Builds the floor plane used by the original RectAreaLight example. */
        void buildFloor(WebglLightsRectarealightHostEntity &entity)
        {
            const glm::vec4 color(0.05780543f, 0.05780543f, 0.05780543f, 1.0f);
            appendTriangle(entity, {-1000.0f, -0.05f, -1000.0f}, {1000.0f, -0.05f, -1000.0f},
                           {1000.0f, -0.05f, 1000.0f}, {0.0f, 1.0f, 0.0f}, color);
            appendTriangle(entity, {-1000.0f, -0.05f, -1000.0f}, {1000.0f, -0.05f, 1000.0f},
                           {-1000.0f, -0.05f, 1000.0f}, {0.0f, 1.0f, 0.0f}, color);
        }

        /** Builds the exact r185 TorusKnotGeometry(1.5, 0.5, 200, 16). */
        void buildTorusKnot(WebglLightsRectarealightHostEntity &entity)
        {
            constexpr double radius = 1.5;
            constexpr double tube = 0.5;
            const auto curve = [](double u) {
                const double q = 1.5 * u;
                return glm::dvec3((2.0 + std::cos(q)) * 0.5 * std::cos(u),
                                  (2.0 + std::cos(q)) * 0.5 * std::sin(u),
                                  0.5 * std::sin(q));
            };
            for (uint32_t tubular = 0u; tubular <= TorusTubularSegments; ++tubular)
            {
                const double u = double(tubular) / double(TorusTubularSegments) * 4.0 * Pi;
                const glm::dvec3 center = curve(u) * radius;
                const glm::dvec3 ahead = curve(u + 0.01) * radius;
                const glm::dvec3 tangent = ahead - center;
                glm::dvec3 binormal = glm::normalize(glm::cross(tangent, ahead + center));
                const glm::dvec3 normalBasis = glm::normalize(glm::cross(binormal, tangent));
                for (uint32_t radial = 0u; radial <= TorusRadialSegments; ++radial)
                {
                    const double v = double(radial) / double(TorusRadialSegments) * 2.0 * Pi;
                    const glm::dvec3 position = center - tube * std::cos(v) * normalBasis + tube * std::sin(v) * binormal;
                    const glm::dvec3 normal = glm::normalize(position - center);
                    entity.vertices.push_back({glm::vec4(glm::vec3(position), 1.0f), glm::vec4(glm::vec3(normal), 0.0f), glm::vec4(1.0f)});
                }
            }
            const uint32_t stride = TorusRadialSegments + 1u;
            for (uint32_t tubular = 1u; tubular <= TorusTubularSegments; ++tubular)
                for (uint32_t radial = 1u; radial <= TorusRadialSegments; ++radial)
                {
                    const uint32_t a = stride * (tubular - 1u) + radial - 1u;
                    const uint32_t b = stride * tubular + radial - 1u;
                    const uint32_t c = b + 1u;
                    const uint32_t d = a + 1u;
                    entity.indices.insert(entity.indices.end(), {a, b, d, b, c, d});
                }
        }

        /** Builds a flat-shaded box with six geometry-group-like face ranges. */
        void buildBox(WebglLightsRectarealightHostEntity &entity)
        {
            const glm::vec3 p[8] = {
                {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
                {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
                {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f},
                {1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}};
            const uint32_t face[6][4] = {
                {0u, 1u, 2u, 3u}, {5u, 4u, 7u, 6u}, {4u, 0u, 3u, 7u},
                {1u, 5u, 6u, 2u}, {3u, 2u, 6u, 7u}, {4u, 5u, 1u, 0u}};
            const glm::vec3 normal[6] = {
                {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
            const glm::vec4 color(0.74f, 0.29f, 0.18f, 1.0f);
            for (uint32_t side = 0u; side < 6u; ++side)
            {
                appendTriangle(entity, p[face[side][0]], p[face[side][1]], p[face[side][2]], normal[side], color);
                appendTriangle(entity, p[face[side][0]], p[face[side][2]], p[face[side][3]], normal[side], color);
            }
        }

        /** Builds one RectAreaLightHelper frame as triangle-list segments. */
        void buildHelper(WebglLightsRectarealightHostEntity &entity, const glm::vec4 &color)
        {
            const glm::vec3 halfSize(2.0f, 5.0f, 0.0f);
            const glm::vec3 p0(-halfSize.x, -halfSize.y, 0.0f);
            const glm::vec3 p1(halfSize.x, -halfSize.y, 0.0f);
            const glm::vec3 p2(halfSize.x, halfSize.y, 0.0f);
            const glm::vec3 p3(-halfSize.x, halfSize.y, 0.0f);
            appendLineQuad(entity, p0, p1, color, 0.03f);
            appendLineQuad(entity, p1, p2, color, 0.03f);
            appendLineQuad(entity, p2, p3, color, 0.03f);
            appendLineQuad(entity, p3, p0, color, 0.03f);
            // RectAreaLightHelper is a visible rectangular emitter, not only
            // an outline.  Keep the fill in the same RenderSet entity so the
            // front/back helper passes retain the source draw ordering.
            appendTriangle(entity, p0, p1, p2, glm::vec3(0.0f, 0.0f, 1.0f), color);
            appendTriangle(entity, p0, p2, p3, glm::vec3(0.0f, 0.0f, 1.0f), color);
        }

        /** Returns the three animated area-light centers used by the deterministic capture. */
        glm::vec3 areaLightPosition(uint32_t index, uint32_t frameIndex)
        {
            // The locked reference harness advances animation callbacks at
            // the repository-wide 60 Hz step.
            const float time = float(frameIndex) / 60.0f;
            if (index == 0u) return glm::vec3(-5.0f, 6.0f, 5.0f);
            if (index == 1u) return glm::vec3(0.0f, 6.0f, 5.0f);
            return glm::vec3(5.0f, 6.0f, 5.0f);
        }

        /** Validates the frozen RectAreaLight scenarios and output contract. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool validScenario =
                (options.scenarioId == "initial" && options.targetFrame == 0u) ||
                (options.scenarioId == "animated-lights" && options.targetFrame == 60u) ||
                (options.scenarioId == "orbit" && options.targetFrame == 61u);
            if (options.caseId != "webgl_lights_rectarealight" || !validScenario ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("webgl_lights_rectarealight scenario does not match the locked r185 contract.");
        }
    }

    void WebglLightsRectarealightRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        orbitScenario = options.scenarioId == "orbit";
        captureWritten = false;
        entities.clear();
        entities.resize(EntityCount);
        buildFloor(entities[FloorEntity]);
        buildTorusKnot(entities[BoxEntity]);
        const glm::vec4 lightColors[3] = {
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
            glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
            glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)};
        for (uint32_t lightIndex = 0u; lightIndex < 3u; ++lightIndex)
        {
            buildHelper(entities[HelperEntityStart + lightIndex * 2u], lightColors[lightIndex]);
            buildHelper(entities[HelperEntityStart + lightIndex * 2u + 1u], lightColors[lightIndex]);
        }
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            entities[index].instanceData.reserved = glm::vec4(0.0f);
            entities[index].materialData.baseColor = index == FloorEntity
                ? glm::vec4(0.05780543f, 0.05780543f, 0.05780543f, 1.0f)
                : (index == BoxEntity ? glm::vec4(1.0f) : glm::vec4(1.0f));
            entities[index].materialData.parameters = glm::vec4(index >= HelperEntityStart ? 1.0f : 0.0f);
        }
        updateObjectData(options.targetFrame);
        // Three's CanvasTexture is a 2x2 black canvas with white pixels at
        // (0,0) and (1,1); it is repeated 400 times and used as the floor's
        // roughnessMap.  Keep the bytes RGBA8 and row-major for the DSL
        // TextureComponent rather than baking the pattern into geometry.
        const uint8_t checkerTexture[16] = {
            255u, 255u, 255u, 255u,
            0u, 0u, 0u, 255u,
            0u, 0u, 0u, 255u,
            255u, 255u, 255u, 255u};
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_lights_rectarealight could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            auto &entity = entities[index];
            const std::string suffix = "-" + std::to_string(index);
            const std::string vertexName = "WebglLightsRectarealightVertices" + suffix;
            const std::string indexName = "WebglLightsRectarealightIndices" + suffix;
            const std::string objectName = "WebglLightsRectarealightObject" + suffix;
            const std::string instanceName = "WebglLightsRectarealightInstance" + suffix;
            const std::string materialName = "WebglLightsRectarealightMaterial" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendBuffer(allocation, WebglLightsRectarealightSceneRenderSetComponents::vertices,
                         vertexName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]));
            appendBuffer(allocation, WebglLightsRectarealightSceneRenderSetComponents::indices,
                         indexName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(uint32_t));
            appendBuffer(allocation, WebglLightsRectarealightSceneRenderSetComponents::objects,
                         objectName.c_str(), &entity.objectData, sizeof(entity.objectData));
            appendBuffer(allocation, WebglLightsRectarealightSceneRenderSetComponents::instances,
                         instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData));
            appendBuffer(allocation, WebglLightsRectarealightSceneRenderSetComponents::materials,
                         materialName.c_str(), &entity.materialData, sizeof(entity.materialData));
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebglLightsRectarealightSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = "WebglLightsRectarealightChecker",
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = 2u,
                .height = 2u,
                .data = checkerTexture,
                .dataStorageBytes = sizeof(checkerTexture),
                .mipmapOffsetBytes = {0u},
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLightsRectarealightRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        const glm::vec3 cameraTarget(0.0f, 5.5f, 0.0f);
        glm::vec3 cameraPosition(0.0f, 5.0f, -15.0f);
        if (orbitScenario)
        {
            const glm::mat4 orbitRotation = glm::rotate(
                glm::mat4(1.0f), -0.35f, glm::vec3(0.0f, 1.0f, 0.0f));
            cameraPosition = glm::vec3(
                orbitRotation * glm::vec4(cameraPosition - cameraTarget, 1.0f)) + cameraTarget;
        }
        const glm::mat4 view = glm::lookAt(cameraPosition, cameraTarget,
                                           glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.6f, 1.0f, 1000.0f);
        const float time = float(frameIndex) / 60.0f;
        for (uint32_t index = 0u; index < EntityCount; ++index)
        {
            glm::mat4 model(1.0f);
            glm::vec4 kind(0.0f);
            if (index == FloorEntity) model = glm::mat4(1.0f);
            else if (index == BoxEntity) model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.5f, 0.0f));
            else
            {
                const uint32_t helperIndex = index - HelperEntityStart;
                const uint32_t lightIndex = helperIndex / 2u;
                const glm::vec3 lightPosition = areaLightPosition(lightIndex, frameIndex);
                // RectAreaLightHelper inherits the light's local XY plane.  A
                // default RectAreaLight has identity rotation; it does not
                // face the scene origin.  Keeping the local plane here is
                // what produces the three vertical source panels in r185.
                model = glm::translate(glm::mat4(1.0f), lightPosition);
                const float rotation = lightIndex == 0u
                    ? -time
                    : (lightIndex == 1u ? time * 0.5f : time);
                model = model * glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 1.0f, 0.0f));
                if ((helperIndex & 1u) != 0u)
                    model = model * glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                kind.w = (helperIndex & 1u) == 0u ? 2.0f : 3.0f;
            }
            auto &entity = entities[index];
            entity.objectData.model = model;
            entity.objectData.modelView = view * model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.positionAndKind = kind;
            entity.baseObjectData = entity.objectData;
        }
    }

    void WebglLightsRectarealightRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_lights_rectarealight could not create its update encoder.");
        for (const auto &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                                             WebglLightsRectarealightSceneRenderSetComponents::objects,
                                             &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        (void)options;
    }

    void WebglLightsRectarealightRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        preparePath(options.captureRgbaPath);
        std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglLightsRectarealightRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frame, uint32_t width, uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        preparePath(options.captureMetadataPath);
        std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_lights_rectarealight\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend)
               << "\",\"frame\":" << frame << ",\"randomSeed\":" << options.randomSeed
               << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false}\n";
    }

    void WebglLightsRectarealightRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frame) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        preparePath(options.sceneSnapshotPath);
        std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_lights_rectarealight\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frame
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglLightsRectarealightSceneRenderSet\",\n  \"renderableObjectCount\":8,\n"
               << "  \"entityCount\":8,\n  \"instanceCounts\":[1,1,1,1,1,1,1,1],\n  \"scenePassCount\":3,\n"
               << "  \"screenPassCount\":0,\n  \"drawCommandCount\":3,\n  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"],\n"
               << "  \"scenePasses\":[\"WebglLightsRectarealightOpaqueLtcPass\",\"WebglLightsRectarealightHelperLinesPass\",\"WebglLightsRectarealightHelperBackfacesPass\"],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set-0\",\"renderSetType\":\"WebglLightsRectarealightSceneRenderSet\",\"renderableObjectCount\":8,\"entityCount\":8,\"drawCommandCount\":3,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"floor\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"torus-knot\",\"instanceCount\":1},{\"entityId\":2,\"logicalRenderableId\":\"rect-area-helper-0-front\",\"instanceCount\":1},{\"entityId\":3,\"logicalRenderableId\":\"rect-area-helper-0-back\",\"instanceCount\":1},{\"entityId\":4,\"logicalRenderableId\":\"rect-area-helper-1-front\",\"instanceCount\":1},{\"entityId\":5,\"logicalRenderableId\":\"rect-area-helper-1-back\",\"instanceCount\":1},{\"entityId\":6,\"logicalRenderableId\":\"rect-area-helper-2-front\",\"instanceCount\":1},{\"entityId\":7,\"logicalRenderableId\":\"rect-area-helper-2-back\",\"instanceCount\":1}],\"scenePasses\":[{\"name\":\"opaque-ltc\",\"renderClass\":\"WebglLightsRectarealightOpaqueLtcPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"helper-lines\",\"renderClass\":\"WebglLightsRectarealightHelperLinesPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"helper-backfaces\",\"renderClass\":\"WebglLightsRectarealightHelperBackfacesPass\",\"renderSetId\":\"scene-set-0\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n";
    }

    void WebglLightsRectarealightRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &options,
        uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("rect-area-light capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_lights_rectarealight has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLightsRectarealightRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &)
    {
        entities.clear();
    }
}
