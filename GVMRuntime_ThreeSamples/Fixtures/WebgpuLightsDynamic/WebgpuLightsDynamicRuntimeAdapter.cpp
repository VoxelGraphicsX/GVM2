#include "WebgpuLightsDynamicRuntimeAdapter.hpp"

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
#include <sstream>
#include <string>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Creates parent directories for a deterministic capture artifact. */
        void prepareWebgpuLightsDynamicPath(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebgpuLightsDynamicBuffer(GVM::Core::RenderSetAllocInfo &allocation,
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
        void appendWebgpuLightsDynamicTriangle(WebgpuLightsDynamicEntity &entity,
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

        /** Generates the eight-sided cone used by the upstream webgpuLightsDynamic example. */
        void buildWebgpuLightsDynamicCone(WebgpuLightsDynamicEntity &entity)
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
                appendWebgpuLightsDynamicTriangle(entity, apex, p0, p1,
                                           glm::normalize(glm::cross(p0 - apex, p1 - apex)));
                appendWebgpuLightsDynamicTriangle(entity, center, p1, p0, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        /** Generates a latitude/longitude sphere with per-triangle barycentrics. */
        void buildWebgpuLightsDynamicSphere(WebgpuLightsDynamicEntity &entity,
                                    uint32_t rings,
                                    uint32_t segments,
                                    float radius)
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
                    appendWebgpuLightsDynamicTriangle(entity, a, b, c, glm::normalize(a));
                    appendWebgpuLightsDynamicTriangle(entity, a, c, d, glm::normalize(a));
                }
            }
        }

        /** Builds the 120-unit floor plane from the upstream dynamic-lighting scene. */
        void buildWebgpuLightsDynamicFloor(WebgpuLightsDynamicEntity &entity)
        {
            appendWebgpuLightsDynamicTriangle(entity, {-60.0f, 0.0f, -60.0f}, {60.0f, 0.0f, -60.0f}, {60.0f, 0.0f, 60.0f}, {0.0f, 1.0f, 0.0f});
            appendWebgpuLightsDynamicTriangle(entity, {-60.0f, 0.0f, -60.0f}, {60.0f, 0.0f, 60.0f}, {-60.0f, 0.0f, 60.0f}, {0.0f, 1.0f, 0.0f});
        }

        /** Builds a compact flat-shaded unit box for one shared geometry slot. */
        void buildWebgpuLightsDynamicBox(WebgpuLightsDynamicEntity &entity)
        {
            const glm::vec3 p[8] = {{-0.6f, -0.6f, -0.6f}, {0.6f, -0.6f, -0.6f}, {0.6f, 0.6f, -0.6f}, {-0.6f, 0.6f, -0.6f},
                                    {-0.6f, -0.6f, 0.6f}, {0.6f, -0.6f, 0.6f}, {0.6f, 0.6f, 0.6f}, {-0.6f, 0.6f, 0.6f}};
            const uint32_t f[6][4] = {{0u, 1u, 2u, 3u}, {5u, 4u, 7u, 6u}, {4u, 0u, 3u, 7u}, {1u, 5u, 6u, 2u}, {3u, 2u, 6u, 7u}, {4u, 5u, 1u, 0u}};
            const glm::vec3 n[6] = {{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
            for (uint32_t side = 0u; side < 6u; ++side)
            {
                appendWebgpuLightsDynamicTriangle(entity, p[f[side][0]], p[f[side][1]], p[f[side][2]], n[side]);
                appendWebgpuLightsDynamicTriangle(entity, p[f[side][0]], p[f[side][2]], p[f[side][3]], n[side]);
            }
        }

        /** Resolves one HSL hue channel for deterministic MeshStandard colors. */
        float webgpuLightsDynamicHueChannel(float p, float q, float hue)
        {
            float wrapped = hue;
            if (wrapped < 0.0f) wrapped += 1.0f;
            if (wrapped > 1.0f) wrapped -= 1.0f;
            if (wrapped < 1.0f / 6.0f) return p + (q - p) * 6.0f * wrapped;
            if (wrapped < 0.5f) return q;
            if (wrapped < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - wrapped) * 6.0f;
            return p;
        }

        /** Converts the source HSL material recipe to deterministic linear RGB. */
        glm::vec3 webgpuLightsDynamicHsl(float hue, float saturation, float lightness)
        {
            const float q = lightness < 0.5f ? lightness * (1.0f + saturation) : lightness + saturation - lightness * saturation;
            const float p = 2.0f * lightness - q;
            return glm::vec3(webgpuLightsDynamicHueChannel(p, q, hue + 1.0f / 3.0f),
                             webgpuLightsDynamicHueChannel(p, q, hue),
                             webgpuLightsDynamicHueChannel(p, q, hue - 1.0f / 3.0f));
        }

        /** Builds the Three perspective matrix with the generated-backend Y convention. */
        glm::mat4 webgpuLightsDynamicProjection(uint32_t width, uint32_t height)
        {
            const float aspect = float(width) / float(height);
            return glm::perspective(glm::radians(50.0f), aspect, 0.1f, 200.0f);
        }

        /** Validates the four deterministic target/webgpuLightsDynamic scenarios. */
        /** Validates the frozen WebgpuLightsDynamic scenario matrix and output contract. */
        void validateWebgpuLightsDynamicOptions(const ThreeSampleHostOptions &options)
        {
            const bool scenario0 = options.scenarioId == "initial-seeded" && options.targetFrame == 0u;
            const bool scenario1 = options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool scenario2 = options.scenarioId == "add-remove" && options.targetFrame == 61u;
            const bool scenario3 = options.scenarioId == "auto-add-tick" && options.targetFrame == 31u;
            const bool scenario4 = options.scenarioId == "static-mode" && options.targetFrame == 1u;
            const bool scenario5 = options.scenarioId == "remove-all" && options.targetFrame == 1u;
            const bool scenario6 = options.scenarioId == "overflow-boundary" && options.targetFrame == 1u;
            const bool scenario7 = options.scenarioId == "orbit" && options.targetFrame == 61u;
            if (options.caseId != "webgpu_lights_dynamic"
                || (!scenario0 && !scenario1 && !scenario2 && !scenario3 && !scenario4 && !scenario5 && !scenario6 && !scenario7)
                || options.width != 800u || options.height != 500u
                || options.randomSeed != DefaultThreeRandomSeed)
            {
                throw std::invalid_argument("webgpu_lights_dynamic scenario does not match the locked r185 contract.");
            }
        }
    }

    void WebgpuLightsDynamicRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebgpuLightsDynamicOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(104u);
        buildWebgpuLightsDynamicFloor(entities[0]);
        for (uint32_t meshIndex = 0u; meshIndex < 100u; ++meshIndex)
        {
            auto &entity = entities[1u + meshIndex];
            const uint32_t geometryIndex = meshIndex % 5u;
            if (geometryIndex == 0u) buildWebgpuLightsDynamicSphere(entity, 10u, 16u, 0.8f);
            else if (geometryIndex == 1u) buildWebgpuLightsDynamicBox(entity);
            else if (geometryIndex == 2u) buildWebgpuLightsDynamicSphere(entity, 8u, 20u, 0.85f);
            else if (geometryIndex == 3u) buildWebgpuLightsDynamicSphere(entity, 8u, 16u, 0.7f);
            else buildWebgpuLightsDynamicCone(entity);
        }
        buildWebgpuLightsDynamicSphere(entities[101u], 16u, 24u, 2.0f);
        buildWebgpuLightsDynamicSphere(entities[102u], 8u, 12u, 0.22f);
        buildWebgpuLightsDynamicSphere(entities[103u], 8u, 12u, 0.22f);
        const glm::vec3 camera(0.0f, 15.0f, 30.0f);
        const glm::mat4 view = glm::lookAt(camera, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = webgpuLightsDynamicProjection(options.width, options.height);
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            auto &entity = entities[index];
            glm::vec3 position(0.0f);
            glm::mat4 rotation(1.0f);
            glm::vec4 color(0.25f);
            if (index == 0u) {
                position = glm::vec3(0.0f);
                color = glm::vec4(0.2666667f);
            } else if (index <= 100u) {
                const uint32_t meshIndex = index - 1u;
                const uint32_t ring = meshIndex / 10u;
                const uint32_t ringIndex = meshIndex % 10u;
                const float angle = float(ringIndex) / 10.0f * float(2.0 * Pi) + float(ring) * 0.3f;
                const float radius = 6.0f + float(ring) * 4.0f;
                const float jitter = 0.7f + 0.3f * float((meshIndex * 37u) % 101u) / 100.0f;
                position = glm::vec3(std::cos(angle) * radius, jitter, std::sin(angle) * radius);
                rotation = glm::rotate(rotation, 0.15f * float(meshIndex % 13u), glm::vec3(1.0f, 0.0f, 0.0f));
                const glm::vec3 rgb = webgpuLightsDynamicHsl(float(meshIndex) / 50.0f, 0.75f, 0.5f);
                color = glm::vec4(rgb, 1.0f);
            } else if (index == 101u) {
                position = glm::vec3(0.0f, 2.0f, 0.0f);
                color = glm::vec4(1.0f);
            } else {
                const uint32_t lightIndex = index - 102u;
                position = glm::vec3(lightIndex == 0u ? 8.0f : -8.0f, 8.0f, 4.0f);
                color = lightIndex == 0u ? glm::vec4(1.0f, 0.55f, 0.25f, 1.0f) : glm::vec4(0.25f, 0.55f, 1.0f, 1.0f);
            }
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), position) * rotation;
            entity.objectData.modelView = view * model;
            entity.objectData.modelViewProjection = projection * entity.objectData.modelView;
            entity.objectData.normalMatrix = glm::transpose(glm::inverse(entity.objectData.modelView));
            entity.objectData.baseColorAndFlags = glm::vec4(color.x, color.y, color.z, 0.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.baseColorAndFlags = color;
            const float lightPhase = float(index % 17u) / 16.0f;
            entity.materialData.lightColorAndRange = glm::vec4(
                0.65f + 0.35f * lightPhase,
                0.45f + 0.35f * (1.0f - lightPhase),
                0.75f,
                options.scenarioId == "lights-off" ? 0.0f : 0.65f);
            entity.lightData.colorAndRange = entity.materialData.lightColorAndRange;
            entity.renderFlags.values[0] = options.scenarioId == "lights-off" ? 0u : 1u;
            entity.renderFlags.values[1] = 0u;
            entity.renderFlags.values[2] = 0u;
            entity.renderFlags.values[3] = 0u;
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgpu_lights_dynamic could not create its RenderSet encoder.");
        for (uint32_t index = 0u; index < entities.size(); ++index)
        {
            const auto &entity = entities[index];
            const std::string suffix = "-" + std::to_string(index);
            const std::string vertexName = "WebgpuLightsDynamicVertices" + suffix;
            const std::string indexName = "WebgpuLightsDynamicIndices" + suffix;
            const std::string objectName = "WebgpuLightsDynamicObject" + suffix;
            const std::string instanceName = "WebgpuLightsDynamicInstance" + suffix;
            const std::string materialName = "WebgpuLightsDynamicMaterial" + suffix;
            const std::string lightName = "WebgpuLightsDynamicLightData" + suffix;
            const std::string flagsName = "WebgpuLightsDynamicRenderFlags" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWebgpuLightsDynamicBuffer(allocation, WebgpuLightsDynamicSceneRenderSetComponents::vertices, vertexName.c_str(), entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0]), 1u);
            appendWebgpuLightsDynamicBuffer(allocation, WebgpuLightsDynamicSceneRenderSetComponents::indices, indexName.c_str(), entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0]), 1u);
            appendWebgpuLightsDynamicBuffer(allocation, WebgpuLightsDynamicSceneRenderSetComponents::objects, objectName.c_str(), &entity.objectData, sizeof(entity.objectData), 1u);
            appendWebgpuLightsDynamicBuffer(allocation, WebgpuLightsDynamicSceneRenderSetComponents::instances, instanceName.c_str(), &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendWebgpuLightsDynamicBuffer(allocation, WebgpuLightsDynamicSceneRenderSetComponents::materials, materialName.c_str(), &entity.materialData, sizeof(entity.materialData), 1u);
            appendWebgpuLightsDynamicBuffer(allocation, WebgpuLightsDynamicSceneRenderSetComponents::lightData, lightName.c_str(), &entity.lightData, sizeof(entity.lightData), 1u);
            appendWebgpuLightsDynamicBuffer(allocation, WebgpuLightsDynamicSceneRenderSetComponents::renderFlags, flagsName.c_str(), &entity.renderFlags, sizeof(entity.renderFlags), 1u);
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuLightsDynamicRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuLightsDynamicRuntimeAdapter::afterFrame(
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
        prepareWebgpuLightsDynamicPath(options.captureRgbaPath);
        if (!options.captureRgbaPath.empty())
        {
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        }
        prepareWebgpuLightsDynamicPath(options.captureMetadataPath);
        if (!options.captureMetadataPath.empty())
        {
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgpu_lights_dynamic\",\n"
                   << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                   << "  \"sceneRenderSetCount\": 1,\n  \"renderSetType\": \"WebgpuLightsDynamicSceneRenderSet\",\n"
                   << "  \"entityCount\": " << entities.size() << ",\n  \"instanceCounts\": [";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u) output << ',';
                output << 1u;
            }
            output << "],\n"
                   << "  \"scenePassCount\": 2,\n  \"screenPassCount\": 1,\n"
                   << "  \"drawCommandCount\": 2,\n"
                   << "  \"directDrawFallback\": false,\n  \"sampleCount\": 1,\n"
                   << "  \"msaaEnabled\": false\n}\n";
        }
        captureWritten = true;
    }

    void WebgpuLightsDynamicRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
}
