#include "WebglLoaderTextureDdsRuntimeAdapter.hpp"

#include "TexturedBoxSampleData.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <filesystem>
#include <fstream>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr uint32_t EntityCount = 13u;
        constexpr uint32_t AssetCount = 14u;

        constexpr const char *AssetPaths[AssetCount] = {
            "textures/compressed/disturb_dxt1_nomip.dds",
            "textures/compressed/disturb_dxt1_mip.dds",
            "textures/compressed/hepatica_dxt3_mip.dds",
            "textures/compressed/explosion_dxt5_mip.dds",
            "textures/compressed/disturb_argb_nomip.dds",
            "textures/compressed/disturb_argb_mip.dds",
            "textures/compressed/disturb_dx10_bc6h_signed_nomip.dds",
            "textures/compressed/disturb_dx10_bc6h_signed_mip.dds",
            "textures/compressed/disturb_dx10_bc6h_unsigned_nomip.dds",
            "textures/compressed/disturb_dx10_bc6h_unsigned_mip.dds",
            "textures/wave_normals_24bit_uncompressed.dds",
            "textures/compressed/Mountains.dds",
            "textures/compressed/Mountains_argb_mip.dds",
            "textures/compressed/Mountains_argb_nomip.dds",
        };

        /** Describes one frozen DDS asset, material phase, and Scene placement. */
        struct DdsEntityDefinition final
        {
            const char *fileName;
            uint32_t phase;
            uint32_t materialKind;
            uint32_t mapAsset;
            uint32_t cubeAsset;
            float x;
            float y;
        };

        constexpr DdsEntityDefinition EntityDefinitions[EntityCount] = {
            {"map1-environment1", 0u, 1u, 0u, 11u, -10.0f, -2.0f},
            {"map2", 0u, 0u, 1u, 0u, -6.0f, -2.0f},
            {"map3-alpha-mask", 1u, 0u, 2u, 0u, -6.0f, 2.0f},
            {"map4-additive", 2u, 0u, 3u, 0u, -10.0f, 2.0f},
            {"environment2", 0u, 2u, 0u, 12u, -2.0f, 2.0f},
            {"environment3", 0u, 2u, 0u, 13u, -2.0f, -2.0f},
            {"map5", 0u, 0u, 4u, 0u, 2.0f, -2.0f},
            {"map6", 0u, 0u, 5u, 0u, 2.0f, 2.0f},
            {"map7", 0u, 0u, 6u, 0u, 6.0f, -2.0f},
            {"map8", 0u, 0u, 7u, 0u, 6.0f, 2.0f},
            {"map9", 0u, 0u, 8u, 0u, 10.0f, -2.0f},
            {"map10", 0u, 0u, 9u, 0u, 10.0f, 2.0f},
            {"map11-alpha", 3u, 0u, 10u, 0u, -10.0f, -6.0f},
        };

        /** Validates the three locked DDS scenarios and common capture contract. */
        void validateDdsScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool loader = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 120u;
            if (options.caseId != "webgl_loader_texture_dds" ||
                (!initial && !loader && !animated) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                !options.inputReplayPath.empty() || options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "DDS adapter requires one locked 800x500 r185 scenario and explicit assets.");
            }
        }

        /** Builds the exact 24-vertex BoxGeometry union required by all entities. */
        void buildDdsBoxGeometry(
            eastl::vector<WebglLoaderTextureDdsHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            eastl::vector<TexturedBoxHostVertex> sourceVertices;
            buildTexturedBoxGeometry(2.0f, 2.0f, 2.0f, sourceVertices, indices);
            constexpr glm::vec3 FaceNormals[6u] = {
                {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
                {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
            vertices.clear();
            vertices.reserve(sourceVertices.size());
            for (uint32_t vertex = 0u; vertex < sourceVertices.size(); ++vertex)
            {
                const TexturedBoxHostVertex &source = sourceVertices[vertex];
                const glm::vec3 normal = FaceNormals[vertex / 4u];
                vertices.push_back({
                    .position = {source.position.x, source.position.y, source.position.z, 1.0f},
                    .normal = {normal, 0.0f},
                    .textureCoordinate = {
                        source.texCoord.x, source.texCoord.y, 0.0f, 0.0f},
                });
            }
        }

        /** Builds Three r185's default TorusGeometry indexed stream. */
        void buildDdsTorusGeometry(
            eastl::vector<WebglLoaderTextureDdsHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t RadialSegments = 12u;
            constexpr uint32_t TubularSegments = 48u;
            constexpr float Radius = 1.0f;
            constexpr float Tube = 0.4f;
            vertices.clear();
            indices.clear();
            for (uint32_t radial = 0u; radial <= RadialSegments; ++radial)
            {
                const float v = float(radial) / float(RadialSegments) *
                    glm::two_pi<float>();
                for (uint32_t tubular = 0u;
                     tubular <= TubularSegments;
                     ++tubular)
                {
                    const float u = float(tubular) /
                        float(TubularSegments) * glm::two_pi<float>();
                    const glm::vec3 center(
                        Radius * std::cos(u), Radius * std::sin(u), 0.0f);
                    const glm::vec3 position(
                        (Radius + Tube * std::cos(v)) * std::cos(u),
                        (Radius + Tube * std::cos(v)) * std::sin(u),
                        Tube * std::sin(v));
                    vertices.push_back({
                        {position, 1.0f},
                        {glm::normalize(position - center), 0.0f},
                        {1.0f - float(tubular) / float(TubularSegments),
                         1.0f - float(radial) / float(RadialSegments),
                         0.0f, 0.0f},
                    });
                }
            }
            for (uint32_t radial = 1u;
                 radial <= RadialSegments;
                 ++radial)
            {
                for (uint32_t tubular = 1u;
                     tubular <= TubularSegments;
                     ++tubular)
                {
                    const uint32_t a =
                        (TubularSegments + 1u) * radial + tubular - 1u;
                    const uint32_t b =
                        (TubularSegments + 1u) * (radial - 1u) + tubular - 1u;
                    const uint32_t c =
                        (TubularSegments + 1u) * (radial - 1u) + tubular;
                    const uint32_t d =
                        (TubularSegments + 1u) * radial + tubular;
                    indices.insert(indices.end(), {a, b, d, b, c, d});
                }
            }
            if (vertices.size() != 637u || indices.size() != 3456u)
            {
                throw std::runtime_error(
                    "DDS TorusGeometry topology diverged from r185.");
            }
        }

        /** Packs one decoded DDS face into an explicit mip upload payload. */
        void packDdsFace(
            const DdsRgba8Face &face,
            eastl::vector<uint8_t> &bytes,
            eastl::vector<uint64_t> &offsets)
        {
            bytes.clear();
            offsets.clear();
            for (const RgbaImageData &mip : face.mipLevels)
            {
                offsets.push_back(bytes.size());
                bytes.insert(
                    bytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
        }

        /** Returns whether one frozen DDS asset uses sRGB sampling. */
        bool isDdsSrgbAsset(uint32_t assetIndex)
        {
            return assetIndex <= 5u || assetIndex >= 11u;
        }

        /** Appends one typed RenderSet component allocation. */
        void appendDdsBuffer(
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

        /** Creates parent folders for an explicitly requested artifact. */
        void prepareDdsOutputPath(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path output(path.c_str());
            if (!output.parent_path().empty())
                std::filesystem::create_directories(output.parent_path());
        }
    } // namespace

    void WebglLoaderTextureDdsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateDdsScenario(options);
        device = inDevice;
        buildDdsBoxGeometry(boxVertices, boxIndices);
        buildDdsTorusGeometry(torusVertices, torusIndices);
        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        for (uint32_t asset = 0u; asset < AssetCount; ++asset)
        {
            textures[asset] = decodeDdsRgba8(
                assetRoot / AssetPaths[asset]);
        }
        const double timeSeconds = 1700000000.0 + double(options.targetFrame) / 60.0;
        const glm::mat4 rotation = makeThreeEulerXyRotation(-timeSeconds, timeSeconds);
        const glm::mat4 environmentRotation =
            makeThreeEulerXyRotation(timeSeconds, timeSeconds);
        glm::mat4 view(1.0f);
        view[3u][1u] = -2.0f;
        view[3u][2u] = -16.0f;
        const glm::mat4 projection = makeThreePerspectiveProjection(
            options.width, options.height, 50.0, 1.0, 100.0);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("DDS adapter could not create its Scene Set encoder.");

        for (uint32_t entity = 0u; entity < EntityCount; ++entity)
        {
            const DdsEntityDefinition &definition =
                EntityDefinitions[entity];
            uint32_t textureSlotCount = 1u;
            if (definition.materialKind == 1u)
            {
                packDdsFace(
                    textures[definition.mapAsset].faces[0u],
                    textureBytes[entity][0u], mipOffsets[entity][0u]);
                for (uint32_t face = 0u; face < 6u; ++face)
                {
                    packDdsFace(
                        textures[definition.cubeAsset].faces[face],
                        textureBytes[entity][face + 1u],
                        mipOffsets[entity][face + 1u]);
                }
                textureSlotCount = 7u;
            }
            else if (definition.materialKind == 2u)
            {
                for (uint32_t face = 0u; face < 6u; ++face)
                {
                    packDdsFace(
                        textures[definition.cubeAsset].faces[face],
                        textureBytes[entity][face],
                        mipOffsets[entity][face]);
                }
                textureSlotCount = 6u;
            }
            else
            {
                packDdsFace(
                    textures[definition.mapAsset].faces[0u],
                    textureBytes[entity][0u], mipOffsets[entity][0u]);
            }
            glm::mat4 model(1.0f);
            model[3u][0u] = definition.x;
            model[3u][1u] = -definition.y;
            model *= rotation;
            glm::mat4 environmentModel(1.0f);
            environmentModel[3u][0u] = definition.x;
            environmentModel[3u][1u] = definition.y;
            environmentModel *= environmentRotation;
            const glm::mat4 modelView = view * model;
            objects[entity] = {
                .modelViewProjection = projection * modelView,
                .modelView = modelView,
                .environmentModel = environmentModel,
                .phaseAndFlags = {definition.phase, 0u, 0u, 0u},
            };
            instances[entity].reserved = glm::vec4(0.0f);
            materials[entity].ambientPointRoughnessOpacity =
                glm::vec4(0.02f, 2.0f, 1.0f, 1.0f);
            renderFlags[entity] = glm::uvec4(
                definition.phase, definition.materialKind, 0u, 0u);

            const auto &entityVertices = entity == 0u
                ? torusVertices : boxVertices;
            const auto &entityIndices = entity == 0u
                ? torusIndices : boxIndices;

            const eastl::string prefix =
                "WebglLoaderTextureDdsEntity" + eastl::to_string(entity);
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entityVertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entityIndices.size());
            allocation.instanceCount = 1u;
            appendDdsBuffer(allocation, WebglLoaderTextureDdsSceneRenderSetComponents::vertices,
                prefix + "Vertices", entityVertices.data(), entityVertices.size() * sizeof(entityVertices[0u]));
            appendDdsBuffer(allocation, WebglLoaderTextureDdsSceneRenderSetComponents::indices,
                prefix + "Indices", entityIndices.data(), entityIndices.size() * sizeof(entityIndices[0u]));
            appendDdsBuffer(allocation, WebglLoaderTextureDdsSceneRenderSetComponents::objects,
                prefix + "Object", &objects[entity], sizeof(objects[entity]));
            appendDdsBuffer(allocation, WebglLoaderTextureDdsSceneRenderSetComponents::instances,
                prefix + "Instance", &instances[entity], sizeof(instances[entity]));
            appendDdsBuffer(allocation, WebglLoaderTextureDdsSceneRenderSetComponents::materials,
                prefix + "Material", &materials[entity], sizeof(materials[entity]));
            appendDdsBuffer(allocation, WebglLoaderTextureDdsSceneRenderSetComponents::renderFlags,
                prefix + "RenderFlags", &renderFlags[entity], sizeof(renderFlags[entity]));
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebglLoaderTextureDdsSceneRenderSetComponents::textures;
            for (uint32_t slot = 0u; slot < textureSlotCount; ++slot)
            {
                uint32_t asset = definition.mapAsset;
                uint32_t face = 0u;
                if (definition.materialKind == 1u && slot > 0u)
                {
                    asset = definition.cubeAsset;
                    face = slot - 1u;
                }
                else if (definition.materialKind == 2u)
                {
                    asset = definition.cubeAsset;
                    face = slot;
                }
                const RgbaImageData &base =
                    textures[asset].faces[face].mipLevels.front();
                textureComponent.textures.push_back({
                    .textureName = prefix + "Texture" +
                        eastl::to_string(slot),
                    .format = isDdsSrgbAsset(asset)
                        ? GVM::RHI::TextureFormat::RGBA8UnormSrgb
                        : GVM::RHI::TextureFormat::RGBA8Unorm,
                    .width = base.width,
                    .height = base.height,
                    .data = textureBytes[entity][slot].data(),
                    .dataStorageBytes = textureBytes[entity][slot].size(),
                    .mipmapOffsetBytes = mipOffsets[entity][slot],
                });
            }
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderTextureDdsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderTextureDdsRuntimeAdapter::afterFrame(
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
            prepareDdsOutputPath(options.captureRgbaPath);
            std::ofstream output(options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write DDS RGBA capture.");
        }
        if (!options.captureMetadataPath.empty())
        {
            prepareDdsOutputPath(options.captureMetadataPath);
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_loader_texture_dds\",\n"
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
            prepareDdsOutputPath(options.sceneSnapshotPath);
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_loader_texture_dds\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\": " << frameIndex << ",\n"
                << "  \"renderSetPolicy\": \"required\",\n"
                << "  \"gpuWorkDslOnly\": true,\n"
                << "  \"sceneRenderSetCount\": 1,\n"
                << "  \"renderableObjectCount\": 13,\n"
                << "  \"entityCount\": 13,\n"
                << "  \"instanceCounts\": [1,1,1,1,1,1,1,1,1,1,1,1,1],\n"
                << "  \"scenePassCount\": 4,\n"
                << "  \"screenPassCount\": 1,\n"
                << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\",\"scenePass\":\"opaque-basic-and-environment\",\"entityOrdinal\":0},{\"sceneRoot\":\"scene\",\"scenePass\":\"alpha-mask-double-sided\",\"entityOrdinal\":0},{\"sceneRoot\":\"scene\",\"scenePass\":\"transparent-additive-double-sided\",\"entityOrdinal\":0},{\"sceneRoot\":\"scene\",\"scenePass\":\"transparent-alpha\",\"entityOrdinal\":0}],\n"
                << "  \"drawCommandCount\": 4,\n"
                << "  \"directDrawFallback\": false,\n"
                << "  \"usesRenderEntityID\": true,\n"
                << "  \"usesRenderEntityInstanceID\": true,\n"
                << "  \"sampleCount\": 1,\n"
                << "  \"msaaEnabled\": false,\n"
                << "  \"sceneRoots\": [{\n"
                << "    \"id\": \"scene\",\n"
                << "    \"renderSetCount\": 1,\n"
                << "    \"renderSetId\": \"scene\",\n"
                << "    \"renderSetType\": \"WebglLoaderTextureDdsSceneRenderSet\",\n"
                << "    \"renderableObjectCount\": 13,\n"
                << "    \"entityCount\": 13,\n"
                << "    \"entities\": [{\"entityId\":0,\"logicalRenderableId\":\"map1-environment1\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"map2\",\"instanceCount\":1},{\"entityId\":2,\"logicalRenderableId\":\"map3-alpha-mask\",\"instanceCount\":1},{\"entityId\":3,\"logicalRenderableId\":\"map4-additive\",\"instanceCount\":1},{\"entityId\":4,\"logicalRenderableId\":\"environment2\",\"instanceCount\":1},{\"entityId\":5,\"logicalRenderableId\":\"environment3\",\"instanceCount\":1},{\"entityId\":6,\"logicalRenderableId\":\"map5\",\"instanceCount\":1},{\"entityId\":7,\"logicalRenderableId\":\"map6\",\"instanceCount\":1},{\"entityId\":8,\"logicalRenderableId\":\"map7\",\"instanceCount\":1},{\"entityId\":9,\"logicalRenderableId\":\"map8\",\"instanceCount\":1},{\"entityId\":10,\"logicalRenderableId\":\"map9\",\"instanceCount\":1},{\"entityId\":11,\"logicalRenderableId\":\"map10\",\"instanceCount\":1},{\"entityId\":12,\"logicalRenderableId\":\"map11-alpha\",\"instanceCount\":1}],\n"
                << "    \"componentSchema\": [{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"decoded-dds-map-and-cube-atlas-slots\"},{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"opaque-mask-additive-alpha-and-cube-phase\"}],\n"
                << "    \"drawCommandCount\": 4,\n"
                << "    \"directDrawFallback\": false,\n"
                << "    \"scenePasses\": [{\"name\":\"opaque-basic-and-environment\",\"renderClass\":\"WebglLoaderTextureDdsOpaquePass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"alpha-mask-double-sided\",\"renderClass\":\"WebglLoaderTextureDdsAlphaMaskPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"transparent-additive-double-sided\",\"renderClass\":\"WebglLoaderTextureDdsAdditivePass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"transparent-alpha\",\"renderClass\":\"WebglLoaderTextureDdsAlphaPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
                << "  }]\n}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            prepareDdsOutputPath(options.semanticSnapshotPath);
            std::ofstream output(options.semanticSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\": 1,\n"
                << "  \"caseId\": \"webgl_loader_texture_dds\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\": " << frameIndex << ",\n"
                << "  \"kind\": \"loader-snapshot\",\n"
                << "  \"canonicalState\": \"fourteen-dds-assets-thirteen-meshes-three-cube-atlases-bc6h-signed-unsigned\",\n"
                << "  \"result\": {\"assetCount\":14,\"bc1TextureCount\":3,\"bc2TextureCount\":1,\"bc3TextureCount\":1,\"bc6hTextureCount\":4,\"uncompressedTextureCount\":5,\"cubeTextureCount\":3,\"renderableObjectCount\":0,\"sceneRootCount\":1,\"canonicalSceneSha256\":\"b18fd454e94364197859c82d73e4191fe4ce4fec0b4c4d071ead96672f75bfe6\"}\n}\n";
        }
        captureWritten = true;
    }

    void WebglLoaderTextureDdsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
} // namespace GVM::ThreeSamples
