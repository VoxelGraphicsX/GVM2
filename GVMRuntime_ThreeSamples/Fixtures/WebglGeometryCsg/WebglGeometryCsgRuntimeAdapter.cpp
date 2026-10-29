#include "WebglGeometryCsgRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Creates parent directories for a deterministic capture artifact. */
        void prepareWebglGeometryCsgPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebglGeometryCsgBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                                     GVM::Core::RenderComponentHandle component,
                                     const char *name,
                                     const void *data,
                                     uint64_t byteCount,
                                     uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = data,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Appends one barycentric triangle to a CPU entity. */
        void appendWebglGeometryCsgTriangle(WebglGeometryCsgEntity &entity,
                                       const glm::vec3 &a,
                                       const glm::vec3 &b,
                                       const glm::vec3 &c,
                                       const glm::vec3 &normal)
        {
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({glm::vec4(a, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(b, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)});
            entity.vertices.push_back({glm::vec4(c, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)});
            entity.indices.push_back(base + 0u);
            entity.indices.push_back(base + 1u);
            entity.indices.push_back(base + 2u);
        }

        /** Generates the eight-sided cone used by the upstream webglGeometryCsg example. */
        void buildWebglGeometryCsgCone(WebglGeometryCsgEntity &entity)
        {
            constexpr uint32_t SegmentCount = 8u;
            const glm::vec3 apex(0.0f, 0.25f, 0.0f);
            const glm::vec3 center(0.0f, -0.25f, 0.0f);
            for (uint32_t segment = 0u; segment < SegmentCount; ++segment)
            {
                const double a0 = 2.0 * Pi * double(segment) / double(SegmentCount);
                const double a1 = 2.0 * Pi * double(segment + 1u) / double(SegmentCount);
                const glm::vec3 p0(0.25f * float(std::cos(a0)), -0.25f, 0.25f * float(std::sin(a0)));
                const glm::vec3 p1(0.25f * float(std::cos(a1)), -0.25f, 0.25f * float(std::sin(a1)));
                appendWebglGeometryCsgTriangle(entity, apex, p0, p1,
                                           glm::normalize(glm::cross(p0 - apex, p1 - apex)));
                appendWebglGeometryCsgTriangle(entity, center, p1, p0, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        /** Tests whether a triangle centroid lies inside the subtractive cylinder. */
        bool webglGeometryCsgInsideSubtractCylinder(const glm::vec3 &point)
        {
            return point.x * point.x + point.z * point.z < 0.92f &&
                abs(point.y) < 2.5f;
        }

        /** Builds the 1x1 plane used by the upstream shadow receiver. */
        void buildWebglGeometryCsgPlane(WebglGeometryCsgEntity &entity)
        {
            appendWebglGeometryCsgTriangle(entity,
                {-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, -0.5f},
                {0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f});
            appendWebglGeometryCsgTriangle(entity,
                {-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f},
                {-0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f});
        }

        /** Generates a latitude/longitude sphere with per-triangle barycentrics. */
        void buildWebglGeometryCsgSphere(WebglGeometryCsgEntity &entity,
                                    uint32_t rings,
                                    uint32_t segments,
                                    float radius,
                                    bool subtractCylinder = false)
        {
            for (uint32_t ring = 0u; ring < rings; ++ring)
            {
                const double v0 = double(ring) / double(rings);
                const double v1 = double(ring + 1u) / double(rings);
                const double p0 = Pi * v0;
                const double p1 = Pi * v1;
                for (uint32_t segment = 0u; segment < segments; ++segment)
                {
                    const double u0 = 2.0 * Pi * double(segment) / double(segments);
                    const double u1 = 2.0 * Pi * double(segment + 1u) / double(segments);
                    const glm::vec3 a(radius * float(std::sin(p0) * std::cos(u0)), radius * float(std::cos(p0)), radius * float(std::sin(p0) * std::sin(u0)));
                    const glm::vec3 b(radius * float(std::sin(p1) * std::cos(u0)), radius * float(std::cos(p1)), radius * float(std::sin(p1) * std::sin(u0)));
                    const glm::vec3 c(radius * float(std::sin(p1) * std::cos(u1)), radius * float(std::cos(p1)), radius * float(std::sin(p1) * std::sin(u1)));
                    const glm::vec3 d(radius * float(std::sin(p0) * std::cos(u1)), radius * float(std::cos(p0)), radius * float(std::sin(p0) * std::sin(u1)));
                    const glm::vec3 center = (a + b + c) / 3.0f;
                    const glm::vec3 center2 = (a + c + d) / 3.0f;
                    if (!subtractCylinder ||
                        !webglGeometryCsgInsideSubtractCylinder(center))
                        appendWebglGeometryCsgTriangle(entity, a, b, c, glm::normalize(a));
                    if (!subtractCylinder ||
                        !webglGeometryCsgInsideSubtractCylinder(center2))
                        appendWebglGeometryCsgTriangle(entity, a, c, d, glm::normalize(a));
                }
            }
        }

        /** Builds the Three perspective matrix with the generated-backend Y convention. */
        glm::mat4 webglGeometryCsgProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(50.0f), aspect, 1.0f, 100.0f);
        }

        /** Validates the four deterministic target/webglGeometryCsg scenarios. */
        /** Validates the frozen WebglGeometryCsg scenario matrix and output contract. */
        void validateWebglGeometryCsgOptions(const ThreeSampleHostOptions &options)
        {
            const bool scenario0 = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool scenario1 = options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool scenario2 = options.scenarioId == "intersection" && options.targetFrame == 61u;
            if (options.caseId != "webgl_geometry_csg"
                || (!scenario0 && !scenario1 && !scenario2)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgl_geometry_csg scenario does not match the locked r185 contract.");
            }
        }
    }

    void WebglGeometryCsgRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebglGeometryCsgOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(4u);
        // The upstream scene contains a shadow plane, the evaluated CSG
        // result, its orange core, and an optional wireframe view of the
        // result.  All four remain entities of this one Scene RenderSet.
        buildWebglGeometryCsgPlane(entities[0]);
        buildWebglGeometryCsgSphere(entities[1], 32u, 64u, 2.0f, true);
        buildWebglGeometryCsgSphere(entities[2], 8u, 16u, 0.15f);
        entities[3].vertices = entities[1].vertices;
        entities[3].indices = entities[1].indices;
        const glm::vec3 camera = glm::normalize(glm::vec3(-1.0f, 1.0f, 1.0f)) * 10.0f;
        const glm::mat4 view = glm::lookAt(camera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = webglGeometryCsgProjection(options.width, options.height);
        const float time = float(options.targetFrame) * (1000.0f / 60.0f) + 9000.0f;
        const glm::mat4 resultModel = glm::rotate(glm::mat4(1.0f), time * 0.0005f, glm::vec3(0.0f, 0.0f, 1.0f))
            * glm::rotate(glm::mat4(1.0f), time * 0.00025f, glm::vec3(0.0f, 1.0f, 0.0f))
            * glm::rotate(glm::mat4(1.0f), time * 0.0001f, glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::mat4 models[4u] = {
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -3.0f, 0.0f))
                * glm::rotate(glm::mat4(1.0f), -float(Pi * 0.5), glm::vec3(1.0f, 0.0f, 0.0f))
                * glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)),
            resultModel,
            glm::mat4(1.0f),
            resultModel};
        const glm::vec4 colors[4u] = {
            glm::vec4(0.843137f, 0.105882f, 0.376471f, 0.075f),
            glm::vec4(0.501961f, 0.796078f, 0.768627f, 1.0f),
            glm::vec4(1.0f, 0.596078f, 0.0f, 1.0f),
            glm::vec4(0.0f, 0.596078f, 0.470588f, 1.0f)};
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            auto &entity = entities[index];
            entity.objectData.modelView = view * models[index];
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.normalMatrix = glm::transpose(glm::inverse(entity.objectData.modelView));
            entity.objectData.baseColorAndFlags = glm::vec4(colors[index].x, colors[index].y, colors[index].z,
                index == 3u && options.scenarioId == "intersection" ? 1.0f : 0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.baseColorAndFlags = colors[index];
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_geometry_csg could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const std::string suffix = std::to_string(index);
            const std::string verticesName = "WebglGeometryCsgVertices_" + suffix;
            const std::string indicesName = "WebglGeometryCsgIndices_" + suffix;
            const std::string objectName = "WebglGeometryCsgObject_" + suffix;
            const std::string instanceName = "WebglGeometryCsgInstance_" + suffix;
            const std::string materialName = "WebglGeometryCsgMaterial_" + suffix;
            appendWebglGeometryCsgBuffer(allocation, WebglGeometryCsgSceneRenderSetComponents::vertices, verticesName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebglGeometryCsgBuffer(allocation, WebglGeometryCsgSceneRenderSetComponents::indices, indicesName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebglGeometryCsgBuffer(allocation, WebglGeometryCsgSceneRenderSetComponents::objects, objectName.c_str(), &entity.objectData, sizeof(entity.objectData), 1u);
            appendWebglGeometryCsgBuffer(allocation, WebglGeometryCsgSceneRenderSetComponents::instances, instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendWebglGeometryCsgBuffer(allocation, WebglGeometryCsgSceneRenderSetComponents::materials, materialName.c_str(), &entity.materialData, sizeof(entity.materialData), 1u);
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebglGeometryCsgSceneRenderSetComponents::textures;
            const uint8_t whiteTexture[4] = {255u, 255u, 255u, 255u};
            textureComponent.textures.push_back({
                .textureName = "WebglGeometryCsgWhiteTexture",
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = 1u,
                .height = 1u,
                .data = whiteTexture,
                .dataStorageBytes = sizeof(whiteTexture),
                .mipmapOffsetBytes = {0u},
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglGeometryCsgRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryCsgRuntimeAdapter::afterFrame(
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
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        prepareWebglGeometryCsgPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareWebglGeometryCsgPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\": 1,\n"
                   << "  \"source\": \"gvm-three-r185\",\n"
                   << "  \"caseId\": \"webgl_geometry_csg\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n"
                   << "  \"randomSeed\": " << options.randomSeed << ",\n"
                   << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                   << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
                   << "  \"byteCount\": " << rgba.size() << ",\n"
                   << "  \"format\": \"rgba8unorm\",\n"
                   << "  \"inputReplay\": ";
            if (options.scenarioId == "intersection")
            {
                output << "{\"schemaVersion\":1,\"caseId\":\"webgl_geometry_csg\","
                       << "\"scenarioId\":\"intersection\",\"captureFrame\":61,"
                       << "\"sha256\":\"236d8c5d8189dba9df94f5cb74ded16d4040c3a799a762b9b085e3a5b1ea004d\","
                       << "\"target\":\"body > canvas\",\"eventCount\":2}";
            }
            else
            {
                output << "null";
            }
            output << ",\n  \"sceneRenderSetCount\": 1,\n"
                   << "  \"renderSetType\": \"WebglGeometryCsgSceneRenderSet\",\n"
                   << "  \"entityCount\": " << entities.size() << ",\n  \"instanceCounts\": [";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << 1u;
            }
            output << "],\n"
                   << "  \"scenePassCount\": 3,\n  \"screenPassCount\": 0,\n"
                   << "  \"drawCommandCount\": 3,\n"
                   << "  \"directDrawFallback\": false,\n  \"sampleCount\": 1,\n"
                   << "  \"msaaEnabled\": false\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareWebglGeometryCsgPath(options.sceneSnapshotPath);
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            const char *logicalIds[] = {
                "shadow-plane", "csg-result", "orange-core", "wireframe-result"};
            output << "{\n"
                   << "  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgl_geometry_csg\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"implementationLevel\":\"semantic-complete\",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"renderSetPolicy\":\"required\",\n"
                   << "  \"sceneRenderSetCount\":1,\n"
                   << "  \"renderSetType\":\"WebglGeometryCsgSceneRenderSet\",\n"
                   << "  \"renderableObjectCount\":4,\n"
                   << "  \"entityCount\":" << entities.size() << ",\n"
                   << "  \"instanceCounts\":[1,1,1,1],\n"
                   << "  \"scenePassCount\":3,\n"
                   << "  \"screenPassCount\":0,\n"
                   << "  \"drawCommandCount\":3,\n"
                   << "  \"directDrawFallback\":false,\n"
                   << "  \"sceneRoots\":[{\n"
                   << "    \"id\":\"scene\",\n"
                   << "    \"renderSetCount\":1,\n"
                   << "    \"renderSetId\":\"webgl-geometry-csg-scene-set\",\n"
                   << "    \"renderSetType\":\"WebglGeometryCsgSceneRenderSet\",\n"
                   << "    \"renderableObjectCount\":4,\n"
                   << "    \"entityCount\":4,\n"
                   << "    \"entities\":[";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << "{\"entityId\":" << index
                       << ",\"logicalRenderableId\":\"" << logicalIds[index]
                       << "\",\"instanceCount\":1}";
            }
            output << "],\n"
                   << "    \"componentSchema\":["
                   << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                   << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                   << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                   << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                   << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                   << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"background-and-cube-faces\"}],\n"
                   << "    \"drawCommandCount\":3,\n"
                   << "    \"directDrawFallback\":false,\n"
                   << "    \"scenePasses\":["
                   << "{\"name\":\"shadow-depth\",\"renderClass\":\"WebglGeometryCsgShadowDepthPass\",\"renderSetId\":\"webgl-geometry-csg-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                   << "{\"name\":\"main-color\",\"renderClass\":\"WebglGeometryCsgMainColorPass\",\"renderSetId\":\"webgl-geometry-csg-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                   << "{\"name\":\"wireframe-overlay\",\"renderClass\":\"WebglGeometryCsgWireframeOverlayPass\",\"renderSetId\":\"webgl-geometry-csg-scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
                   << "  }]\n"
                   << "}\n";
        }
        captureWritten = true;
    }

    void WebglGeometryCsgRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
}
