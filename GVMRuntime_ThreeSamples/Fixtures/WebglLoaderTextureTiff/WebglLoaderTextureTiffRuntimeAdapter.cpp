#include "WebglLoaderTextureTiffRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/string.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr eastl::array<const char *, 3u> TiffNames = {
            "crate_uncompressed.tif", "crate_lzw.tif", "crate_jpeg.tif"};
        constexpr eastl::array<const char *, 3u> TiffHashes = {
            "d2d64f206234f4148d7716abe8a14d47db5ecd569317e2f010eac1cf25ca2445",
            "d9ea5311be5df454647a6ee1f96f0bb6909c2132d7d35d3c1ab8ff93cea5cf74",
            "c8a9e0a848d6ce6ec633d3e7a12097778130e3d95ab86056c9b458e674e9b2c4"};
        constexpr eastl::array<uint32_t, 3u> TiffExtents = {256u, 512u, 1024u};
        constexpr eastl::array<float, 3u> PlaneX = {-1.5f, 0.0f, 1.5f};
        constexpr eastl::array<const char *, 3u> PlaneNames = {
            "WebglLoaderTextureTiffUncompressed",
            "WebglLoaderTextureTiffLzw",
            "WebglLoaderTextureTiffJpeg"};

        /** Creates parent directories for one requested TIFF artifact. */
        void prepareTiffOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one deterministic TIFF text artifact. */
        void writeTiffText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareTiffOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error("Could not write a TIFF sample text artifact.");
            }
        }

        /** Reads and hashes one bounded TIFF asset without changing its decoded payload. */
        eastl::string calculateTiffSha256(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open one locked TIFF asset.");
            }
            const std::streamoff size = input.tellg();
            if (size <= 0 || uint64_t(size) > uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error("One locked TIFF asset has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            if (!input)
            {
                throw std::runtime_error("Could not read one complete TIFF asset.");
            }
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            constexpr char Hex[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(Hex[value >> 4u]);
                result.push_back(Hex[value & 15u]);
            }
            return result;
        }

        /** Appends one typed buffer payload to a RenderSet entity allocation. */
        void appendTiffBuffer(GVM::Core::RenderSetAllocInfo &allocation, GVM::Core::RenderComponentHandle component, const char *name, const void *value, uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }
    } // namespace

    void WebglLoaderTextureTiffRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool validScenario =
            (options.scenarioId == "initial-all-loaded" ||
             options.scenarioId == "canonical-loader") &&
            options.targetFrame == 0u;
        if (options.caseId != "webgl_loader_texture_tiff" || !validScenario ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
        {
            throw std::invalid_argument("TIFF adapter requires one locked r185 manifest scenario.");
        }
        device = inDevice;
        vertices = {
            {{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
            {{ 0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}},
            {{ 0.5f,  0.5f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}},
            {{-0.5f,  0.5f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
        };
        indices = {0u, 1u, 2u, 0u, 2u, 3u};
        const std::filesystem::path tiffRoot =
            std::filesystem::path(options.assetRoot.c_str()) / "textures" / "tiff";
        for (uint32_t textureIndex = 0u; textureIndex < TiffNames.size(); ++textureIndex)
        {
            const std::filesystem::path texturePath = tiffRoot / TiffNames[textureIndex];
            if (calculateTiffSha256(texturePath) != TiffHashes[textureIndex])
            {
                throw std::runtime_error("One TIFF asset differs from the locked r185 input.");
            }
            const RgbaImageData decoded = decodeTiffRgba8(texturePath);
            if (decoded.width != TiffExtents[textureIndex] ||
                decoded.height != TiffExtents[textureIndex])
            {
                throw std::runtime_error("One decoded TIFF extent differs from r185.");
            }
            textureWidths[textureIndex] = decoded.width;
            textureHeights[textureIndex] = decoded.height;
            mipOffsets[textureIndex].push_back(0u);
            textureBytes[textureIndex] = decoded.pixels;
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("TIFF adapter could not create the Scene RenderSet encoder.");
        }
        for (uint32_t planeIndex = 0u; planeIndex < TiffNames.size(); ++planeIndex)
        {
            glm::mat4 modelView(1.0f);
            modelView[3u][0u] = PlaneX[planeIndex];
            modelView[3u][2u] = -4.0f;
            objectData[planeIndex] = {
                .modelViewProjection = makeThreePerspectiveProjection(800u, 500u, 45.0, 0.01, 10.0) * modelView,
                .materialAndFlags = {0u, 0u, 0u, 0u},
            };
            instanceData[planeIndex] = {
                .tint = {1.0f, 1.0f, 1.0f, 1.0f},
            };
            materialData[planeIndex] = {
                .baseColor = {1.0f, 1.0f, 1.0f, 1.0f},
            };
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            appendTiffBuffer(allocation, WebglLoaderTextureTiffSceneRenderSetComponents::vertices, PlaneNames[planeIndex], vertices.data(), vertices.size() * sizeof(TexturedBoxHostVertex));
            appendTiffBuffer(allocation, WebglLoaderTextureTiffSceneRenderSetComponents::indices, PlaneNames[planeIndex], indices.data(), indices.size() * sizeof(uint32_t));
            appendTiffBuffer(allocation, WebglLoaderTextureTiffSceneRenderSetComponents::objects, PlaneNames[planeIndex], &objectData[planeIndex], sizeof(objectData[planeIndex]));
            appendTiffBuffer(allocation, WebglLoaderTextureTiffSceneRenderSetComponents::instances, PlaneNames[planeIndex], &instanceData[planeIndex], sizeof(instanceData[planeIndex]));
            appendTiffBuffer(allocation, WebglLoaderTextureTiffSceneRenderSetComponents::materials, PlaneNames[planeIndex], &materialData[planeIndex], sizeof(materialData[planeIndex]));
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebglLoaderTextureTiffSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = TiffNames[planeIndex],
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = textureWidths[planeIndex],
                .height = textureHeights[planeIndex],
                .data = textureBytes[planeIndex].data(),
                .dataStorageBytes = textureBytes[planeIndex].size(),
                .mipmapOffsetBytes = mipOffsets[planeIndex],
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            entityIndices[planeIndex] = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderTextureTiffRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderTextureTiffRuntimeAdapter::afterFrame(
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
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
            prepareTiffOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write the TIFF RGBA capture.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\": 1,\n"
                 << "  \"source\": \"gvm-three-r185\",\n"
                 << "  \"caseId\": \"webgl_loader_texture_tiff\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\": " << frameIndex << ",\n"
                 << "  \"randomSeed\": " << options.randomSeed << ",\n"
                 << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                 << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\": " << byteCount << ",\n"
                 << "  \"format\": \"rgba8unorm\",\n"
                 << "  \"samplePolicy\": {\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false}\n}\n";
        writeTiffText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\": 1,\n"
                 << "  \"caseId\": \"webgl_loader_texture_tiff\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\": 0,\n"
                 << "  \"gpuWorkDslOnly\": true,\n"
                 << "  \"renderSetPolicy\": \"required\",\n"
                 << "  \"sceneRenderSetCount\": 1,\n"
                 << "  \"renderSetType\": \"WebglLoaderTextureTiffSceneRenderSet\",\n"
                 << "  \"renderableObjectCount\": 3,\n"
                 << "  \"entityCount\": 3,\n"
                 << "  \"instanceCounts\": [1,1,1],\n"
                 << "  \"scenePassCount\": 1,\n"
                 << "  \"drawCommandCount\": 3,\n"
                 << "  \"directDrawFallback\": false,\n"
                 << "  \"textureExtents\": [[256,256],[512,512],[1024,1024]],\n"
                 << "  \"componentSchema\": [\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"]\n}\n";
        writeTiffText(options.sceneSnapshotPath, snapshot.str());
        if (options.scenarioId == "canonical-loader")
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\": 1,\n"
                     << "  \"caseId\": \"webgl_loader_texture_tiff\",\n"
                     << "  \"scenarioId\": \"canonical-loader\",\n"
                     << "  \"kind\": \"loader-snapshot\",\n"
                     << "  \"result\": {\"compression\":[\"uncompressed\",\"lzw\",\"jpeg\"],"
                     << "\"assetSha256\":[\"" << TiffHashes[0] << "\",\"" << TiffHashes[1] << "\",\"" << TiffHashes[2] << "\"],"
                     << "\"textureColorSpace\":\"srgb\",\"entityCount\":3}\n}\n";
            writeTiffText(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglLoaderTextureTiffRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
} // namespace GVM::ThreeSamples
