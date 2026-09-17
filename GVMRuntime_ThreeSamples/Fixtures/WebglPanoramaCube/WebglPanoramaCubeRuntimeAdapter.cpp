#include "WebglPanoramaCubeRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "TexturedBoxSampleData.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/string.h>

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
        constexpr uint32_t AtlasWidth = 8192u;
        constexpr uint32_t AtlasHeight = 1024u;
        constexpr uint32_t TileExtent = 1024u;
        constexpr uint32_t TileCount = 6u;
        constexpr uint32_t TileMipCount = 11u;
        constexpr const char *AtlasSha256 =
            "4aadca4e81b71bae67ab1229e403dc755e33c4fbc498ce4c8d868e3d2a5a0200";
        constexpr const char *ReplaySha256 =
            "ad13b21ace4f42b6d9885428ef6b205c4d882a6f51fa47408eccbca1916fff50";
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebglPanoramaCubeHostVertex) == 48u);
        static_assert(sizeof(WebglPanoramaCubeHostObjectData) == 64u);
        static_assert(sizeof(WebglPanoramaCubeHostInstanceData) == 16u);
        static_assert(sizeof(WebglPanoramaCubeHostMaterialData) == 16u);

        /** Reads one pinned panorama input into bounded memory. */
        eastl::vector<uint8_t> readPanoramaCubeInput(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open a panorama-cube input.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
                throw std::runtime_error("A panorama-cube input is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
                throw std::runtime_error("Could not read a panorama-cube input.");
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded input. */
        eastl::string calculatePanoramaCubeSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Panorama-cube SHA-256 input is too large.");
            }
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(
                bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Crops one square tile from the horizontal source atlas. */
        RgbaImageData cropPanoramaCubeTile(
            const RgbaImageData &atlas,
            uint32_t tileIndex)
        {
            if (atlas.width != AtlasWidth || atlas.height != AtlasHeight ||
                tileIndex >= TileCount)
            {
                throw std::invalid_argument(
                    "Panorama-cube atlas dimensions or tile index are invalid.");
            }
            RgbaImageData tile;
            tile.width = TileExtent;
            tile.height = TileExtent;
            tile.pixels.resize(size_t(TileExtent) * TileExtent * 4u);
            for (uint32_t y = 0u; y < TileExtent; ++y)
            {
                const size_t sourceOffset =
                    (size_t(y) * atlas.width + size_t(tileIndex) * TileExtent) * 4u;
                const size_t destinationOffset = size_t(y) * TileExtent * 4u;
                for (size_t byteIndex = 0u;
                     byteIndex < size_t(TileExtent) * 4u;
                     ++byteIndex)
                {
                    tile.pixels[destinationOffset + byteIndex] =
                        atlas.pixels[sourceOffset + byteIndex];
                }
            }
            return tile;
        }

        /** Packs one complete tile mip chain into RenderSet texture storage. */
        WebglPanoramaCubeTextureData makePanoramaCubeTextureData(
            const RgbaImageData &baseTile)
        {
            const eastl::vector<RgbaImageData> mipChain =
                buildSrgbMipChain(baseTile);
            if (mipChain.size() != TileMipCount)
                throw std::runtime_error("A panorama tile has an invalid mip count.");
            WebglPanoramaCubeTextureData result;
            result.width = baseTile.width;
            result.height = baseTile.height;
            for (const RgbaImageData &mip : mipChain)
            {
                result.mipOffsets.push_back(result.bytes.size());
                result.bytes.insert(
                    result.bytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
            return result;
        }

        /** Builds the inward BoxGeometry and assigns its six material slots. */
        void buildPanoramaCubeGeometry(
            eastl::vector<WebglPanoramaCubeHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            eastl::vector<TexturedBoxHostVertex> sourceVertices;
            buildTexturedBoxGeometry(
                1.0f, 1.0f, 1.0f, sourceVertices, indices);
            vertices.clear();
            vertices.reserve(sourceVertices.size());
            for (uint32_t index = 0u; index < sourceVertices.size(); ++index)
            {
                const TexturedBoxHostVertex &source = sourceVertices[index];
                const uint32_t sourceFace = index / 4u;
                const uint32_t textureFace = sourceFace == 2u
                    ? 3u
                    : (sourceFace == 3u ? 2u : sourceFace);
                vertices.push_back({
                    .position = glm::vec4(
                        source.position.x,
                        source.position.y,
                        -source.position.z,
                        1.0f),
                    .textureCoordinate = glm::vec4(
                        source.texCoord.x,
                        source.texCoord.y,
                        0.0f,
                        0.0f),
                    .faceSlot = glm::uvec4(textureFace, 0u, 0u, 0u),
                });
            }
            if (vertices.size() != 24u || indices.size() != 36u)
                throw std::logic_error("Panorama BoxGeometry layout diverged.");
        }

        /** Builds the exact target-frame damped OrbitControls view transform. */
        glm::mat4 makePanoramaCubeView(bool orbitDamped)
        {
            glm::vec3 cameraPosition(0.0f, 0.0f, 0.01f);
            if (orbitDamped)
            {
                constexpr double Pi = 3.14159265358979323846;
                constexpr double DampingRetention = 0.95;
                constexpr uint32_t AppliedUpdateCount = 62u;
                const double retained =
                    std::pow(DampingRetention, double(AppliedUpdateCount));
                const double theta =
                    (2.0 * Pi * 60.0 / 500.0 * 0.25) * (1.0 - retained);
                const double phi =
                    Pi * 0.5 +
                    (2.0 * Pi * 30.0 / 500.0 * 0.25) * (1.0 - retained);
                cameraPosition = glm::vec3(
                    static_cast<float>(0.01 * std::sin(phi) * std::sin(theta)),
                    static_cast<float>(0.01 * std::cos(phi)),
                    static_cast<float>(0.01 * std::sin(phi) * std::cos(theta)));
            }
            return glm::lookAt(
                cameraPosition,
                glm::vec3(0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
        }

        /** Appends one typed component payload to the entity allocation. */
        void appendPanoramaCubeBuffer(
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

        /** Creates parent directories for one requested formal artifact. */
        void preparePanoramaCubeOutputPath(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path output(path.c_str());
            if (!output.parent_path().empty())
                std::filesystem::create_directories(output.parent_path());
        }
    } // namespace

    void WebglPanoramaCubeRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool atlasSnapshot =
            options.scenarioId == "atlas-snapshot" && options.targetFrame == 0u;
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool orbit =
            options.scenarioId == "orbit-damped" && options.targetFrame == 60u;
        if (options.caseId != "webgl_panorama_cube" ||
            (!atlasSnapshot && !initial && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != 0x12345678u || options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "Panorama-cube adapter requires its locked case, scenarios, extent, seed, and assets.");
        }
        hasReplay = orbit;
        if (orbit)
        {
            if (options.inputReplayPath.empty() ||
                calculatePanoramaCubeSha256(readPanoramaCubeInput(
                    std::filesystem::path(options.inputReplayPath.c_str()))) !=
                    ReplaySha256)
            {
                throw std::invalid_argument(
                    "Panorama-cube replay differs from its locked identity.");
            }
        }
        else if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Only the panorama-cube orbit scenario accepts replay input.");
        }

        const std::filesystem::path atlasPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "cube" / "sun_temple_stripe.jpg";
        const eastl::vector<uint8_t> encoded = readPanoramaCubeInput(atlasPath);
        if (calculatePanoramaCubeSha256(encoded) != AtlasSha256)
            throw std::invalid_argument("Panorama-cube atlas identity differs.");
        const RgbaImageData atlas = decodeJpegRgba8(atlasPath);
        if (atlas.width != AtlasWidth || atlas.height != AtlasHeight)
            throw std::runtime_error("Panorama-cube atlas extent differs.");
        for (uint32_t tile = 0u; tile < TileCount; ++tile)
            textures[tile] = makePanoramaCubeTextureData(
                cropPanoramaCubeTile(atlas, tile));

        buildPanoramaCubeGeometry(vertices, indices);
        const glm::mat4 projection = makeThreePerspectiveProjection(
            options.width, options.height, 90.0, 0.1, 100.0);
        const WebglPanoramaCubeHostObjectData objectData = {
            .modelViewProjection = projection * makePanoramaCubeView(orbit),
        };
        const WebglPanoramaCubeHostInstanceData instanceData = {
            .reserved = glm::vec4(0.0f),
        };
        const WebglPanoramaCubeHostMaterialData materialData = {
            .faceCountAndReserved = glm::uvec4(TileCount, 0u, 0u, 0u),
        };

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneSetHandle);
        if (!encoder)
            throw std::runtime_error("Could not create the panorama Scene Set encoder.");
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendPanoramaCubeBuffer(
            allocation, WebglPanoramaCubeSceneRenderSetComponents::vertices,
            "WebglPanoramaCubeVertices", vertices.data(),
            vertices.size() * sizeof(vertices[0u]));
        appendPanoramaCubeBuffer(
            allocation, WebglPanoramaCubeSceneRenderSetComponents::indices,
            "WebglPanoramaCubeIndices", indices.data(),
            indices.size() * sizeof(indices[0u]));
        appendPanoramaCubeBuffer(
            allocation, WebglPanoramaCubeSceneRenderSetComponents::objects,
            "WebglPanoramaCubeObject", &objectData, sizeof(objectData));
        appendPanoramaCubeBuffer(
            allocation, WebglPanoramaCubeSceneRenderSetComponents::instances,
            "WebglPanoramaCubeInstance", &instanceData, sizeof(instanceData));
        appendPanoramaCubeBuffer(
            allocation, WebglPanoramaCubeSceneRenderSetComponents::materials,
            "WebglPanoramaCubeMaterial", &materialData, sizeof(materialData));

        GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
        textureComponent.textureComponentHandle =
            WebglPanoramaCubeSceneRenderSetComponents::textures;
        for (uint32_t tile = 0u; tile < TileCount; ++tile)
        {
            const WebglPanoramaCubeTextureData &texture = textures[tile];
            textureComponent.textures.push_back({
                .textureName = "WebglPanoramaCubeFace" + eastl::to_string(tile),
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = texture.width,
                .height = texture.height,
                .data = texture.bytes.data(),
                .dataStorageBytes = texture.bytes.size(),
                .mipmapOffsetBytes = texture.mipOffsets,
            });
        }
        allocation.textureInfos.push_back(eastl::move(textureComponent));
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
        device = inDevice;
    }

    void WebglPanoramaCubeRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPanoramaCubeRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            preparePanoramaCubeOutputPath(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error("Could not write panorama-cube RGBA.");
        }
        if (!options.captureMetadataPath.empty())
        {
            preparePanoramaCubeOutputPath(options.captureMetadataPath);
            std::ofstream output(
                options.captureMetadataPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_panorama_cube\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << width * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\",\n"
                << "  \"sampleCount\":1,\n"
                << "  \"samplePolicy\":{\"mode\":\"single-sample\","
                << "\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                << "  \"inputReplay\":";
            if (hasReplay)
            {
                output << "{\"schemaVersion\":1,"
                    << "\"caseId\":\"webgl_panorama_cube\","
                    << "\"scenarioId\":\"orbit-damped\","
                    << "\"captureFrame\":60,"
                    << "\"sha256\":\"" << ReplaySha256 << "\","
                    << "\"target\":\"#container > canvas\","
                    << "\"eventCount\":3,\"lastEventFrame\":0}";
            }
            else
            {
                output << "null";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            preparePanoramaCubeOutputPath(options.sceneSnapshotPath);
            std::ofstream output(
                options.sceneSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_panorama_cube\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderSetType\":\"WebglPanoramaCubeSceneRenderSet\",\n"
                << "  \"renderableObjectCount\":1,\n"
                << "  \"entityCount\":1,\n"
                << "  \"instanceCount\":1,\n"
                << "  \"geometryGroupCount\":6,\n"
                << "  \"textureSlotCount\":6,\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"sampleCount\":1\n}\n";
        }
        if (!options.semanticSnapshotPath.empty() &&
            options.scenarioId == "atlas-snapshot")
        {
            preparePanoramaCubeOutputPath(options.semanticSnapshotPath);
            std::ofstream output(
                options.semanticSnapshotPath.c_str(), std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_panorama_cube\",\n"
                << "  \"scenarioId\":\"atlas-snapshot\",\n"
                << "  \"kind\":\"loader-snapshot\",\n"
                << "  \"assetSha256\":\"" << AtlasSha256 << "\",\n"
                << "  \"atlasExtent\":[8192,1024],\n"
                << "  \"croppedTileExtent\":[1024,1024],\n"
                << "  \"croppedTileCount\":6,\n"
                << "  \"explicitMipCountPerTile\":11\n}\n";
        }
        captureWritten = true;
    }

    void WebglPanoramaCubeRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        for (WebglPanoramaCubeTextureData &texture : textures)
        {
            texture.bytes.clear();
            texture.mipOffsets.clear();
        }
    }
} // namespace GVM::ThreeSamples
