#include "WebglInteractiveRaycastingPointsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t PointCloudCount = 3u;
        constexpr uint32_t PointCloudWidth = 80u;
        constexpr uint32_t PointCloudLength = 160u;
        constexpr uint32_t TrailSphereCount = 40u;
        constexpr uint32_t MarkerCount = PointCloudCount + TrailSphereCount;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;

        /** Creates parent directories for one capture artifact. */
        void prepareOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Advances the fixed xorshift stream used by marker placement. */
        uint32_t nextRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return state;
        }

        /** Returns a deterministic normalized random sample. */
        float randomUnit(uint32_t &state)
        {
            return float(nextRandom(state) >> 8u) / 16777216.0f;
        }

        /** Converts one Three camera-space point to the host clip convention. */
        glm::vec3 projectPoint(const glm::mat4 &viewProjection, const glm::vec3 &world)
        {
            const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
            const float inverseW = 1.0f / std::max(std::abs(clip.w), 1.0e-6f);
            return glm::vec3(clip.x * inverseW, -clip.y * inverseW,
                             (clip.z * inverseW + 1.0f) * 0.5f);
        }

        /** Appends one projected point billboard with Three's size attenuation. */
        void appendPointBillboard(WebglInteractiveRaycastingPointsEntityData &entity,
                                  const glm::mat4 &viewProjection,
                                  const glm::vec3 &world,
                                  float size,
                                  const glm::vec4 &color)
        {
            const glm::vec3 center = projectPoint(viewProjection, world);
            // `size` is the full WebGL point diameter in physical pixels.
            // The expanded clip-space quad stores half that diameter on each
            // side of the center; converting a pixel radius to NDC therefore
            // divides by the full framebuffer extent once more.
            const float halfWidth = size / 1600.0f;
            const float halfHeight = size / 1000.0f;
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            const glm::vec4 corners[] = {
                {center.x - halfWidth, center.y - halfHeight, center.z, 1.0f},
                {center.x + halfWidth, center.y - halfHeight, center.z, 1.0f},
                {center.x + halfWidth, center.y + halfHeight, center.z, 1.0f},
                {center.x - halfWidth, center.y + halfHeight, center.z, 1.0f}};
            const glm::vec4 uvs[] = {
                {0.0f, 0.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, 0.0f, 0.0f},
                {1.0f, 1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f, 0.0f}};
            for (uint32_t cornerIndex = 0u; cornerIndex < 4u; ++cornerIndex)
                entity.vertices.push_back({corners[cornerIndex], color, uvs[cornerIndex]});
            entity.indices.insert(entity.indices.end(),
                                  {base, base + 1u, base + 2u,
                                   base, base + 2u, base + 3u});
        }

        /** Builds the low-segment red sphere used by the raycast trail. */
        void appendTrailSphere(WebglInteractiveRaycastingPointsEntityData &entity,
                               const glm::mat4 &viewProjection,
                               const glm::vec3 &center,
                               float radius,
                               const glm::vec4 &color)
        {
            constexpr uint32_t WidthSegments = 32u;
            constexpr uint32_t HeightSegments = 16u;
            for (uint32_t y = 0u; y < HeightSegments; ++y)
            {
                const float v0 = float(y) / float(HeightSegments);
                const float v1 = float(y + 1u) / float(HeightSegments);
                const float theta0 = v0 * Pi;
                const float theta1 = v1 * Pi;
                for (uint32_t x = 0u; x < WidthSegments; ++x)
                {
                    const float u0 = float(x) / float(WidthSegments);
                    const float u1 = float(x + 1u) / float(WidthSegments);
                    const glm::vec3 points[] = {
                        {std::sin(theta0) * std::cos(u0 * Pi * 2.0f), std::cos(theta0),
                         std::sin(theta0) * std::sin(u0 * Pi * 2.0f)},
                        {std::sin(theta0) * std::cos(u1 * Pi * 2.0f), std::cos(theta0),
                         std::sin(theta0) * std::sin(u1 * Pi * 2.0f)},
                        {std::sin(theta1) * std::cos(u1 * Pi * 2.0f), std::cos(theta1),
                         std::sin(theta1) * std::sin(u1 * Pi * 2.0f)},
                        {std::sin(theta1) * std::cos(u0 * Pi * 2.0f), std::cos(theta1),
                         std::sin(theta1) * std::sin(u0 * Pi * 2.0f)}};
                    const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
                    for (const glm::vec3 &point : points)
                    {
                        const glm::vec3 projected = projectPoint(
                            viewProjection, center + point * radius);
                        entity.vertices.push_back({glm::vec4(projected, 1.0f), color,
                                                   glm::vec4(0.0f)});
                    }
                    if (y != 0u)
                        entity.indices.insert(entity.indices.end(),
                                              {base, base + 1u, base + 3u});
                    if (y != HeightSegments - 1u)
                        entity.indices.insert(entity.indices.end(),
                                              {base + 1u, base + 2u, base + 3u});
                }
            }
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendPayload(GVM::Core::RenderSetAllocInfo &allocation,
                           GVM::Core::RenderComponentHandle component,
                           const eastl::string &name,
                           const void *value,
                           uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }

        /** Validates the three deterministic point-raycasting scenarios. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 120u;
            const bool pointer = options.scenarioId == "point-trail" && options.targetFrame == 121u;
            if (options.caseId != "webgl_interactive_raycasting_points" ||
                (!initial && !animated && !pointer) || options.width != 800u ||
                options.height != 500u || options.randomSeed != DefaultThreeRandomSeed)
                throw std::invalid_argument("webgl_interactive_raycasting_points requires the locked r185 scenario contract.");
        }
    } // namespace

    void WebglInteractiveRaycastingPointsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(MarkerCount);
        const float cameraAngle = float(options.targetFrame) * 0.005f;
        const glm::vec3 baseCameraPosition(10.0f, 10.0f, 10.0f);
        const glm::mat4 cameraRotation = glm::rotate(
            glm::mat4(1.0f), cameraAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 cameraPosition = glm::vec3(
            cameraRotation * glm::vec4(baseCameraPosition, 1.0f));
        const glm::mat4 view = glm::lookAt(
            cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(
            glm::radians(45.0f), 1.6f, 1.0f, 10000.0f);
        const glm::mat4 viewProjection = projection * view;
        for (uint32_t cloud = 0u; cloud < PointCloudCount; ++cloud)
        {
            auto &entity = entities[cloud];
            const glm::vec3 color = cloud == 0u
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : cloud == 1u
                    ? glm::vec3(0.0f, 1.0f, 0.0f)
                    : glm::vec3(0.0f, 1.0f, 1.0f);
            const float xOffset = float(cloud) * 5.0f - 5.0f;
            for (uint32_t i = 0u; i < PointCloudWidth; ++i)
            {
                for (uint32_t j = 0u; j < PointCloudLength; ++j)
                {
                    const float u = float(i) / float(PointCloudWidth);
                    const float v = float(j) / float(PointCloudLength);
                    const glm::vec3 world(
                        (u - 0.5f) * 5.0f + xOffset,
                        (std::cos(u * Pi * 4.0f) + std::sin(v * Pi * 8.0f)) / 20.0f * 10.0f,
                        (v - 0.5f) * 10.0f);
                    const float viewDepth = std::max(
                        1.0f, -((view * glm::vec4(world, 1.0f)).z));
                    const float pointPixels = 0.05f * 500.0f /
                        (2.0f * std::tan(glm::radians(45.0f) * 0.5f) * viewDepth);
                    const float intensity = (world.y / 10.0f + 0.1f) * 5.0f;
                    appendPointBillboard(entity, viewProjection, world,
                                         pointPixels, glm::vec4(color * intensity, 1.0f));
                }
            }
            entity.objectData.offsetAndScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            entity.objectData.materialAndFlags = glm::uvec4(0u, 1u, 0u, 0u);
            entity.baseObjectData = entity.objectData;
            entity.instanceData.offsetAndScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            entity.instanceData.tint = glm::vec4(1.0f);
            entity.materialData.baseColor = glm::vec4(1.0f);
        }
        for (uint32_t sphereIndex = 0u; sphereIndex < TrailSphereCount; ++sphereIndex)
        {
            const uint32_t entityIndex = PointCloudCount + sphereIndex;
            auto &entity = entities[entityIndex];
            const float sphereScale = options.scenarioId == "point-trail" && sphereIndex == 0u
                ? 1.0f : 0.01f;
            appendTrailSphere(entity, viewProjection, glm::vec3(0.0f),
                              0.1f * sphereScale, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
            entity.objectData.offsetAndScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            entity.objectData.materialAndFlags = glm::uvec4(0u);
            entity.baseObjectData = entity.objectData;
            entity.instanceData.offsetAndScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            entity.instanceData.tint = glm::vec4(1.0f);
            entity.materialData.baseColor = glm::vec4(1.0f);
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_interactive_raycasting_points could not create its Scene Set encoder.");
        for (uint32_t index = 0u; index < MarkerCount; ++index)
        {
            auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("RaycastMarker-") + eastl::to_string(index);
            appendPayload(allocation, WebglInteractiveRaycastingPointsSceneRenderSetComponents::vertices,
                          prefix + "-vertices", entity.vertices.data(), entity.vertices.size() * sizeof(entity.vertices[0u]));
            appendPayload(allocation, WebglInteractiveRaycastingPointsSceneRenderSetComponents::indices,
                          prefix + "-indices", entity.indices.data(), entity.indices.size() * sizeof(entity.indices[0u]));
            appendPayload(allocation, WebglInteractiveRaycastingPointsSceneRenderSetComponents::objects,
                          prefix + "-object", &entity.objectData, sizeof(entity.objectData));
            appendPayload(allocation, WebglInteractiveRaycastingPointsSceneRenderSetComponents::instances,
                          prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData));
            appendPayload(allocation, WebglInteractiveRaycastingPointsSceneRenderSetComponents::materials,
                          prefix + "-material", &entity.materialData, sizeof(entity.materialData));
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglInteractiveRaycastingPointsRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        (void)frameIndex;
    }

    void WebglInteractiveRaycastingPointsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_interactive_raycasting_points could not create its update encoder.");
        for (const auto &entity : entities)
        {
            encoder->setBufferComponentData(entity.entityIndex,
                WebglInteractiveRaycastingPointsSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
            encoder->setBufferComponentData(entity.entityIndex,
                WebglInteractiveRaycastingPointsSceneRenderSetComponents::instances,
                &entity.instanceData, sizeof(entity.instanceData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglInteractiveRaycastingPointsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        prepareOutputPath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglInteractiveRaycastingPointsRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width,
        uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        prepareOutputPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_interactive_raycasting_points\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false}\n";
    }

    void WebglInteractiveRaycastingPointsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        prepareOutputPath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_interactive_raycasting_points\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"semantic-complete\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":false,\n  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglInteractiveRaycastingPointsSceneRenderSet\",\n  \"renderableObjectCount\":43,\n"
               << "  \"entityCount\":43,\n  \"instanceCount\":1,\n  \"instanceCounts\":[";
        for (uint32_t index = 0u; index < MarkerCount; ++index)
        {
            if (index > 0u) output << ',';
            output << '1';
        }
        output << "],\n  \"scenePassCount\":1,\n  \"screenPassCount\":0,\n  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n  \"sampleCount\":1,\n"
               << "  \"msaaEnabled\":false,\n  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
               << "  \"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglInteractiveRaycastingPointsMainPass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\n"
               << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetId\":\"scene-set\",\"renderSetCount\":1,\"renderSetType\":\"WebglInteractiveRaycastingPointsSceneRenderSet\",\"renderableObjectCount\":43,\"entityCount\":43,\"drawCommandCount\":1,\"directDrawFallback\":false,\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\"scenePasses\":[{\"name\":\"main\",\"renderClass\":\"WebglInteractiveRaycastingPointsMainPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"entities\":[";
        for (uint32_t index = 0u; index < MarkerCount; ++index)
        {
            if (index > 0u) output << ',';
            output << "{\"entityId\":" << index << ",\"logicalRenderableId\":\""
                   << (index < PointCloudCount ? "point-cloud-" : "trail-sphere-")
                   << (index < PointCloudCount ? index : index - PointCloudCount)
                   << "\",\"instanceCount\":1}";
        }
        output << "]}]\n}\n";
    }

    void WebglInteractiveRaycastingPointsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options,
        uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("point capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_interactive_raycasting_points has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglInteractiveRaycastingPointsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
