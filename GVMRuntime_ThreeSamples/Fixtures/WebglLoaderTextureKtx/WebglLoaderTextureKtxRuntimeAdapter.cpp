#include "WebglLoaderTextureKtxRuntimeAdapter.hpp"

#include "TexturedBoxSampleData.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr uint32_t EntityCount = 9u;

        /** Describes one frozen KTX asset, material phase, and Scene placement. */
        struct KtxEntityDefinition final
        {
            const char *fileName;
            uint32_t phase;
            float x;
            float y;
        };

        constexpr KtxEntityDefinition EntityDefinitions[EntityCount] = {
            {"disturb_PVR2bpp.ktx", 0u, -450.0f, 150.0f},
            {"lensflare_PVR4bpp.ktx", 2u, -150.0f, 150.0f},
            {"disturb_BC1.ktx", 0u, 150.0f, 150.0f},
            {"lensflare_BC3.ktx", 2u, 450.0f, 150.0f},
            {"normal.bc5.ktx", 1u, -600.0f, -150.0f},
            {"disturb_ETC1.ktx", 0u, -300.0f, -150.0f},
            {"normal.eac_rg.ktx", 1u, 0.0f, -150.0f},
            {"disturb_ASTC4x4.ktx", 0u, 300.0f, -150.0f},
            {"lensflare_ASTC8x8.ktx", 2u, 600.0f, -150.0f},
        };

        /** Validates the three locked KTX scenarios and common capture contract. */
        void validateKtxScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool loader = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool animated = options.scenarioId == "animated" && options.targetFrame == 120u;
            if (options.caseId != "webgl_loader_texture_ktx" ||
                (!initial && !loader && !animated) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                !options.inputReplayPath.empty() || options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "KTX adapter requires one locked 800x500 r185 scenario and explicit assets.");
            }
        }

        /** Builds the exact 24-vertex BoxGeometry union required by all entities. */
        void buildKtxBoxGeometry(
            eastl::vector<WebglLoaderTextureKtxHostVertex> &vertices,
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
                const glm::vec3 normal = FaceNormals[vertex / 4u];
                vertices.push_back({
                    .position = {source.position.x, source.position.y, source.position.z, 1.0f},
                    .normal = {normal, 0.0f},
                    .textureCoordinate = {
                        source.texCoord.x, source.texCoord.y, 0.0f, 0.0f},
                });
            }
        }

        /** Appends one typed RenderSet component allocation. */
        void appendKtxBuffer(
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
        void prepareKtxOutputPath(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path output(path.c_str());
            if (!output.parent_path().empty())
                std::filesystem::create_directories(output.parent_path());
        }
    } // namespace

    void WebglLoaderTextureKtxRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateKtxScenario(options);
        device = inDevice;
        buildKtxBoxGeometry(vertices, indices);
        const std::filesystem::path textureRoot =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "compressed";
        const double timeSeconds = 1700000000.0 + double(options.targetFrame) / 60.0;
        const glm::mat4 rotation = makeThreeEulerXyRotation(-timeSeconds, timeSeconds);
        glm::mat4 view(1.0f);
        view[3u][2u] = -1000.0f;
        const glm::mat4 projection = makeThreePerspectiveProjection(
            options.width, options.height, 50.0, 1.0, 2000.0);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("KTX adapter could not create its Scene Set encoder.");

        for (uint32_t entity = 0u; entity < EntityCount; ++entity)
        {
            textures[entity] = decodeKtx1Rgba8(
                textureRoot / EntityDefinitions[entity].fileName);
            textureBytes[entity].clear();
            mipOffsets[entity].clear();
            for (const RgbaImageData &mip : textures[entity].mipLevels)
            {
                mipOffsets[entity].push_back(textureBytes[entity].size());
                textureBytes[entity].insert(
                    textureBytes[entity].end(), mip.pixels.begin(), mip.pixels.end());
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
            materials[entity].ambientPointRoughnessOpacity =
                glm::vec4(0.02f, 2.0f, 1.0f, 1.0f);
            renderFlags[entity] = glm::uvec4(
                EntityDefinitions[entity].phase, 1u, 0u, 0u);

            const eastl::string prefix =
                "WebglLoaderTextureKtxEntity" + eastl::to_string(entity);
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            appendKtxBuffer(allocation, WebglLoaderTextureKtxSceneRenderSetComponents::vertices,
                prefix + "Vertices", vertices.data(), vertices.size() * sizeof(vertices[0u]));
            appendKtxBuffer(allocation, WebglLoaderTextureKtxSceneRenderSetComponents::indices,
                prefix + "Indices", indices.data(), indices.size() * sizeof(indices[0u]));
            appendKtxBuffer(allocation, WebglLoaderTextureKtxSceneRenderSetComponents::objects,
                prefix + "Object", &objects[entity], sizeof(objects[entity]));
            appendKtxBuffer(allocation, WebglLoaderTextureKtxSceneRenderSetComponents::instances,
                prefix + "Instance", &instances[entity], sizeof(instances[entity]));
            appendKtxBuffer(allocation, WebglLoaderTextureKtxSceneRenderSetComponents::materials,
                prefix + "Material", &materials[entity], sizeof(materials[entity]));
            appendKtxBuffer(allocation, WebglLoaderTextureKtxSceneRenderSetComponents::renderFlags,
                prefix + "RenderFlags", &renderFlags[entity], sizeof(renderFlags[entity]));
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebglLoaderTextureKtxSceneRenderSetComponents::textures;
            const RgbaImageData &base = textures[entity].mipLevels.front();
            textureComponent.textures.push_back({
                .textureName = prefix + "Texture",
                .format = textures[entity].colorData
                    ? GVM::RHI::TextureFormat::RGBA8UnormSrgb
                    : GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = base.width,
                .height = base.height,
                .data = textureBytes[entity].data(),
                .dataStorageBytes = textureBytes[entity].size(),
                .mipmapOffsetBytes = mipOffsets[entity],
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderTextureKtxRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderTextureKtxRuntimeAdapter::afterFrame(
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
            prepareKtxOutputPath(options.captureRgbaPath);
            std::ofstream output(options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write KTX RGBA capture.");
        }
        if (!options.captureMetadataPath.empty())
        {
            prepareKtxOutputPath(options.captureMetadataPath);
            std::ofstream output(options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_loader_texture_ktx\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\": " << frameIndex << ",\n"
                << "  \"randomSeed\": " << options.randomSeed << ",\n"
                << "  \"width\": " << width << ", \"height\": " << height << ",\n"
                << "  \"rowStrideBytes\": " << width * 4u << ",\n"
                << "  \"byteCount\": " << rgba.size() << ",\n"
                << "  \"format\": \"rgba8unorm\",\n"
                << "  \"sampleCount\": 1\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareKtxOutputPath(options.sceneSnapshotPath);
            std::ofstream output(options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"caseId\": \"webgl_loader_texture_ktx\",\n"
                << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                << "  \"sceneRenderSetCount\": 1,\n"
                << "  \"entityCount\": 9,\n"
                << "  \"instanceCounts\": [1,1,1,1,1,1,1,1,1],\n"
                << "  \"scenePassCount\": 3,\n"
                << "  \"sampleCount\": 1\n}\n";
        }
        captureWritten = true;
    }

    void WebglLoaderTextureKtxRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
} // namespace GVM::ThreeSamples
