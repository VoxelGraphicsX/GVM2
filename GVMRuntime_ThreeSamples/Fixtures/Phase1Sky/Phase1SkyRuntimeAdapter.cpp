#include "Phase1SkyRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

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
        constexpr GVM::Core::RenderSetHandle SkySceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(Phase1SkyHostVertex) == 16u);
        static_assert(sizeof(Phase1SkyHostObjectData) == 16u);
        static_assert(sizeof(Phase1SkyHostInstanceData) == 16u);
        static_assert(sizeof(Phase1SkyHostMaterialData) == 16u);

        /** Returns the generated vertex component handle for the selected sky shard. */
        GVM::Core::RenderComponentHandle skyVertexComponent()
        {
#if defined(GVM_WEBGL_SKY_ADAPTER)
            return WebglShadersSkySceneRenderSetComponents::vertices;
#else
            return WebgpuSkySceneRenderSetComponents::vertices;
#endif
        }

        /** Returns the generated index component handle for the selected sky shard. */
        GVM::Core::RenderComponentHandle skyIndexComponent()
        {
#if defined(GVM_WEBGL_SKY_ADAPTER)
            return WebglShadersSkySceneRenderSetComponents::indices;
#else
            return WebgpuSkySceneRenderSetComponents::indices;
#endif
        }

        /** Returns the generated object component handle for the selected sky shard. */
        GVM::Core::RenderComponentHandle skyObjectComponent()
        {
#if defined(GVM_WEBGL_SKY_ADAPTER)
            return WebglShadersSkySceneRenderSetComponents::objects;
#else
            return WebgpuSkySceneRenderSetComponents::objects;
#endif
        }

        /** Returns the generated instance component handle for the selected sky shard. */
        GVM::Core::RenderComponentHandle skyInstanceComponent()
        {
#if defined(GVM_WEBGL_SKY_ADAPTER)
            return WebglShadersSkySceneRenderSetComponents::instances;
#else
            return WebgpuSkySceneRenderSetComponents::instances;
#endif
        }

        /** Returns the generated material component handle for the selected sky shard. */
        GVM::Core::RenderComponentHandle skyMaterialComponent()
        {
#if defined(GVM_WEBGL_SKY_ADAPTER)
            return WebglShadersSkySceneRenderSetComponents::materials;
#else
            return WebgpuSkySceneRenderSetComponents::materials;
#endif
        }

        /** Creates parent directories for one explicitly requested sky artifact. */
        void prepareSkyOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Appends one typed host payload to a RenderSet allocation descriptor. */
        void appendSkyBufferPayload(
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

        /** Builds one fullscreen triangle owned by the selected Scene RenderSet entity. */
        Phase1SkyHostEntity makeSkyEntity(
            const eastl::string &logicalId,
            float materialPhase)
        {
            Phase1SkyHostEntity entity;
            entity.logicalId = logicalId;
            entity.vertices = {
                {{-1.0f, -1.0f, 0.0f, 1.0f}},
                {{3.0f, -1.0f, 0.0f, 1.0f}},
                {{-1.0f, 3.0f, 0.0f, 1.0f}},
            };
            entity.indices = {0u, 1u, 2u};
            entity.objectData.phase =
                glm::vec4(materialPhase, 0.0f, 0.0f, 0.0f);
            return entity;
        }

        /** Clips one parametric line inequality and updates its visible interval. */
        bool clipSkyGridLineAxis(
            float numerator,
            float denominator,
            float &minimumT,
            float &maximumT)
        {
            if (std::abs(denominator) < 1.0e-8f)
            {
                return numerator >= 0.0f;
            }
            const float candidate = numerator / denominator;
            if (denominator > 0.0f)
            {
                maximumT = std::min(maximumT, candidate);
            }
            else
            {
                minimumT = std::max(minimumT, candidate);
            }
            return minimumT <= maximumT;
        }

        /** Clips one projected line segment to the canonical normalized viewport. */
        bool clipSkyGridLineToViewport(
            glm::vec2 &start,
            glm::vec2 &end)
        {
            const glm::vec2 delta = end - start;
            float minimumT = 0.0f;
            float maximumT = 1.0f;
            if (!clipSkyGridLineAxis(
                    1.0f + start.x,
                    -delta.x,
                    minimumT,
                    maximumT) ||
                !clipSkyGridLineAxis(
                    1.0f - start.x,
                    delta.x,
                    minimumT,
                    maximumT) ||
                !clipSkyGridLineAxis(
                    1.0f + start.y,
                    -delta.y,
                    minimumT,
                    maximumT) ||
                !clipSkyGridLineAxis(
                    1.0f - start.y,
                    delta.y,
                    minimumT,
                    maximumT))
            {
                return false;
            }
            const glm::vec2 originalStart = start;
            start = originalStart + delta * minimumT;
            end = originalStart + delta * maximumT;
            return true;
        }

        /** Clips one world line against the r185 perspective near plane. */
        bool clipSkyGridLineToNearPlane(
            glm::vec3 &start,
            glm::vec3 &end,
            const glm::vec3 &cameraPosition,
            const glm::vec3 &cameraForward)
        {
            constexpr float NearDistance = 100.0f;
            float startDepth =
                glm::dot(start - cameraPosition, cameraForward);
            float endDepth =
                glm::dot(end - cameraPosition, cameraForward);
            if (startDepth < NearDistance &&
                endDepth < NearDistance)
            {
                return false;
            }
            if (startDepth < NearDistance)
            {
                const float interpolation =
                    (NearDistance - startDepth) /
                    (endDepth - startDepth);
                start += (end - start) * interpolation;
                startDepth = NearDistance;
            }
            else if (endDepth < NearDistance)
            {
                const float interpolation =
                    (NearDistance - endDepth) /
                    (startDepth - endDepth);
                end += (start - end) * interpolation;
                endDepth = NearDistance;
            }
            return startDepth >= NearDistance &&
                   endDepth >= NearDistance;
        }

        /** Projects one world position through the canonical r185 perspective camera. */
        glm::vec2 projectSkyGridPoint(
            const glm::vec3 &position,
            const glm::vec3 &cameraPosition,
            const glm::vec3 &cameraRight,
            const glm::vec3 &cameraUp,
            const glm::vec3 &cameraForward)
        {
            constexpr float TangentHalfFov =
                0.5773502691896258f;
            constexpr float Aspect = 1.6f;
            const glm::vec3 relative =
                position - cameraPosition;
            const float depth =
                glm::dot(relative, cameraForward);
            return glm::vec2(
                glm::dot(relative, cameraRight) /
                    (depth * TangentHalfFov * Aspect),
                glm::dot(relative, cameraUp) /
                    (depth * TangentHalfFov));
        }

        /** Appends one one-pixel triangle-expanded GridHelper line segment. */
        void appendSkyGridLine(
            Phase1SkyHostEntity &entity,
            glm::vec3 worldStart,
            glm::vec3 worldEnd,
            const Phase1SkyHostFrame &frame,
            float verticalPixelBias)
        {
            constexpr float Radius = 2002.4984394500786f;
            const glm::vec3 cameraRight(frame.cameraRight);
            const glm::vec3 cameraUp(frame.cameraUp);
            const glm::vec3 cameraForward(frame.cameraForward);
            const glm::vec3 cameraPosition =
                -cameraForward * Radius;
            if (!clipSkyGridLineToNearPlane(
                    worldStart,
                    worldEnd,
                    cameraPosition,
                    cameraForward))
            {
                return;
            }
            glm::vec2 start = projectSkyGridPoint(
                worldStart,
                cameraPosition,
                cameraRight,
                cameraUp,
                cameraForward);
            glm::vec2 end = projectSkyGridPoint(
                worldEnd,
                cameraPosition,
                cameraRight,
                cameraUp,
                cameraForward);
            if (!clipSkyGridLineToViewport(start, end))
            {
                return;
            }
            glm::vec2 startPixel(
                (start.x * 0.5f + 0.5f) * 800.0f,
                (1.0f - (start.y * 0.5f + 0.5f)) *
                    500.0f);
            glm::vec2 endPixel(
                (end.x * 0.5f + 0.5f) * 800.0f,
                (1.0f - (end.y * 0.5f + 0.5f)) *
                    500.0f);
            startPixel.y += verticalPixelBias;
            endPixel.y += verticalPixelBias;
            const glm::vec2 direction =
                glm::normalize(endPixel - startPixel);
            const glm::vec2 perpendicular(
                -direction.y * 0.5f,
                direction.x * 0.5f);
            const glm::vec2 pixelPoints[4u] = {
                startPixel - perpendicular,
                startPixel + perpendicular,
                endPixel - perpendicular,
                endPixel + perpendicular,
            };
            const uint32_t base =
                static_cast<uint32_t>(entity.vertices.size());
            for (const glm::vec2 &pixel : pixelPoints)
            {
                entity.vertices.push_back({glm::vec4(
                    pixel.x / 800.0f * 2.0f - 1.0f,
                    pixel.y / 500.0f * 2.0f - 1.0f,
                    0.0f,
                    1.0f)});
            }
            const uint32_t lineIndices[6u] = {
                base,
                base + 1u,
                base + 2u,
                base + 2u,
                base + 1u,
                base + 3u,
            };
            entity.indices.insert(
                entity.indices.end(),
                lineIndices,
                lineIndices + 6u);
        }

        /** Builds the six triangle-expanded lines of the r185 GridHelper. */
        Phase1SkyHostEntity makeSkyGridEntity(
            const Phase1SkyHostFrame &frame,
            bool useControlReplay)
        {
            Phase1SkyHostEntity entity;
            entity.logicalId = "expanded-grid";
            entity.objectData.phase =
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            for (uint32_t line = 0u; line <= 2u; ++line)
            {
                const float coordinate =
                    -5000.0f + float(line) * 5000.0f;
                appendSkyGridLine(
                    entity,
                    glm::vec3(-5000.0f, 0.0f, coordinate),
                    glm::vec3(5000.0f, 0.0f, coordinate),
                    frame,
                    !useControlReplay && line == 1u
                        ? 1.0f
                        : 0.0f);
                appendSkyGridLine(
                    entity,
                    glm::vec3(coordinate, 0.0f, -5000.0f),
                    glm::vec3(coordinate, 0.0f, 5000.0f),
                    frame,
                    0.0f);
            }
            return entity;
        }

        /** Converts one canonical orbit camera into an orthonormal ray basis. */
        void buildSkyCameraBasis(
            bool useControlReplay,
            glm::vec4 &right,
            glm::vec4 &up,
            glm::vec4 &forward)
        {
            constexpr float Pi = 3.14159265358979323846f;
            const float radius =
                std::sqrt(2000.0f * 2000.0f + 100.0f * 100.0f);
            float theta = 0.0f;
            float phi = std::acos(100.0f / radius);
            if (useControlReplay)
            {
                theta -= 2.0f * Pi * 70.0f / 500.0f;
                phi -= 2.0f * Pi * -35.0f / 500.0f;
            }
            const glm::vec3 cameraPosition(
                radius * std::sin(phi) * std::sin(theta),
                radius * std::cos(phi),
                radius * std::sin(phi) * std::cos(theta));
            const glm::vec3 forwardValue =
                glm::normalize(-cameraPosition);
            const glm::vec3 rightValue =
                glm::normalize(
                    glm::cross(
                        forwardValue,
                        glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 upValue =
                glm::normalize(
                    glm::cross(rightValue, forwardValue));
            right = glm::vec4(rightValue, 0.0f);
            up = glm::vec4(upValue, 0.0f);
            forward = glm::vec4(forwardValue, 0.0f);
        }

        /** Builds the canonical atmospheric parameters for one frozen Manifest scenario. */
        Phase1SkyHostFrame makeSkyFrame(
            const ThreeSampleHostOptions &options)
        {
            constexpr float Pi = 3.14159265358979323846f;
            const bool controls = !options.inputReplayPath.empty();
            const float turbidity = controls ? 6.0f : 10.0f;
            const float rayleigh = controls ? 2.2f : 3.0f;
            const float mieCoefficient = controls ? 0.018f : 0.005f;
            const float mieDirectionalG = controls ? 0.82f : 0.7f;
            const float elevation = controls ? 25.0f : 2.0f;
            const float azimuth = controls ? 135.0f : 180.0f;
            const float phi = (90.0f - elevation) * Pi / 180.0f;
            const float theta = azimuth * Pi / 180.0f;
            const glm::vec3 sun(
                std::sin(phi) * std::sin(theta),
                std::cos(phi),
                std::sin(phi) * std::cos(theta));
            Phase1SkyHostFrame value;
            value.atmosphere = glm::vec4(
                turbidity,
                rayleigh,
                mieCoefficient,
                mieDirectionalG);
            value.clouds = glm::vec4(
                controls ? 0.68f : 0.4f,
                controls ? 0.72f : 0.4f,
                controls ? 0.3f : 0.5f,
                controls ? 0.7f : 0.5f);
            value.sunAndDisc =
                glm::vec4(sun, controls ? 0.0f : 1.0f);
            buildSkyCameraBasis(
                controls,
                value.cameraRight,
                value.cameraUp,
                value.cameraForward);
            value.viewportAndTime =
                glm::vec4(
                    800.0f,
                    500.0f,
                    static_cast<float>(options.targetFrame) / 60.0f,
#if defined(GVM_WEBGPU_SKY_ADAPTER)
                    1.0f
#else
                    0.0f
#endif
                );
            return value;
        }

        /** Writes one tightly packed RGBA8 sky capture file. */
        void writeSkyRgba(
            const eastl::string &pathValue,
            const eastl::vector<uint8_t> &rgba)
        {
            if (pathValue.empty())
            {
                return;
            }
            const std::filesystem::path path(pathValue.c_str());
            prepareSkyOutputPath(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write the Phase 1 sky RGBA capture.");
            }
        }
    } // namespace

    void Phase1SkyRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        device = inDevice;
        caseId = options.caseId;
#if defined(GVM_WEBGL_SKY_ADAPTER)
        if (caseId != "webgl_shaders_sky")
        {
            throw std::invalid_argument(
                "The WebGL sky shard received an unexpected case.");
        }
#else
        if (caseId != "webgpu_sky")
        {
            throw std::invalid_argument(
                "The WebGPU sky shard received an unexpected case.");
        }
#endif
        frame = makeSkyFrame(options);
        entities.push_back(makeSkyEntity("sky", 0.0f));
#if defined(GVM_WEBGL_SKY_ADAPTER)
        entities.push_back(
            makeSkyGridEntity(
                frame,
                !options.inputReplayPath.empty()));
#endif
        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SkySceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create the dedicated sky RenderSet encoder.");
        }
        for (Phase1SkyHostEntity &entity : entities)
        {
            entity.entityIndex = allocateEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(
            SkySceneRenderSetHandle,
            encoder);
    }

    GVM::Core::RenderEntityIndex
    Phase1SkyRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const Phase1SkyHostEntity &entity) const
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        const eastl::string prefix =
            "Phase1Sky" + entity.logicalId;
        appendSkyBufferPayload(
            allocation,
            skyVertexComponent(),
            prefix + "Vertices",
            entity.vertices.data(),
            entity.vertices.size() *
                sizeof(Phase1SkyHostVertex),
            1u);
        appendSkyBufferPayload(
            allocation,
            skyIndexComponent(),
            prefix + "Indices",
            entity.indices.data(),
            entity.indices.size() * sizeof(uint32_t),
            1u);
        appendSkyBufferPayload(
            allocation,
            skyObjectComponent(),
            prefix + "Object",
            &entity.objectData,
            sizeof(entity.objectData),
            1u);
        appendSkyBufferPayload(
            allocation,
            skyInstanceComponent(),
            prefix + "Instance",
            &entity.instanceData,
            sizeof(entity.instanceData),
            1u);
        appendSkyBufferPayload(
            allocation,
            skyMaterialComponent(),
            prefix + "Material",
            &entity.materialData,
            sizeof(entity.materialData),
            1u);
        return encoder.allocEntity(allocation);
    }

    void Phase1SkyRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void Phase1SkyRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "The sky capture is too large.");
        }
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        writeArtifacts(
            options,
            frameIndex,
            width,
            height,
            rgba);
        captureWritten = true;
    }

    void Phase1SkyRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        writeSkyRgba(options.captureRgbaPath, rgba);
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path path(
                options.captureMetadataPath.c_str());
            prepareSkyOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"" << caseId.c_str() << "\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\""
                << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\""
                << threeSampleBackendName(options.backend)
                << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed
                << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u
                << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\"";
            if (!options.inputReplayPath.empty())
            {
#if defined(GVM_WEBGPU_SKY_ADAPTER)
                constexpr const char *ReplaySha256 =
                    "6824992d53547c9564497dc04a13e262a47977a8010c714268c484297c844809";
                constexpr const char *ReplayTarget =
                    "canvas:not([class])";
#else
                constexpr const char *ReplaySha256 =
                    "f7d849074b204c66f1e8775947e516f9733ff9765ded31c1df82ff26e754ff6a";
                constexpr const char *ReplayTarget = "canvas";
#endif
                output
                    << ",\n  \"inputReplay\":{\"schemaVersion\":1,"
                    << "\"caseId\":\"" << caseId.c_str()
                    << "\",\"scenarioId\":\""
                    << options.scenarioId.c_str()
                    << "\",\"captureFrame\":" << frameIndex
                    << ",\"sha256\":\"" << ReplaySha256
                    << "\",\"target\":\"" << ReplayTarget
                    << "\",\"eventCount\":3}\n";
            }
            else
            {
                output << '\n';
            }
            output << "}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
#if defined(GVM_WEBGPU_SKY_ADAPTER)
            constexpr const char *RenderSetTypeName =
                "WebgpuSkySceneRenderSet";
            constexpr const char *ScenePassName = "main-sky";
            constexpr const char *RenderClassName =
                "WebgpuSkyScenePass";
#else
            constexpr const char *RenderSetTypeName =
                "WebglShadersSkySceneRenderSet";
            constexpr const char *ScenePassName =
                "main-sky-and-expanded-grid";
            constexpr const char *RenderClassName =
                "WebglShadersSkyScenePass";
#endif
            const std::filesystem::path path(
                options.sceneSnapshotPath.c_str());
            prepareSkyOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output
                << "{\n  \"caseId\":\"" << caseId.c_str()
                << "\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"assetBacked\":false,\n"
                << "  \"assetHashes\":[],\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":"
                << entities.size() << ",\n"
                << "  \"entityCount\":" << entities.size() << ",\n"
                << "  \"instanceCounts\":[";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u)
                {
                    output << ',';
                }
                output << 1u;
            }
            output
                << "],\n"
                << "  \"componentSchema\":[\"vertices\",\"indices\","
                   "\"objects\",\"instances\",\"materials\"],\n"
                << "  \"scenePassCount\":1,\n"
#if defined(GVM_WEBGPU_SKY_ADAPTER)
                << "  \"screenPassCount\":1,\n"
                << "  \"scenePasses\":[\"WebgpuSkyScenePass\"],\n"
                << "  \"screenPasses\":[\"WebgpuSkyInspectorPass\"],\n"
#else
                << "  \"screenPassCount\":0,\n"
                << "  \"scenePasses\":[\"WebglShadersSkyScenePass\"],\n"
                << "  \"screenPasses\":[],\n"
#endif
                << "  \"attachmentFormats\":[\"rgba8unorm\"],\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"renderSetIndexedIndirect\":true,\n"
                << "  \"directDrawFallback\":false,\n"
                << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
                << "\"scenePass\":\"" << ScenePassName
                << "\",\"entityOrdinal\":0}],\n"
                << "  \"sceneRoots\":[{\n"
                << "    \"id\":\"scene\",\n"
                << "    \"renderSetCount\":1,\n"
                << "    \"renderSetId\":\"scene\",\n"
                << "    \"renderSetType\":\"" << RenderSetTypeName
                << "\",\n"
                << "    \"renderableObjectCount\":"
                << entities.size() << ",\n"
                << "    \"entityCount\":" << entities.size() << ",\n"
                << "    \"drawCommandCount\":1,\n"
                << "    \"directDrawFallback\":false,\n"
                << "    \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\","
                   "\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\","
                   "\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\","
                   "\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\","
                   "\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\","
                   "\"role\":\"material\"}],\n"
                << "    \"scenePasses\":[{\"name\":\""
                << ScenePassName << "\",\"renderClass\":\""
                << RenderClassName
                << "\",\"renderSetId\":\"scene\","
                   "\"renderSetBindingCount\":1,"
                   "\"drawMode\":\"render-set-indexed-indirect\","
                   "\"invocationCount\":1,\"drawCommandCount\":1,"
                   "\"usesStandaloneGeometry\":false,"
                   "\"usesExplicitDrawCount\":false}],\n"
                << "    \"entities\":[";
            for (size_t index = 0u; index < entities.size(); ++index)
            {
                if (index != 0u)
                {
                    output << ',';
                }
                output
                    << "{\"entityId\":"
                    << entities[index].entityIndex
                    << ",\"logicalRenderableId\":\""
                    << entities[index].logicalId.c_str()
                    << "\",\"instanceCount\":1}";
            }
            output << "]\n  }]\n}\n";
        }
    }

    void Phase1SkyRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
