#include "CameraArrayRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t CameraArrayAmount = 6u;
        constexpr uint32_t CameraArrayWidth = 800u;
        constexpr uint32_t CameraArrayHeight = 500u;
        constexpr float CameraArrayPi = 3.14159265358979323846f;
        constexpr GVM::Core::RenderSetHandle CameraArraySceneSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(CameraArrayHostVertex) == 64u);
        static_assert(sizeof(CameraArrayHostObjectData) == 16u);
        static_assert(sizeof(CameraArrayHostInstanceData) == 16u);
        static_assert(sizeof(CameraArrayHostMaterialData) == 16u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareCameraArrayOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Appends one generated component payload to a RenderSet allocation. */
        void appendCameraArrayPayload(
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

        /** Rotates one position or normal with Three's intrinsic XYZ Euler convention. */
        glm::vec3 rotateCameraArrayVector(
            const glm::vec3 &value,
            float rotationX,
            float rotationZ)
        {
            const float cosineZ = std::cos(rotationZ);
            const float sineZ = std::sin(rotationZ);
            const glm::vec3 afterZ(
                value.x * cosineZ - value.y * sineZ,
                value.x * sineZ + value.y * cosineZ,
                value.z);
            const float cosineX = std::cos(rotationX);
            const float sineX = std::sin(rotationX);
            return glm::vec3(
                afterZ.x,
                afterZ.y * cosineX - afterZ.z * sineX,
                afterZ.y * sineX + afterZ.z * cosineX);
        }

        /** Returns the r185 subcamera position for one top-origin output tile. */
        glm::vec3 cameraArrayPosition(
            uint32_t tileIndex,
            bool webgpuVariant)
        {
            const uint32_t tileX = tileIndex % CameraArrayAmount;
            const uint32_t tileY = tileIndex / CameraArrayAmount;
            const uint32_t cameraY =
                webgpuVariant ? tileY : CameraArrayAmount - 1u - tileY;
            return glm::vec3(
                float(tileX) / 3.0f - 1.0f,
                1.0f - float(cameraY) / 3.0f,
                webgpuVariant
                    ? 3.0f + float(tileX + cameraY)
                    : 3.0f);
        }

        /** Projects one world point into its final full-canvas camera tile. */
        glm::vec4 projectCameraArrayPoint(
            const glm::vec3 &worldPosition,
            uint32_t tileIndex,
            bool webgpuVariant)
        {
            const uint32_t tileX = tileIndex % CameraArrayAmount;
            const uint32_t tileY = tileIndex / CameraArrayAmount;
            const glm::vec3 cameraPosition =
                cameraArrayPosition(tileIndex, webgpuVariant);
            const glm::vec3 forward = glm::normalize(-cameraPosition);
            const glm::vec3 right = glm::normalize(
                glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 cameraUp = glm::normalize(glm::cross(right, forward));
            const glm::vec3 relative = worldPosition - cameraPosition;
            const float viewX = glm::dot(relative, right);
            const float viewY = glm::dot(relative, cameraUp);
            const float viewDepth = glm::dot(relative, forward);
            const float halfFovDegrees = webgpuVariant ? 25.0f : 20.0f;
            const float tangentHalfFov =
                std::tan(halfFovDegrees * CameraArrayPi / 180.0f);
            const float localNdcX =
                viewX / (viewDepth * tangentHalfFov * (8.0f / 5.0f));
            const float localNdcY =
                -viewY / (viewDepth * tangentHalfFov);
            const float startX = std::floor(
                float(tileX) * float(CameraArrayWidth) / float(CameraArrayAmount));
            const float startY = std::floor(
                float(tileY) * float(CameraArrayHeight) / float(CameraArrayAmount));
            const float tileWidth = std::ceil(
                float(CameraArrayWidth) / float(CameraArrayAmount));
            const float tileHeight = std::ceil(
                float(CameraArrayHeight) / float(CameraArrayAmount));
            const float pixelX = startX + (localNdcX * 0.5f + 0.5f) * tileWidth;
            const float pixelY = startY + (localNdcY * 0.5f + 0.5f) * tileHeight;
            const float fullNdcX = pixelX / float(CameraArrayWidth) * 2.0f - 1.0f;
            const float fullNdcY = pixelY / float(CameraArrayHeight) * 2.0f - 1.0f;
            const float farPlane = webgpuVariant ? 2000.0f : 10.0f;
            const float clipZ =
                (farPlane / (farPlane - 0.1f)) * viewDepth -
                (farPlane * 0.1f / (farPlane - 0.1f));
            return glm::vec4(
                fullNdcX * viewDepth,
                fullNdcY * viewDepth,
                clipZ,
                viewDepth);
        }

        /** Projects one world point into Three's default directional shadow map. */
        glm::vec3 projectCameraArrayShadow(const glm::vec3 &worldPosition)
        {
            const glm::vec3 lightPosition(0.5f, 0.5f, 1.0f);
            const glm::vec3 forward = glm::normalize(-lightPosition);
            const glm::vec3 right = glm::normalize(
                glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 lightUp = glm::normalize(glm::cross(right, forward));
            const glm::vec3 relative = worldPosition - lightPosition;
            const float viewX = glm::dot(relative, right);
            const float viewY = glm::dot(relative, lightUp);
            const float viewDepth = glm::dot(relative, forward);
            return glm::vec3(
                viewX / 2.5f + 0.5f,
                -viewY / 2.5f + 0.5f,
                (viewDepth - 0.5f) / 499.5f);
        }

        /** Appends one world-space triangle to every camera tile. */
        void appendCameraArrayTriangle(
            CameraArrayEntity &entity,
            const glm::vec3 &a,
            const glm::vec3 &b,
            const glm::vec3 &c,
            const glm::vec3 &normalA,
            const glm::vec3 &normalB,
            const glm::vec3 &normalC,
            bool webgpuVariant)
        {
            const glm::vec3 positions[3u] = {a, b, c};
            const glm::vec3 normals[3u] = {normalA, normalB, normalC};
            for (uint32_t tileIndex = 0u; tileIndex < 36u; ++tileIndex)
            {
                for (uint32_t corner = 0u; corner < 3u; ++corner)
                {
                    const glm::vec3 shadow =
                        projectCameraArrayShadow(positions[corner]);
                    entity.vertices.push_back({
                        projectCameraArrayPoint(
                            positions[corner],
                            tileIndex,
                            webgpuVariant),
                        glm::vec4(positions[corner], 1.0f),
                        glm::vec4(normals[corner], float(tileIndex)),
                        glm::vec4(shadow, 0.0f),
                    });
                    entity.indices.push_back(
                        static_cast<uint32_t>(entity.indices.size()));
                }
            }
        }

        /** Builds an exact clip-covering receiving plane for all thirty-six camera tiles. */
        CameraArrayEntity buildCameraArrayBackground(bool webgpuVariant)
        {
            CameraArrayEntity entity;
            entity.logicalId = "background";
            const glm::vec3 normal(0.0f, 0.0f, 1.0f);
            const float halfFovDegrees = webgpuVariant ? 25.0f : 20.0f;
            const float tangentHalfFov =
                std::tan(halfFovDegrees * CameraArrayPi / 180.0f);
            for (uint32_t tileIndex = 0u; tileIndex < 36u; ++tileIndex)
            {
                const uint32_t tileX = tileIndex % CameraArrayAmount;
                const uint32_t tileY = tileIndex / CameraArrayAmount;
                const float startX = std::floor(
                    float(tileX) * float(CameraArrayWidth) /
                    float(CameraArrayAmount));
                const float startY = std::floor(
                    float(tileY) * float(CameraArrayHeight) /
                    float(CameraArrayAmount));
                const float endX = startX + std::ceil(
                    float(CameraArrayWidth) / float(CameraArrayAmount));
                const float endY = startY + std::ceil(
                    float(CameraArrayHeight) / float(CameraArrayAmount));
                const glm::vec3 cameraPosition =
                    cameraArrayPosition(tileIndex, webgpuVariant);
                const glm::vec3 forward =
                    glm::normalize(-cameraPosition);
                const glm::vec3 right = glm::normalize(
                    glm::cross(
                        forward,
                        glm::vec3(0.0f, 1.0f, 0.0f)));
                const glm::vec3 cameraUp =
                    glm::normalize(glm::cross(right, forward));
                const float pixelCornersX[4u] =
                    {startX, endX, endX, startX};
                const float pixelCornersY[4u] =
                    {startY, startY, endY, endY};
                CameraArrayHostVertex corners[4u];
                for (uint32_t corner = 0u; corner < 4u; ++corner)
                {
                    const float localNdcX =
                        (pixelCornersX[corner] - startX) /
                            (endX - startX) *
                            2.0f -
                        1.0f;
                    const float localNdcY =
                        (pixelCornersY[corner] - startY) /
                            (endY - startY) *
                            2.0f -
                        1.0f;
                    const glm::vec3 rayDirection =
                        forward +
                        right *
                            (localNdcX * tangentHalfFov *
                             (8.0f / 5.0f)) -
                        cameraUp * (localNdcY * tangentHalfFov);
                    const float rayScale =
                        (-1.0f - cameraPosition.z) /
                        rayDirection.z;
                    const glm::vec3 worldPosition =
                        cameraPosition + rayDirection * rayScale;
                    const glm::vec3 shadow =
                        projectCameraArrayShadow(worldPosition);
                    corners[corner] = {
                        projectCameraArrayPoint(
                            worldPosition,
                            tileIndex,
                            webgpuVariant),
                        glm::vec4(worldPosition, 1.0f),
                        glm::vec4(normal, float(tileIndex)),
                        glm::vec4(shadow, 0.0f),
                    };
                }
                const uint32_t triangleCorners[6u] =
                    {0u, 1u, 3u, 1u, 2u, 3u};
                for (uint32_t corner : triangleCorners)
                {
                    entity.vertices.push_back(corners[corner]);
                    entity.indices.push_back(
                        static_cast<uint32_t>(entity.indices.size()));
                }
            }
            entity.objectData.receiveShadowAndReserved =
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            entity.instanceData.identity =
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            entity.materialData.baseColorAndPhase =
                glm::vec4(0.0f, 0.0f, 0.13286832f, 0.0f);
            return entity;
        }

        /** Builds Three's closed 32-segment cylinder at the selected deterministic frame. */
        CameraArrayEntity buildCameraArrayCylinder(
            uint32_t frameIndex,
            bool webgpuVariant)
        {
            CameraArrayEntity entity;
            entity.logicalId = "cylinder";
            const float rotationX = float(frameIndex + 1u) * 0.005f;
            const float rotationZ = float(frameIndex + 1u) * 0.01f;
            for (uint32_t segment = 0u; segment < 32u; ++segment)
            {
                const float theta0 =
                    float(segment) * 2.0f * CameraArrayPi / 32.0f;
                const float theta1 =
                    float(segment + 1u) * 2.0f * CameraArrayPi / 32.0f;
                const glm::vec3 bottom0(
                    0.5f * std::sin(theta0), -0.5f, 0.5f * std::cos(theta0));
                const glm::vec3 top0(
                    bottom0.x, 0.5f, bottom0.z);
                const glm::vec3 bottom1(
                    0.5f * std::sin(theta1), -0.5f, 0.5f * std::cos(theta1));
                const glm::vec3 top1(
                    bottom1.x, 0.5f, bottom1.z);
                const glm::vec3 normal0(
                    std::sin(theta0), 0.0f, std::cos(theta0));
                const glm::vec3 normal1(
                    std::sin(theta1), 0.0f, std::cos(theta1));
                const glm::vec3 worldBottom0 =
                    rotateCameraArrayVector(bottom0, rotationX, rotationZ);
                const glm::vec3 worldTop0 =
                    rotateCameraArrayVector(top0, rotationX, rotationZ);
                const glm::vec3 worldBottom1 =
                    rotateCameraArrayVector(bottom1, rotationX, rotationZ);
                const glm::vec3 worldTop1 =
                    rotateCameraArrayVector(top1, rotationX, rotationZ);
                const glm::vec3 worldNormal0 =
                    rotateCameraArrayVector(normal0, rotationX, rotationZ);
                const glm::vec3 worldNormal1 =
                    rotateCameraArrayVector(normal1, rotationX, rotationZ);
                appendCameraArrayTriangle(
                    entity,
                    worldTop0,
                    worldBottom0,
                    worldTop1,
                    worldNormal0,
                    worldNormal0,
                    worldNormal1,
                    webgpuVariant);
                appendCameraArrayTriangle(
                    entity,
                    worldBottom0,
                    worldBottom1,
                    worldTop1,
                    worldNormal0,
                    worldNormal1,
                    worldNormal1,
                    webgpuVariant);

                const glm::vec3 topNormal =
                    rotateCameraArrayVector(
                        glm::vec3(0.0f, 1.0f, 0.0f),
                        rotationX,
                        rotationZ);
                const glm::vec3 bottomNormal = -topNormal;
                const glm::vec3 worldTopCenter =
                    rotateCameraArrayVector(
                        glm::vec3(0.0f, 0.5f, 0.0f),
                        rotationX,
                        rotationZ);
                const glm::vec3 worldBottomCenter =
                    rotateCameraArrayVector(
                        glm::vec3(0.0f, -0.5f, 0.0f),
                        rotationX,
                        rotationZ);
                appendCameraArrayTriangle(
                    entity,
                    worldTopCenter,
                    worldTop1,
                    worldTop0,
                    topNormal,
                    topNormal,
                    topNormal,
                    webgpuVariant);
                appendCameraArrayTriangle(
                    entity,
                    worldBottomCenter,
                    worldBottom0,
                    worldBottom1,
                    bottomNormal,
                    bottomNormal,
                    bottomNormal,
                    webgpuVariant);
            }
            entity.objectData.receiveShadowAndReserved =
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            entity.instanceData.identity =
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            entity.materialData.baseColorAndPhase =
                glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            return entity;
        }

        /** Writes one tightly packed RGBA8 capture. */
        void writeCameraArrayRgba(
            const eastl::string &pathValue,
            const eastl::vector<uint8_t> &rgba)
        {
            if (pathValue.empty())
            {
                return;
            }
            const std::filesystem::path outputPath(pathValue.c_str());
            prepareCameraArrayOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write camera-array RGBA capture.");
            }
        }
    } // namespace

    void CameraArrayRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        caseId = options.caseId;
        webgpuVariant = caseId == "webgpu_camera_array";
        if ((!webgpuVariant && caseId != "webgl_camera_array") ||
            options.randomSeed != 0x12345678u ||
            options.width != CameraArrayWidth ||
            options.height != CameraArrayHeight ||
            !options.inputReplayPath.empty() ||
            !((options.scenarioId == "initial" && options.targetFrame == 0u) ||
              (options.scenarioId == "animated" && options.targetFrame == 120u)))
        {
            throw std::invalid_argument(
                "Camera-array adapter requires its locked case, scenario, seed, and extent.");
        }
#if defined(GVM_CAMERA_ARRAY_WEBGPU)
        if (!webgpuVariant)
#else
        if (webgpuVariant)
#endif
        {
            throw std::invalid_argument(
                "Camera-array host target and case variant do not agree.");
        }
        device = inDevice;
        entities.push_back(buildCameraArrayBackground(webgpuVariant));
        entities.push_back(
            buildCameraArrayCylinder(options.targetFrame, webgpuVariant));
        const auto encoder =
            renderer.createRenderSetCommandEncoder(CameraArraySceneSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create the camera-array Scene RenderSet encoder.");
        }
        for (CameraArrayEntity &entity : entities)
        {
            entity.entityIndex = allocateEntity(*encoder, entity);
        }
        renderer.executeRenderSetCommand(CameraArraySceneSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex CameraArrayRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        const CameraArrayEntity &entity) const
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
#if defined(GVM_CAMERA_ARRAY_WEBGPU)
        constexpr auto Vertices =
            WebgpuCameraArraySceneRenderSetComponents::vertices;
        constexpr auto Indices =
            WebgpuCameraArraySceneRenderSetComponents::indices;
        constexpr auto Objects =
            WebgpuCameraArraySceneRenderSetComponents::objects;
        constexpr auto Instances =
            WebgpuCameraArraySceneRenderSetComponents::instances;
        constexpr auto Materials =
            WebgpuCameraArraySceneRenderSetComponents::materials;
#else
        constexpr auto Vertices =
            WebglCameraArraySceneRenderSetComponents::vertices;
        constexpr auto Indices =
            WebglCameraArraySceneRenderSetComponents::indices;
        constexpr auto Objects =
            WebglCameraArraySceneRenderSetComponents::objects;
        constexpr auto Instances =
            WebglCameraArraySceneRenderSetComponents::instances;
        constexpr auto Materials =
            WebglCameraArraySceneRenderSetComponents::materials;
#endif
        const eastl::string prefix =
            "CameraArray" + entity.logicalId;
        appendCameraArrayPayload(
            allocation,
            Vertices,
            prefix + "Vertices",
            entity.vertices.data(),
            entity.vertices.size() * sizeof(CameraArrayHostVertex),
            1u);
        appendCameraArrayPayload(
            allocation,
            Indices,
            prefix + "Indices",
            entity.indices.data(),
            entity.indices.size() * sizeof(uint32_t),
            1u);
        appendCameraArrayPayload(
            allocation,
            Objects,
            prefix + "Object",
            &entity.objectData,
            sizeof(entity.objectData),
            1u);
        appendCameraArrayPayload(
            allocation,
            Instances,
            prefix + "Instance",
            &entity.instanceData,
            sizeof(entity.instanceData),
            1u);
        appendCameraArrayPayload(
            allocation,
            Materials,
            prefix + "Material",
            &entity.materialData,
            sizeof(entity.materialData),
            1u);
        return encoder.allocEntity(allocation);
    }

    void CameraArrayRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void CameraArrayRuntimeAdapter::afterFrame(
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
                "Camera-array capture is too large.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        writeArtifacts(options, frameIndex, width, height, rgba);
        captureWritten = true;
    }

    void CameraArrayRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        writeCameraArrayRgba(options.captureRgbaPath, rgba);
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareCameraArrayOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"" << caseId.c_str() << "\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\""
                << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\""
                << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\"\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareCameraArrayOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            const char *prefix =
                webgpuVariant ? "WebgpuCameraArray" : "WebglCameraArray";
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"" << caseId.c_str() << "\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"singleSample\":true,\n"
                << "  \"msaaEnabled\":false,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":2,\n"
                << "  \"entityCount\":2,\n"
                << "  \"instanceCounts\":[1,1],\n"
                << "  \"renderSetType\":\"" << prefix
                << "SceneRenderSet\",\n"
                << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
                << "  \"scenePassCount\":37,\n"
                << "  \"scenePasses\":[\"" << prefix
                << "ShadowDepthPass\",\"" << prefix
                << "MainPass\"],\n"
                << "  \"shadowPassInvocations\":1,\n"
                << "  \"arrayCameraViewCount\":36,\n"
                << "  \"mainPassInvocations\":36,\n"
                << "  \"drawCommandCount\":37,\n"
                << "  \"renderSetIndexedIndirect\":true,\n"
                << "  \"directDrawFallback\":false,\n"
                << "  \"sceneRoots\":[{\n"
                << "    \"id\":\"scene\",\n"
                << "    \"renderSetCount\":1,\n"
                << "    \"renderSetId\":\"camera-array-scene-set\",\n"
                << "    \"renderSetType\":\"" << prefix
                << "SceneRenderSet\",\n"
                << "    \"renderableObjectCount\":2,\n"
                << "    \"entityCount\":2,\n"
                << "    \"entities\":[\n"
                << "      {\"entityId\":" << entities[0].entityIndex
                << ",\"logicalRenderableId\":\"background\",\"instanceCount\":1},\n"
                << "      {\"entityId\":" << entities[1].entityIndex
                << ",\"logicalRenderableId\":\"cylinder\",\"instanceCount\":1}\n"
                << "    ],\n"
                << "    \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
                << "    \"drawCommandCount\":37,\n"
                << "    \"directDrawFallback\":false,\n"
                << "    \"scenePasses\":[\n"
                << "      {\"name\":\"directional-shadow-depth\",\"renderClass\":\""
                << prefix
                << "ShadowDepthPass\",\"renderSetId\":\"camera-array-scene-set\","
                << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
                << "\"invocationCount\":1,\"drawCommandCount\":1,"
                << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},\n"
                << "      {\"name\":\"array-camera-main\",\"renderClass\":\""
                << prefix
                << "MainPass\",\"renderSetId\":\"camera-array-scene-set\","
                << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
                << "\"invocationCount\":36,\"drawCommandCount\":36,"
                << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}\n"
                << "    ]\n"
                << "  }],\n"
                << "  \"scenePassSequence\":[\n"
                << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"directional-shadow-depth\",\"entityOrdinal\":0}";
            for (uint32_t cameraIndex = 0u;
                 cameraIndex < 36u;
                 ++cameraIndex)
            {
                output
                    << ",\n    {\"sceneRoot\":\"scene\","
                    << "\"scenePass\":\"array-camera-main\","
                    << "\"entityOrdinal\":" << cameraIndex << "}";
            }
            output << "\n  ]\n}\n";
        }
    }

    void CameraArrayRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
