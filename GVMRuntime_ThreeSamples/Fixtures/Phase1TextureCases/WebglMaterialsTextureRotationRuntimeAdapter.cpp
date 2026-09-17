#include "WebglMaterialsTextureRotationRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "UGLBin/exports.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t UvGridExtent = 1024u;
        constexpr uint32_t UvGridMipCount = 11u;
        constexpr uint32_t GeometryGroupCount = 6u;
        constexpr const char *UvGridAssetSha256 = "909d9a1eb2a5d5de9d221a5e8de4e9119d409decddf522d48896bd51523d354d";
        constexpr const char *TransformReplaySha256 = "7f2b24034ef6fc429b0ce6503345a14f522671db33d8f169b80b96026bacb209";
        constexpr const char *CanonicalSceneSha256 = "f70dba992c8f96fe367c7142c6954af349e2e396498d86930a451e45065c0283";
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Mirrors the 48-byte texture-transform material emitted by both UGLC pipelines. */
        struct alignas(16) TextureRotationHostMaterialData
        {
            TexturedBoxHostFloat4 baseColor;
            TexturedBoxHostFloat4 offsetAndRepeat;
            TexturedBoxHostFloat4 rotationAndCenter;
        };

        static_assert(sizeof(TextureRotationHostMaterialData) == 48u);

        /** Creates parent directories for one explicitly requested capture artifact. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Reads one bounded file as exact bytes for immutable identity validation. */
        eastl::vector<uint8_t> readFileBytes(const std::filesystem::path &inputPath, const char *label)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open " + std::string(label) + ": " + inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(std::string(label) + " has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete " + std::string(label) + ".");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte sequence. */
        eastl::string calculateSha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Resolves one explicit replay path without consulting environment configuration. */
        std::filesystem::path resolveReplayPath(const ThreeSampleHostOptions &options)
        {
            const std::filesystem::path requested(options.inputReplayPath.c_str());
            if (requested.is_absolute() && std::filesystem::is_regular_file(requested))
            {
                return requested;
            }
            if (!requested.empty() && std::filesystem::is_regular_file(requested))
            {
                return std::filesystem::absolute(requested);
            }
            if (!options.assetRoot.empty())
            {
                const std::filesystem::path assetPath = std::filesystem::path(options.assetRoot.c_str()) / requested;
                if (std::filesystem::is_regular_file(assetPath))
                {
                    return assetPath;
                }
            }
            throw std::invalid_argument("uv-transform-orbit could not resolve its explicit --input-replay.");
        }

        /** Builds the exact default or replayed Three r185 GUI and OrbitControls state. */
        TextureRotationScenarioState makeScenarioState(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_materials_texture_rotation")
            {
                throw std::invalid_argument("Texture-rotation adapter requires case-id webgl_materials_texture_rotation.");
            }
            const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool canonical = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool transformed = options.scenarioId == "uv-transform-orbit" && options.targetFrame == 1u;
            if (!initial && !canonical && !transformed)
            {
                throw std::invalid_argument(
                    "webgl_materials_texture_rotation requires initial-loader/frame 0, "
                    "canonical-loader/frame 0, or uv-transform-orbit/frame 1.");
            }
            if (!transformed && !options.inputReplayPath.empty())
            {
                throw std::invalid_argument("Loader texture-rotation scenarios must not consume input replay.");
            }
            if (transformed && options.inputReplayPath.empty())
            {
                throw std::invalid_argument("uv-transform-orbit requires the canonical --input-replay document.");
            }

            TextureRotationScenarioState state;
            if (!transformed)
            {
                return state;
            }

            const eastl::vector<uint8_t> replayBytes = readFileBytes(resolveReplayPath(options), "texture-rotation input replay");
            state.replaySha256 = calculateSha256(replayBytes);
            if (state.replaySha256 != TransformReplaySha256)
            {
                throw std::invalid_argument("Texture-rotation replay SHA-256 differs from the locked GUI/orbit sequence.");
            }

            state.offsetX = 0.2;
            state.offsetY = 0.1;
            state.repeatX = 0.75;
            state.repeatY = 0.5;
            state.rotation = -0.6;
            state.centerX = 0.3;
            state.centerY = 0.7;

            constexpr double Pi = 3.14159265358979323846;
            const double radius = std::sqrt(950.0);
            const double initialTheta = std::atan2(10.0, 25.0);
            const double initialPhi = std::acos(15.0 / radius);
            const double replayTheta = initialTheta - 2.0 * Pi * 50.0 / 500.0;
            const double replayPhi = initialPhi + 2.0 * Pi * 30.0 / 500.0;
            state.cameraX = radius * std::sin(replayPhi) * std::sin(replayTheta);
            state.cameraY = radius * std::cos(replayPhi);
            state.cameraZ = radius * std::sin(replayPhi) * std::cos(replayTheta);
            state.replayTarget = "canvas";
            state.replayEventCount = 3u;
            state.replayLastEventFrame = 0u;
            state.usesInputReplay = true;
            return state;
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("Texture-rotation RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Builds the GVM view equivalent of Three's right-handed world-space camera. */
        glm::mat4 makeCameraView(const TextureRotationScenarioState &state)
        {
            const glm::dvec3 position(state.cameraX, -state.cameraY, state.cameraZ);
            const glm::dmat4 view = glm::lookAt(position, glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(0.0, 1.0, 0.0));
            return glm::mat4(view);
        }

        /** Reflects Three world Y into GVM clip convention without changing face UV identity. */
        void convertBoxGeometryToGvmCoordinates(eastl::vector<TexturedBoxHostVertex> &vertices, eastl::vector<uint32_t> &indices)
        {
            if (indices.size() % 3u != 0u)
            {
                throw std::invalid_argument("Texture-rotation coordinate conversion requires triangle-list indices.");
            }
            for (TexturedBoxHostVertex &vertex : vertices)
            {
                vertex.position.y = -vertex.position.y;
            }
            for (size_t index = 0u; index < indices.size(); index += 3u)
            {
                eastl::swap(indices[index + 1u], indices[index + 2u]);
            }
        }

        /** Appends one typed payload to a RenderSet buffer component allocation. */
        void appendBufferPayload(GVM::Core::RenderSetAllocInfo &allocation, GVM::Core::RenderComponentHandle component, const char *name, const void *value, uint64_t byteCount, uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Appends the complete explicit sRGB mip chain to one entity texture slot. */
        void appendUvGridTexturePayload(GVM::Core::RenderSetAllocInfo &allocation, const eastl::vector<uint8_t> &textureBytes, const eastl::vector<uint64_t> &mipOffsets)
        {
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = WebglMaterialsTextureRotationSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = "WebglMaterialsTextureRotationUvGridSrgb",
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = UvGridExtent,
                .height = UvGridExtent,
                .data = textureBytes.data(),
                .dataStorageBytes = textureBytes.size(),
                .mipmapOffsetBytes = mipOffsets,
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
        }
    } // namespace

    void WebglMaterialsTextureRotationRuntimeAdapter::initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
    {
        if (options.assetRoot.empty())
        {
            throw std::invalid_argument("webgl_materials_texture_rotation requires explicit --asset-root.");
        }
        scenarioState = makeScenarioState(options);
        device = inDevice;
        buildTexturedBoxGeometry(10.0f, 10.0f, 10.0f, vertices, indices);
        convertBoxGeometryToGvmCoordinates(vertices, indices);

        const std::filesystem::path texturePath = std::filesystem::path(options.assetRoot.c_str()) / "textures" / "uv_grid_opengl.jpg";
        const eastl::vector<uint8_t> assetBytes = readFileBytes(texturePath, "Three r185 UV-grid JPEG");
        if (calculateSha256(assetBytes) != UvGridAssetSha256)
        {
            throw std::invalid_argument("textures/uv_grid_opengl.jpg differs from the pinned Three r185 asset.");
        }
        const RgbaImageData uvGrid = decodeJpegRgba8(texturePath);
        if (uvGrid.width != UvGridExtent || uvGrid.height != UvGridExtent)
        {
            throw std::runtime_error("textures/uv_grid_opengl.jpg must decode to the locked 1024x1024 extent.");
        }
        const eastl::vector<RgbaImageData> mipChain = buildSrgbMipChain(uvGrid);
        if (mipChain.size() != UvGridMipCount)
        {
            throw std::runtime_error("UV-grid texture did not produce the expected complete eleven-level mip chain.");
        }

        uvGridTextureBytes.clear();
        uvGridMipOffsets.clear();
        for (const RgbaImageData &mip : mipChain)
        {
            uvGridMipOffsets.push_back(uvGridTextureBytes.size());
            uvGridTextureBytes.insert(uvGridTextureBytes.end(), mip.pixels.begin(), mip.pixels.end());
        }
        allocateSceneEntity(renderer, options);
    }

    void WebglMaterialsTextureRotationRuntimeAdapter::allocateSceneEntity(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        const glm::mat4 projection = makeThreePerspectiveProjection(options.width, options.height, 40.0, 1.0, 1000.0);
        const TexturedBoxHostObjectData objectData = {
            .modelViewProjection = projection * makeCameraView(scenarioState),
            .materialAndFlags = {0u, 0u, 0u, 0u},
        };
        const TexturedBoxHostInstanceData instanceData = {
            .tint = {1.0f, 1.0f, 1.0f, 1.0f},
        };
        const TextureRotationHostMaterialData materialData = {
            .baseColor = {1.0f, 1.0f, 1.0f, 1.0f},
            .offsetAndRepeat =
                {
                    static_cast<float>(scenarioState.offsetX),
                    static_cast<float>(scenarioState.offsetY),
                    static_cast<float>(scenarioState.repeatX),
                    static_cast<float>(scenarioState.repeatY),
                },
            .rotationAndCenter =
                {
                    static_cast<float>(scenarioState.rotation),
                    static_cast<float>(scenarioState.centerX),
                    static_cast<float>(scenarioState.centerY),
                    0.0f,
                },
        };

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("Texture-rotation case could not create its Scene RenderSet command encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendBufferPayload(allocation, WebglMaterialsTextureRotationSceneRenderSetComponents::vertices, "WebglMaterialsTextureRotationVertices", vertices.data(), vertices.size() * sizeof(TexturedBoxHostVertex), 1u);
        appendBufferPayload(allocation, WebglMaterialsTextureRotationSceneRenderSetComponents::indices, "WebglMaterialsTextureRotationIndices", indices.data(), indices.size() * sizeof(uint32_t), 1u);
        appendBufferPayload(allocation, WebglMaterialsTextureRotationSceneRenderSetComponents::objects, "WebglMaterialsTextureRotationObject", &objectData, sizeof(objectData), 1u);
        appendBufferPayload(allocation, WebglMaterialsTextureRotationSceneRenderSetComponents::instances, "WebglMaterialsTextureRotationInstance", &instanceData, sizeof(instanceData), 1u);
        appendBufferPayload(allocation, WebglMaterialsTextureRotationSceneRenderSetComponents::materials, "WebglMaterialsTextureRotationMaterial", &materialData, sizeof(materialData), 1u);
        appendUvGridTexturePayload(allocation, uvGridTextureBytes, uvGridMipOffsets);

        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglMaterialsTextureRotationRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMaterialsTextureRotationRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computeRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("Texture-rotation capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();

        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeLoaderSemanticSnapshot(options);
        captureWritten = true;
    }

    void WebglMaterialsTextureRotationRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglMaterialsTextureRotationRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open texture-rotation RGBA output path.");
        }
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write complete texture-rotation RGBA capture.");
        }
    }

    void WebglMaterialsTextureRotationRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open texture-rotation metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_materials_texture_rotation\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"inputReplay\": ";
        if (!scenarioState.usesInputReplay)
        {
            output << "null\n";
        }
        else
        {
            output << "{\n"
                   << "    \"schemaVersion\": 1,\n"
                   << "    \"sha256\": \"" << scenarioState.replaySha256.c_str() << "\",\n"
                   << "    \"caseId\": \"webgl_materials_texture_rotation\",\n"
                   << "    \"scenarioId\": \"uv-transform-orbit\",\n"
                   << "    \"captureFrame\": 1,\n"
                   << "    \"eventCount\": " << scenarioState.replayEventCount << ",\n"
                   << "    \"lastEventFrame\": " << scenarioState.replayLastEventFrame << ",\n"
                   << "    \"target\": \"" << scenarioState.replayTarget.c_str() << "\"\n"
                   << "  }\n";
        }
        output << "}\n";
    }

    void WebglMaterialsTextureRotationRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open texture-rotation snapshot output path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_materials_texture_rotation\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"entityCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 1,\n"
               << "  \"geometryGroupCount\": " << GeometryGroupCount << ",\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"indexedVertexCount\": 36,\n"
               << "  \"boxVertexCount\": 24,\n"
               << "  \"texturePath\": \"textures/uv_grid_opengl.jpg\",\n"
               << "  \"textureAssetSha256\": \"" << UvGridAssetSha256 << "\",\n"
               << "  \"textureColorSpace\": \"srgb\",\n"
               << "  \"textureWidth\": 1024,\n"
               << "  \"textureHeight\": 1024,\n"
               << "  \"mipLevelCount\": 11,\n"
               << "  \"mipmapGeneration\": \"explicit-cpu-linear-light\",\n"
               << "  \"samplerAddressMode\": \"repeat\",\n"
               << "  \"samplerMaxAnisotropy\": 16,\n"
               << "  \"offset\": [" << scenarioState.offsetX << ", " << scenarioState.offsetY << "],\n"
               << "  \"repeat\": [" << scenarioState.repeatX << ", " << scenarioState.repeatY << "],\n"
               << "  \"rotation\": " << scenarioState.rotation << ",\n"
               << "  \"center\": [" << scenarioState.centerX << ", " << scenarioState.centerY << "],\n"
               << "  \"cameraFovDegrees\": 40,\n"
               << "  \"cameraNear\": 1,\n"
               << "  \"cameraFar\": 1000,\n"
               << "  \"cameraPosition\": [" << scenarioState.cameraX << ", " << scenarioState.cameraY << ", " << scenarioState.cameraZ << "],\n"
               << "  \"orbitTarget\": [0, 0, 0],\n"
               << "  \"inputReplayEventCount\": " << scenarioState.replayEventCount << ",\n"
               << "  \"sceneRoots\": [\n"
               << "    {\n"
               << "      \"id\": \"scene\",\n"
               << "      \"renderSetCount\": 1,\n"
               << "      \"renderSetId\": \"scene\",\n"
               << "      \"renderSetType\": \"WebglMaterialsTextureRotationSceneRenderSet\",\n"
               << "      \"renderableObjectCount\": 1,\n"
               << "      \"entityCount\": 1,\n"
               << "      \"entities\": [{\"entityId\": " << entityIndex << ", \"logicalRenderableId\": \"box\", \"instanceCount\": 1}],\n"
               << "      \"componentSchema\": [\n"
               << "        {\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},\n"
               << "        {\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},\n"
               << "        {\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},\n"
               << "        {\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},\n"
               << "        {\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},\n"
               << "        {\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}\n"
               << "      ],\n"
               << "      \"drawCommandCount\": 1,\n"
               << "      \"directDrawFallback\": false,\n"
               << "      \"scenePasses\": [\n"
               << "        {\"name\":\"main\",\"renderClass\":\"WebglMaterialsTextureRotationMainPass\","
                  "\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,"
                  "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
                  "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
                  "\"usesExplicitDrawCount\":false}\n"
               << "      ]\n"
               << "    }\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }

    void WebglMaterialsTextureRotationRuntimeAdapter::writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options) const
    {
        if (options.scenarioId != "canonical-loader" || options.semanticSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.semanticSnapshotPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open texture-rotation loader semantic output path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_materials_texture_rotation\",\n"
               << "  \"scenarioId\": \"canonical-loader\",\n"
               << "  \"frame\": 0,\n"
               << "  \"kind\": \"loader-snapshot\",\n"
               << "  \"canonicalState\": \"canonical-loaded-scene\",\n"
               << "  \"result\": {\n"
               << "    \"assetPath\": \"textures/uv_grid_opengl.jpg\",\n"
               << "    \"assetSha256\": \"" << UvGridAssetSha256 << "\",\n"
               << "    \"canonicalSceneSha256\": \"" << CanonicalSceneSha256 << "\",\n"
               << "    \"geometryGroupCount\": 6,\n"
               << "    \"indexCount\": 36,\n"
               << "    \"mipLevelCount\": 11,\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"sceneRootCount\": 1,\n"
               << "    \"textureHeight\": 1024,\n"
               << "    \"textureWidth\": 1024,\n"
               << "    \"vertexCount\": 24\n"
               << "  }\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
