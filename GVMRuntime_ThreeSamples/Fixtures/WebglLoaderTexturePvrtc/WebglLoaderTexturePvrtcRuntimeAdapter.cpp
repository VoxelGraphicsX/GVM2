#include "WebglLoaderTexturePvrtcRuntimeAdapter.hpp"

#include "TexturedBoxSampleData.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr uint32_t EntityCount = 8u;

        /** Describes one frozen PVR asset, geometry, material phase, and placement. */
        struct PvrEntityDefinition final
        {
            const char *fileName;
            uint32_t phase;
            bool cubeEnvironment;
            float x;
            float y;
        };

        constexpr PvrEntityDefinition EntityDefinitions[EntityCount] = {
            {"disturb_4bpp_rgb.pvr", 0u, false, -500.0f, 200.0f},
            {"disturb_4bpp_rgb_mips.pvr", 0u, false, -166.0f, 200.0f},
            {"disturb_2bpp_rgb.pvr", 0u, false, 166.0f, 200.0f},
            {"disturb_4bpp_rgb_v3.pvr", 0u, false, 500.0f, 200.0f},
            {"flare_4bpp_rgba.pvr", 2u, false, -500.0f, -200.0f},
            {"flare_2bpp_rgba.pvr", 2u, false, -166.0f, -200.0f},
            {"park3_cube_nomip_4bpp_rgb.pvr", 0u, true, 166.0f, -200.0f},
            {"park3_cube_mip_2bpp_rgb_v3.pvr", 0u, true, 500.0f, -200.0f},
        };

        /** Validates one locked 800x500 PVR loader scenario. */
        void validatePvrScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-loader" &&
                options.targetFrame == 0u;
            const bool loader = options.scenarioId == "canonical-loader" &&
                options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" &&
                options.targetFrame == 120u;
            if (options.caseId != "webgl_loader_texture_pvrtc" ||
                (!initial && !loader && !animated) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                !options.inputReplayPath.empty() || options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "PVR adapter requires one locked 800x500 r185 loader scenario.");
            }
        }

        /** Builds the exact BoxGeometry(200,200,200) vertex and index streams. */
        void buildPvrBoxGeometry(
            eastl::vector<WebglLoaderTexturePvrtcHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            eastl::vector<TexturedBoxHostVertex> sourceVertices;
            buildTexturedBoxGeometry(200.0f, 200.0f, 200.0f, sourceVertices, indices);
            constexpr glm::vec3 FaceNormals[6u] = {
                {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
                {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
            vertices.clear();
            vertices.reserve(sourceVertices.size());
            for (uint32_t vertex = 0u; vertex < sourceVertices.size(); ++vertex)
            {
                const TexturedBoxHostVertex &source = sourceVertices[vertex];
                vertices.push_back({
                    .position = {source.position.x, source.position.y, source.position.z, 1.0f},
                    .normal = {FaceNormals[vertex / 4u], 0.0f},
                    .textureCoordinate = {source.texCoord.x, source.texCoord.y, 0.0f, 0.0f},
                });
            }
        }

        /** Builds the exact indexed TorusGeometry(100,50,32,24) stream. */
        void buildPvrTorusGeometry(
            eastl::vector<WebglLoaderTexturePvrtcHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t RadialSegments = 32u;
            constexpr uint32_t TubularSegments = 24u;
            constexpr float Radius = 100.0f;
            constexpr float Tube = 50.0f;
            vertices.clear();
            indices.clear();
            vertices.reserve((RadialSegments + 1u) * (TubularSegments + 1u));
            for (uint32_t radial = 0u; radial <= RadialSegments; ++radial)
            {
                const float v = float(radial) / float(RadialSegments) * glm::two_pi<float>();
                for (uint32_t tubular = 0u; tubular <= TubularSegments; ++tubular)
                {
                    const float u = float(tubular) / float(TubularSegments) * glm::two_pi<float>();
                    const glm::vec3 center(Radius * std::cos(u), Radius * std::sin(u), 0.0f);
                    const glm::vec3 position(
                        (Radius + Tube * std::cos(v)) * std::cos(u),
                        (Radius + Tube * std::cos(v)) * std::sin(u),
                        Tube * std::sin(v));
                    vertices.push_back({
                        .position = {position, 1.0f},
                        .normal = {glm::normalize(position - center), 0.0f},
                        .textureCoordinate = {
                            1.0f - float(tubular) / float(TubularSegments),
                            float(radial) / float(RadialSegments), 0.0f, 0.0f},
                    });
                }
            }
            for (uint32_t radial = 1u; radial <= RadialSegments; ++radial)
            {
                for (uint32_t tubular = 1u; tubular <= TubularSegments; ++tubular)
                {
                    const uint32_t a = (TubularSegments + 1u) * radial + tubular - 1u;
                    const uint32_t b = (TubularSegments + 1u) * (radial - 1u) + tubular - 1u;
                    const uint32_t c = (TubularSegments + 1u) * (radial - 1u) + tubular;
                    const uint32_t d = (TubularSegments + 1u) * radial + tubular;
                    indices.insert(indices.end(), {a, b, d, b, c, d});
                }
            }
            if (vertices.size() != 825u || indices.size() != 4608u)
                throw std::runtime_error("PVR TorusGeometry topology diverged from r185.");
        }

        /** Appends one typed buffer component to an entity allocation. */
        void appendPvrBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Packs every authored mip of one decoded PVR face contiguously. */
        void packPvrTextureFace(
            const PvrRgba8Texture &texture,
            uint32_t face,
            eastl::vector<uint8_t> &bytes,
            eastl::vector<uint64_t> &mipOffsets)
        {
            bytes.clear();
            mipOffsets.clear();
            for (uint32_t mip = 0u; mip < texture.mipCount; ++mip)
            {
                mipOffsets.push_back(bytes.size());
                const RgbaImageData &image =
                    texture.faceMipImages[size_t(face) * texture.mipCount + mip];
                bytes.insert(bytes.end(), image.pixels.begin(), image.pixels.end());
            }
        }

        /** Creates parent folders for one explicitly requested artifact. */
        void preparePvrOutputPath(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path output(path.c_str());
            if (!output.parent_path().empty())
                std::filesystem::create_directories(output.parent_path());
        }
    } // namespace

    void WebglLoaderTexturePvrtcRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validatePvrScenario(options);
        device = inDevice;
        buildPvrBoxGeometry(boxVertices, boxIndices);
        buildPvrTorusGeometry(torusVertices, torusIndices);
        const std::filesystem::path textureRoot =
            std::filesystem::path(options.assetRoot.c_str()) / "textures" / "compressed";
        const double timeSeconds = 1700000000.0 + double(options.targetFrame) / 60.0;
        const glm::mat4 rotation = makeThreeEulerXyRotation(-timeSeconds, timeSeconds);
        glm::mat4 view(1.0f);
        view[3u][2u] = -1000.0f;
        const glm::mat4 projection = makeThreePerspectiveProjection(
            options.width, options.height, 50.0, 1.0, 2000.0);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("PVR adapter could not create its Scene Set encoder.");

        for (uint32_t entity = 0u; entity < EntityCount; ++entity)
        {
            textures[entity] = decodePvrRgba8(textureRoot / EntityDefinitions[entity].fileName);
            for (uint32_t face = 0u; face < textures[entity].faceCount; ++face)
            {
                packPvrTextureFace(
                    textures[entity], face,
                    textureFaceBytes[entity][face],
                    textureFaceMipOffsets[entity][face]);
            }
            glm::mat4 model(1.0f);
            model[3u][0u] = EntityDefinitions[entity].x;
            model[3u][1u] = -EntityDefinitions[entity].y;
            model *= rotation;
            const glm::mat4 modelView = view * model;
            objects[entity] = {
                .modelViewProjection = projection * modelView,
                .modelView = modelView,
                .phaseAndFlags = {EntityDefinitions[entity].phase, 0u, 0u, 0u},
            };
            instances[entity].reserved = glm::vec4(0.0f);
            materials[entity].ambientPointRoughnessOpacity = glm::vec4(0.0f);
            renderFlags[entity] = glm::uvec4(
                EntityDefinitions[entity].phase, 1u,
                EntityDefinitions[entity].cubeEnvironment ? 1u : 0u,
                0u);

            const auto &entityVertices = EntityDefinitions[entity].cubeEnvironment
                ? torusVertices : boxVertices;
            const auto &entityIndices = EntityDefinitions[entity].cubeEnvironment
                ? torusIndices : boxIndices;
            const eastl::string prefix =
                "WebglLoaderTexturePvrtcEntity" + eastl::to_string(entity);
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entityVertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entityIndices.size());
            allocation.instanceCount = 1u;
            appendPvrBuffer(allocation, WebglLoaderTexturePvrtcSceneRenderSetComponents::vertices,
                prefix + "Vertices", entityVertices.data(), entityVertices.size() * sizeof(entityVertices[0u]));
            appendPvrBuffer(allocation, WebglLoaderTexturePvrtcSceneRenderSetComponents::indices,
                prefix + "Indices", entityIndices.data(), entityIndices.size() * sizeof(entityIndices[0u]));
            appendPvrBuffer(allocation, WebglLoaderTexturePvrtcSceneRenderSetComponents::objects,
                prefix + "Object", &objects[entity], sizeof(objects[entity]));
            appendPvrBuffer(allocation, WebglLoaderTexturePvrtcSceneRenderSetComponents::instances,
                prefix + "Instance", &instances[entity], sizeof(instances[entity]));
            appendPvrBuffer(allocation, WebglLoaderTexturePvrtcSceneRenderSetComponents::materials,
                prefix + "Material", &materials[entity], sizeof(materials[entity]));
            appendPvrBuffer(allocation, WebglLoaderTexturePvrtcSceneRenderSetComponents::renderFlags,
                prefix + "RenderFlags", &renderFlags[entity], sizeof(renderFlags[entity]));
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebglLoaderTexturePvrtcSceneRenderSetComponents::textures;
            for (uint32_t face = 0u; face < textures[entity].faceCount; ++face)
            {
                textureComponent.textures.push_back({
                    .textureName = prefix + "TextureFace" + eastl::to_string(face),
                    .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                    .width = textures[entity].width,
                    .height = textures[entity].height,
                    .data = textureFaceBytes[entity][face].data(),
                    .dataStorageBytes = textureFaceBytes[entity][face].size(),
                    .mipmapOffsetBytes = textureFaceMipOffsets[entity][face],
                });
            }
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderTexturePvrtcRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderTexturePvrtcRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        eastl::vector<uint8_t> rgba(size_t(width) * height * 4u);
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            preparePvrOutputPath(options.captureRgbaPath);
            std::ofstream output(options.captureRgbaPath.c_str(), std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write PVR RGBA capture.");
        }
        if (!options.captureMetadataPath.empty())
        {
            preparePvrOutputPath(options.captureMetadataPath);
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_loader_texture_pvrtc\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\": " << frameIndex << ",\n"
                << "  \"randomSeed\": " << options.randomSeed << ",\n"
                << "  \"width\": " << width << ", \"height\": " << height << ",\n"
                << "  \"rowStrideBytes\": " << width * 4u << ",\n"
                << "  \"byteCount\": " << rgba.size() << ",\n"
                << "  \"format\": \"rgba8unorm\",\n"
                << "  \"sampleCount\": 1,\n"
                << "  \"msaaEnabled\": false,\n"
                << "  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                << "  \"singleSamplePolicy\":{\"sampleCount\":1,\"msaaEnabled\":false,\"simulateMsaa\":false}\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            preparePvrOutputPath(options.sceneSnapshotPath);
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_loader_texture_pvrtc\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\": " << frameIndex << ",\n"
                << "  \"renderSetPolicy\": \"required\",\n"
                << "  \"gpuWorkDslOnly\": true,\n"
                << "  \"sceneRenderSetCount\": 1,\n"
                << "  \"renderableObjectCount\": 8,\n"
                << "  \"entityCount\": 8,\n"
                << "  \"instanceCounts\": [1,1,1,1,1,1,1,1],\n"
                << "  \"scenePassCount\": 2,\n"
                << "  \"screenPassCount\": 1,\n"
                << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\",\"scenePass\":\"opaque-basic-and-environment\",\"entityOrdinal\":0},{\"sceneRoot\":\"scene\",\"scenePass\":\"transparent-double-sided-depth-disabled\",\"entityOrdinal\":0}],\n"
                << "  \"drawCommandCount\": 2,\n"
                << "  \"directDrawFallback\": false,\n"
                << "  \"usesRenderEntityID\": true,\n"
                << "  \"usesRenderEntityInstanceID\": true,\n"
                << "  \"sampleCount\": 1,\n"
                << "  \"msaaEnabled\": false,\n"
                << "  \"sceneRoots\": [{\n"
                << "    \"id\": \"scene\",\n"
                << "    \"renderSetCount\": 1,\n"
                << "    \"renderSetId\": \"scene\",\n"
                << "    \"renderSetType\": \"WebglLoaderTexturePvrtcSceneRenderSet\",\n"
                << "    \"renderableObjectCount\": 8,\n"
                << "    \"entityCount\": 8,\n"
                << "    \"entities\": [{\"entityId\":0,\"logicalRenderableId\":\"disturb-4bpp-rgb\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"disturb-4bpp-rgb-mips\",\"instanceCount\":1},{\"entityId\":2,\"logicalRenderableId\":\"disturb-2bpp-rgb\",\"instanceCount\":1},{\"entityId\":3,\"logicalRenderableId\":\"disturb-4bpp-rgb-v3\",\"instanceCount\":1},{\"entityId\":4,\"logicalRenderableId\":\"flare-4bpp-rgba\",\"instanceCount\":1},{\"entityId\":5,\"logicalRenderableId\":\"flare-2bpp-rgba\",\"instanceCount\":1},{\"entityId\":6,\"logicalRenderableId\":\"park-cube-4bpp\",\"instanceCount\":1},{\"entityId\":7,\"logicalRenderableId\":\"park-cube-2bpp-mips\",\"instanceCount\":1}],\n"
                << "    \"componentSchema\": [{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"decoded-pvr-map-or-cube-atlas-slot\"},{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"opaque-transparent-cube-and-depth-phase\"}],\n"
                << "    \"drawCommandCount\": 2,\n"
                << "    \"directDrawFallback\": false,\n"
                << "    \"scenePasses\": [{\"name\":\"opaque-basic-and-environment\",\"renderClass\":\"WebglLoaderTexturePvrtcOpaquePass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"transparent-double-sided-depth-disabled\",\"renderClass\":\"WebglLoaderTexturePvrtcTransparentPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
                << "  }]\n}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            preparePvrOutputPath(options.semanticSnapshotPath);
            std::ofstream output(options.semanticSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\": 1,\n"
                << "  \"caseId\": \"webgl_loader_texture_pvrtc\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\": " << frameIndex << ",\n"
                << "  \"kind\": \"loader-snapshot\",\n"
                << "  \"canonicalState\": \"eight-pvr-assets-two-four-bpp-rgb-rgba-two-cube-atlases\",\n"
                << "  \"result\": {\"assetCount\":8,\"baseWidth\":256,\"baseHeight\":256,\"singleFaceTextureCount\":6,\"cubeTextureCount\":2,\"twoBppTextureCount\":3,\"fourBppTextureCount\":5,\"authoredMipTextureCount\":2,\"renderableObjectCount\":0,\"sceneRootCount\":1,\"canonicalSceneSha256\":\"759b3781648b49758600945088b68f5358f9c38157d616745ef6fea1faeac140\"}\n}\n";
        }
        captureWritten = true;
    }

    void WebglLoaderTexturePvrtcRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
} // namespace GVM::ThreeSamples
