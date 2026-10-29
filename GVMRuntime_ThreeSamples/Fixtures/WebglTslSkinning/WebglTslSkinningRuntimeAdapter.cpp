#include "WebglTslSkinningRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/array.h>
#include <EASTL/string.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        /** Stores one complete texture upload and its explicit mip offsets. */
        struct WebglTslSkinningTextureUpload final
        {
            eastl::vector<uint8_t> bytes;
            eastl::vector<uint64_t> mipOffsets;
            uint32_t width = 0u;
            uint32_t height = 0u;
            bool srgb = false;
        };

        /** Converts one sRGB channel to linear light. */
        float webglTslSkinningSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Composes one immutable Michelle rest transform. */
        glm::mat4 composeWebglTslSkinningNode(const MichelleGlbNode &node)
        {
            return glm::translate(glm::mat4(1.0f), node.translation) *
                glm::toMat4(node.rotation) *
                glm::scale(glm::mat4(1.0f), node.scale);
        }

        /** Computes one rest world matrix from the validated parent chain. */
        glm::mat4 computeWebglTslSkinningRestWorld(
            const MichelleGlbAsset &asset,
            uint32_t nodeIndex)
        {
            eastl::vector<uint32_t> chain;
            int32_t current = static_cast<int32_t>(nodeIndex);
            while (current >= 0)
            {
                chain.push_back(static_cast<uint32_t>(current));
                current = asset.nodes[static_cast<uint32_t>(current)].parent;
            }
            glm::mat4 world(1.0f);
            for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator)
            {
                world *= composeWebglTslSkinningNode(asset.nodes[*iterator]);
            }
            return world;
        }

        /** Builds one explicit material mip upload without automatic GPU generation. */
        WebglTslSkinningTextureUpload buildWebglTslSkinningTextureUpload(
            const RgbaImageData &baseImage,
            bool srgb)
        {
            WebglTslSkinningTextureUpload upload;
            upload.width = baseImage.width;
            upload.height = baseImage.height;
            upload.srgb = srgb;
            const eastl::vector<RgbaImageData> mips = srgb
                ? buildSrgbMipChain(baseImage)
                : buildUnormMipChain(baseImage);
            for (const RgbaImageData &mip : mips)
            {
                upload.mipOffsets.push_back(upload.bytes.size());
                upload.bytes.insert(
                    upload.bytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
            return upload;
        }

        /** Appends one typed payload to the Michelle entity allocation. */
        void appendWebglTslSkinningPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
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

        /** Converts one CPU-skinned pose into clip and world vertex attributes. */
        void updateWebglTslSkinningVertices(
            const MichelleGlbAsset &asset,
            const eastl::vector<glm::vec4> &positions,
            const eastl::vector<glm::vec4> &normals,
            const glm::mat4 &model,
            eastl::vector<WebglTslSkinningHostVertex> &vertices)
        {
            if (positions.size() != asset.vertices.size() ||
                normals.size() != asset.vertices.size())
            {
                throw std::invalid_argument(
                    "Michelle sampled attributes differ from the mesh.");
            }
            vertices.resize(asset.vertices.size());
            glm::vec2 minimumNdc(std::numeric_limits<float>::max());
            glm::vec2 maximumNdc(std::numeric_limits<float>::lowest());
            glm::vec3 minimumWorld(std::numeric_limits<float>::max());
            glm::vec3 maximumWorld(std::numeric_limits<float>::lowest());
            const glm::vec3 eye(1.0f, 2.0f, 3.0f);
            const glm::vec3 side(
                0.9486832981f, 0.0f, -0.3162277660f);
            const glm::vec3 cameraUp(
                -0.0953462589f, 0.9534625892f, -0.2860387768f);
            const glm::vec3 negativeForward(
                0.3015113446f, 0.3015113446f, 0.9045340337f);
            const float tangent = std::tan(glm::radians(25.0f));
            const float projectionX = 1.0f / ((800.0f / 500.0f) * tangent);
            const float projectionY = -1.0f / tangent;
            constexpr float ProjectionZ = 100.0f / (0.01f - 100.0f);
            constexpr float ProjectionOffset =
                -(100.0f * 0.01f) / (100.0f - 0.01f);
            for (size_t vertex = 0u; vertex < asset.vertices.size(); ++vertex)
            {
                const glm::vec4 worldPosition = model * positions[vertex];
                const glm::vec3 relative = glm::vec3(worldPosition) - eye;
                const glm::vec3 cameraPosition(
                    glm::dot(side, relative),
                    glm::dot(cameraUp, relative),
                    glm::dot(negativeForward, relative));
                const glm::vec4 clipPosition(
                    cameraPosition.x * projectionX,
                    cameraPosition.y * projectionY,
                    cameraPosition.z * ProjectionZ + ProjectionOffset,
                    -cameraPosition.z);
                if (clipPosition.w <= 0.0f)
                {
                    throw std::runtime_error(
                        "Michelle sampled vertex is behind the frozen camera.");
                }
                const glm::vec2 ndc = glm::vec2(clipPosition) / clipPosition.w;
                minimumNdc = glm::min(minimumNdc, ndc);
                maximumNdc = glm::max(maximumNdc, ndc);
                minimumWorld = glm::min(
                    minimumWorld, glm::vec3(worldPosition));
                maximumWorld = glm::max(
                    maximumWorld, glm::vec3(worldPosition));
                vertices[vertex] = {
                    clipPosition,
                    worldPosition,
                    glm::vec4(glm::normalize(glm::vec3(
                        model * normals[vertex])), 0.0f),
                    asset.vertices[vertex].uv,
                };
            }
            if (minimumNdc.x < -0.5f || maximumNdc.x > 0.5f ||
                minimumNdc.y < -0.8f || maximumNdc.y > 0.8f ||
                maximumNdc.y - minimumNdc.y < 0.4f)
            {
                std::ostringstream message;
                message << "Michelle frozen camera produced invalid clip bounds: "
                        << minimumNdc.x << ',' << minimumNdc.y << " to "
                        << maximumNdc.x << ',' << maximumNdc.y << "; world "
                        << minimumWorld.x << ',' << minimumWorld.y << ','
                        << minimumWorld.z << " to " << maximumWorld.x << ','
                        << maximumWorld.y << ',' << maximumWorld.z << '.';
                throw std::runtime_error(message.str());
            }
        }

        /** Creates parent directories for one optional evidence artifact. */
        void prepareWebglTslSkinningEvidencePath(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional UTF-8 evidence artifact. */
        void writeWebglTslSkinningEvidence(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebglTslSkinningEvidencePath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write webgl_tsl_skinning evidence.");
            }
        }
    } // namespace

    void WebglTslSkinningRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool validScenario =
            (options.scenarioId == "loader-snapshot" && options.targetFrame == 0u) ||
            (options.scenarioId == "initial-loader" && options.targetFrame == 0u) ||
            (options.scenarioId == "animated" && options.targetFrame == 60u);
        if (options.caseId != "webgl_tsl_skinning" || !validScenario ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() || !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgl_tsl_skinning requires its three frozen r185 scenarios.");
        }
        device = inDevice;
        const std::filesystem::path assetPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "models/gltf/Michelle.glb";
        asset = loadMichelleGlbAsset(assetPath);
        sampleMichelleSkinnedVertices(
            asset, 0.0f, skinnedPositions, skinnedNormals);
        objectModel = computeWebglTslSkinningRestWorld(
            asset, asset.meshNodeIndex);
        const glm::vec3 cameraPosition(1.0f, 2.0f, 3.0f);
        objectData.cameraPositionAndExposure = glm::vec4(cameraPosition, 0.4f);
        updateWebglTslSkinningVertices(
            asset, skinnedPositions, skinnedNormals,
            objectModel, vertices);
        instanceData.reserved = glm::vec4(0.0f);
        materialData.ambientColorAndPointIntensity = glm::vec4(
            webglTslSkinningSrgbToLinear(0x44u / 255.0f),
            webglTslSkinningSrgbToLinear(0x66u / 255.0f),
            1.0f,
            2500.0f / (4.0f * 3.14159265359f));
        materialData.pointPositionAndIor = glm::vec4(cameraPosition, 1.45f);
        materialData.metallicDistanceAndReserved = glm::vec4(
            0.5f, 100.0f, 0.0f, 0.0f);

        eastl::array<WebglTslSkinningTextureUpload, 4u> textureUploads;
        textureUploads[0u] = buildWebglTslSkinningTextureUpload(
            asset.textures[0u], true);
        textureUploads[1u] = buildWebglTslSkinningTextureUpload(
            asset.textures[1u], false);
        textureUploads[2u] = buildWebglTslSkinningTextureUpload(
            asset.textures[2u], true);
        textureUploads[3u] = buildWebglTslSkinningTextureUpload(
            asset.textures[3u], false);

        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create the webgl_tsl_skinning Set encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(asset.indices.size());
        allocation.instanceCount = 1u;
        appendWebglTslSkinningPayload(
            allocation, WebglTslSkinningSceneRenderSetComponents::vertices,
            "WebglTslSkinningVertices", vertices.data(),
            vertices.size() * sizeof(WebglTslSkinningHostVertex), 1u);
        appendWebglTslSkinningPayload(
            allocation, WebglTslSkinningSceneRenderSetComponents::indices,
            "WebglTslSkinningIndices", asset.indices.data(),
            asset.indices.size() * sizeof(uint32_t), 1u);
        appendWebglTslSkinningPayload(
            allocation, WebglTslSkinningSceneRenderSetComponents::objects,
            "WebglTslSkinningObject", &objectData, sizeof(objectData), 1u);
        appendWebglTslSkinningPayload(
            allocation, WebglTslSkinningSceneRenderSetComponents::instances,
            "WebglTslSkinningInstance", &instanceData, sizeof(instanceData), 1u);
        appendWebglTslSkinningPayload(
            allocation, WebglTslSkinningSceneRenderSetComponents::materials,
            "WebglTslSkinningMaterial", &materialData, sizeof(materialData), 1u);
        GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
        textureInfo.textureComponentHandle =
            WebglTslSkinningSceneRenderSetComponents::textures;
        constexpr const char *TextureNames[] = {
            "MichelleSpecular", "MichelleNormal",
            "MichelleBaseColor", "MichelleMetallicRoughness"};
        for (uint32_t texture = 0u; texture < textureUploads.size(); ++texture)
        {
            const WebglTslSkinningTextureUpload &upload = textureUploads[texture];
            textureInfo.textures.push_back({
                .textureName = TextureNames[texture],
                .format = upload.srgb
                    ? GVM::RHI::TextureFormat::RGBA8UnormSrgb
                    : GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = upload.width,
                .height = upload.height,
                .data = upload.bytes.data(),
                .dataStorageBytes = upload.bytes.size(),
                .mipmapOffsetBytes = upload.mipOffsets,
            });
        }
        allocation.textureInfos.push_back(eastl::move(textureInfo));
        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglTslSkinningRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        sampleMichelleSkinnedVertices(
            asset, static_cast<float>(frameIndex) / 60.0f,
            skinnedPositions, skinnedNormals);
        updateWebglTslSkinningVertices(
            asset, skinnedPositions, skinnedNormals,
            objectModel, vertices);
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not update the webgl_tsl_skinning vertex component.");
        }
        encoder->setBufferComponentData(
            entityIndex,
            WebglTslSkinningSceneRenderSetComponents::vertices,
            vertices.data(), vertices.size() * sizeof(WebglTslSkinningHostVertex),
            0u, static_cast<uint32_t>(vertices.size()));
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglTslSkinningRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("webgl_tsl_skinning capture is too large.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareWebglTslSkinningEvidencePath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write webgl_tsl_skinning RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgl_tsl_skinning\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"randomSeed\":" << options.randomSeed << ",\n"
                 << "  \"width\":" << width << ",\n"
                 << "  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << width * 4u << ",\n"
                 << "  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"sampleCount\":1,\n"
                 << "  \"msaaEnabled\":false,\n"
                 << "  \"inputReplay\":null\n}\n";
        writeWebglTslSkinningEvidence(
            options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene << "{\n  \"schemaVersion\":1,\n"
              << "  \"caseId\":\"webgl_tsl_skinning\",\n"
              << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
              << "  \"frame\":" << frameIndex << ",\n"
              << "  \"renderSetPolicy\":\"required\",\n"
              << "  \"gpuWorkDslOnly\":true,\n"
              << "  \"sceneRenderSetCount\":1,\n"
              << "  \"renderableObjectCount\":1,\n"
              << "  \"entityCount\":1,\n"
              << "  \"instanceCount\":1,\n"
              << "  \"containsInstancing\":false,\n"
              << "  \"scenePassCount\":2,\n"
              << "  \"screenPassCount\":1,\n"
              << "  \"screenPasses\":[\"solid-background\"],\n"
              << "  \"scenePassSequence\":[\n"
              << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main-michelle-double-back\"},\n"
              << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main-michelle-front\"}\n"
              << "  ],\n"
              << "  \"drawCommandCount\":2,\n"
              << "  \"directDrawFallback\":false,\n"
              << "  \"sampleCount\":1,\n"
              << "  \"msaaEnabled\":false,\n"
              << "  \"jointCount\":65,\n"
              << "  \"textureSlotCount\":4,\n"
              << "  \"sceneRoots\":[{\n"
              << "    \"id\":\"scene\",\n"
              << "    \"renderSetCount\":1,\n"
              << "    \"renderSetId\":\"scene\",\n"
              << "    \"renderSetType\":\"WebglTslSkinningSceneRenderSet\",\n"
              << "    \"renderableObjectCount\":1,\n"
              << "    \"entityCount\":1,\n"
              << "    \"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"michelle\",\"instanceCount\":1}],\n"
              << "    \"componentSchema\":[\n"
              << "      {\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},\n"
              << "      {\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},\n"
              << "      {\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},\n"
              << "      {\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},\n"
              << "      {\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},\n"
              << "      {\"name\":\"skinPalettes\",\"kind\":\"buffer\",\"role\":\"michelle-65-joint-palette\"},\n"
              << "      {\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"front-back-material-phase\"},\n"
              << "      {\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"specular-normal-base-metallic-roughness-fixed-slots\"}\n"
              << "    ],\n"
              << "    \"drawCommandCount\":2,\n"
              << "    \"directDrawFallback\":false,\n"
              << "    \"scenePasses\":[\n"
              << "      {\"name\":\"main-michelle-double-back\",\"renderClass\":\"WebglTslSkinningBackPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},\n"
              << "      {\"name\":\"main-michelle-front\",\"renderClass\":\"WebglTslSkinningFrontPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}\n"
              << "    ]\n"
              << "  }]\n}\n";
        writeWebglTslSkinningEvidence(options.sceneSnapshotPath, scene.str());
        if (options.scenarioId == "loader-snapshot")
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\":1,\n"
                     << "  \"caseId\":\"webgl_tsl_skinning\",\n"
                     << "  \"scenarioId\":\"loader-snapshot\",\n"
                     << "  \"frame\":0,\n"
                     << "  \"kind\":\"loader-snapshot\",\n"
                     << "  \"canonicalState\":\"michelle-67-nodes-one-primitive-65-joints-four-textures-two-clips\",\n"
                     << "  \"result\":{\n"
                     << "    \"renderableObjectCount\":1,\n"
                     << "    \"sceneRootCount\":1,\n"
                     << "    \"canonicalSceneSha256\":\"12f5c3d8d790ebb8f376a80d12d95610fd8bdfffd1ec31e0844bebcd9bd2896a\",\n"
                     << "    \"assetSha256\":\"" << asset.sha256.c_str() << "\",\n"
                     << "    \"vertexCount\":16340,\n"
                     << "    \"indexCount\":84318,\n"
                     << "    \"nodeCount\":67,\n"
                     << "    \"jointCount\":65,\n"
                     << "    \"animationChannelCount\":195\n"
                     << "  }\n}\n";
            writeWebglTslSkinningEvidence(
                options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglTslSkinningRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        skinnedPositions.clear();
        skinnedNormals.clear();
        asset = {};
    }
} // namespace GVM::ThreeSamples
