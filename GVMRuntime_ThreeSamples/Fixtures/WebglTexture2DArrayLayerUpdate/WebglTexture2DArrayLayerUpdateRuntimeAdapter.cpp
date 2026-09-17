#include "WebglTexture2DArrayLayerUpdateRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <EASTL/string.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t LayerWidth = 496u;
        constexpr uint32_t LayerHeight = 260u;
        constexpr uint32_t SourceLayerCount = 6u;
        constexpr uint64_t LayerByteCount =
            uint64_t(LayerWidth) * LayerHeight * 4u;
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr const char *SourceSha256 =
            "afab737973d40b8b2df1deda7204284e8a1b4271e1417c740a7de19254257f4b";
        constexpr const char *LayerSha256[SourceLayerCount] = {
            "986448d4fcd79b9f7cc60b9f2f9e54d2a67b82a58d7fdc25162df244d6267ecc",
            "4f3a2649b84aac45b3f0489a2dce33d47d40f8d6f5d0c6cadbab5c6c669c67d3",
            "46d797704adae32da9b96988dd5ce0e5467efa46b0c28594862ab43c1adbc279",
            "6a956062790277c42e110ff7da4a16e3c3de7ec26f596ad8bd5c0bf27b92ade0",
            "7fc97349fbd3f36805bf5bc979aa1517e61c2393e672c2d89afe754cd0627a9a",
            "6d4347b1fe149c636bdb5f670164f44610017911a9af4bb55c89f446830dbb33",
        };

        static_assert(sizeof(WebglTexture2DArrayLayerUpdateHostVertex) == 16u);
        static_assert(sizeof(WebglTexture2DArrayLayerUpdateHostObjectData) == 16u);
        static_assert(sizeof(WebglTexture2DArrayLayerUpdateHostInstanceData) == 16u);
        static_assert(sizeof(WebglTexture2DArrayLayerUpdateHostMaterialData) == 16u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareLayerUpdateOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Reads one immutable binary asset with an exact byte count. */
        eastl::vector<uint8_t> readLayerUpdateAsset(
            const std::filesystem::path &path,
            uint64_t expectedByteCount)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open a locked texture-layer asset.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                (expectedByteCount != 0u &&
                 uint64_t(byteCount) != expectedByteCount))
            {
                throw std::runtime_error(
                    "A texture-layer asset differs from its locked size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read a complete texture-layer asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one byte sequence. */
        eastl::string calculateLayerUpdateSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Texture-layer SHA-256 input is too large.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
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

        /** Appends one generated buffer component payload. */
        void appendLayerUpdatePayload(
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

        /** Writes one tightly packed RGBA8 file. */
        void writeLayerUpdateRgba(
            const eastl::string &pathValue,
            const eastl::vector<uint8_t> &rgba)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path outputPath(pathValue.c_str());
            prepareLayerUpdateOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write the texture-layer RGBA8 capture.");
            }
        }
    } // namespace

    void WebglTexture2DArrayLayerUpdateRuntimeAdapter::initialize(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool transfer =
            options.scenarioId == "transfer-layer" && options.targetFrame == 1u;
        if (options.caseId != "webgl_texture2darray_layerupdate" ||
            options.randomSeed != 0x12345678u ||
            options.assetRoot.empty() ||
            (!initial && !transfer))
        {
            throw std::invalid_argument(
                "Texture-layer adapter received a request outside its Manifest contract.");
        }

        device = inDevice;
        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        const eastl::vector<uint8_t> sourceBytes = readLayerUpdateAsset(
            assetRoot / "textures" / "spiritedaway.ktx2",
            0u);
        if (calculateLayerUpdateSha256(sourceBytes) != SourceSha256)
        {
            throw std::invalid_argument(
                "Spirited Away KTX2 differs from Three r185.");
        }

        layers.clear();
        layers.reserve(SourceLayerCount);
        for (uint32_t layer = 0u; layer < SourceLayerCount; ++layer)
        {
            const eastl::string layerName =
                "spiritedaway_layer" + eastl::to_string(layer) + ".rgba";
            eastl::vector<uint8_t> layerBytes = readLayerUpdateAsset(
                assetRoot / "decoded" / layerName.c_str(),
                LayerByteCount);
            if (calculateLayerUpdateSha256(layerBytes) != LayerSha256[layer])
            {
                throw std::invalid_argument(
                    "A decoded Spirited Away layer differs from the locked UASTC decode.");
            }
            layers.push_back(eastl::move(layerBytes));
        }

        const WebglTexture2DArrayLayerUpdateHostVertex vertices[4u] = {
            {glm::vec4(-10.0f, -5.0f, 0.0f, 0.0f)},
            {glm::vec4(10.0f, -5.0f, 1.0f, 0.0f)},
            {glm::vec4(10.0f, 5.0f, 1.0f, 1.0f)},
            {glm::vec4(-10.0f, 5.0f, 0.0f, 1.0f)},
        };
        const uint32_t indices[6u] = {0u, 1u, 3u, 1u, 2u, 3u};
        const float tangentHalfFov = std::tan(22.5f * 3.14159265358979323846f / 180.0f);
        const WebglTexture2DArrayLayerUpdateHostObjectData objectData = {
            glm::vec4(
                1.0f / (70.0f * tangentHalfFov * 1.6f),
                1.0f / (70.0f * tangentHalfFov),
                0.0f,
                0.0f)};
        const WebglTexture2DArrayLayerUpdateHostInstanceData instances[3u] = {
            {glm::vec4(10.0f, 0.0f, 0.0f, 0.0f)},
            {glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)},
            {glm::vec4(-10.0f, 2.0f, 0.0f, 0.0f)},
        };
        const WebglTexture2DArrayLayerUpdateHostMaterialData materialData = {
            glm::vec4(1.0f)};

        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = 4u;
        allocation.indicesCount = 6u;
        allocation.instanceCount = 3u;
        appendLayerUpdatePayload(
            allocation,
            WebglTexture2DArrayLayerUpdateSceneRenderSetComponents::vertices,
            "WebglTexture2DArrayLayerUpdateVertices",
            vertices,
            sizeof(vertices),
            1u);
        appendLayerUpdatePayload(
            allocation,
            WebglTexture2DArrayLayerUpdateSceneRenderSetComponents::indices,
            "WebglTexture2DArrayLayerUpdateIndices",
            indices,
            sizeof(indices),
            1u);
        appendLayerUpdatePayload(
            allocation,
            WebglTexture2DArrayLayerUpdateSceneRenderSetComponents::objects,
            "WebglTexture2DArrayLayerUpdateObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendLayerUpdatePayload(
            allocation,
            WebglTexture2DArrayLayerUpdateSceneRenderSetComponents::instances,
            "WebglTexture2DArrayLayerUpdateInstances",
            instances,
            sizeof(instances),
            3u);
        appendLayerUpdatePayload(
            allocation,
            WebglTexture2DArrayLayerUpdateSceneRenderSetComponents::materials,
            "WebglTexture2DArrayLayerUpdateMaterial",
            &materialData,
            sizeof(materialData),
            1u);

        const uint32_t textureLayers[3u] = {
            0u,
            transfer ? 4u : 1u,
            2u,
        };
        GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
        textureComponent.textureComponentHandle =
            WebglTexture2DArrayLayerUpdateSceneRenderSetComponents::textures;
        const eastl::vector<uint64_t> mipOffsets = {0u};
        for (uint32_t textureSlot = 0u; textureSlot < 3u; ++textureSlot)
        {
            const uint32_t sourceLayer = textureLayers[textureSlot];
            textureComponent.textures.push_back({
                .textureName =
                    "WebglTexture2DArrayLayerUpdateLayer" +
                    eastl::to_string(textureSlot),
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = LayerWidth,
                .height = LayerHeight,
                .data = layers[sourceLayer].data(),
                .dataStorageBytes = layers[sourceLayer].size(),
                .mipmapOffsetBytes = mipOffsets,
            });
        }
        allocation.textureInfos.push_back(eastl::move(textureComponent));

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create the texture-layer RenderSet encoder.");
        }
        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebglTexture2DArrayLayerUpdateRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglTexture2DArrayLayerUpdateRuntimeAdapter::afterFrame(
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
            throw std::overflow_error(
                "Texture-layer capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeLayerUpdateRgba(options.captureRgbaPath, rgba);

        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareLayerUpdateOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\"webgl_texture2darray_layerupdate\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"randomSeed\":" << options.randomSeed << ",\n"
                   << "  \"width\":" << width << ",\n"
                   << "  \"height\":" << height << ",\n"
                   << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                   << "  \"byteCount\":" << rgba.size() << ",\n"
                   << "  \"format\":\"rgba8unorm\",\n"
                   << "  \"inputReplay\":";
            if (options.scenarioId == "transfer-layer")
            {
                output << "{\"sha256\":\"3d542955228da38babf876044bd3868d0dbf4439b0db628b6c63fbffba336b33\","
                       << "\"caseId\":\"webgl_texture2darray_layerupdate\","
                       << "\"scenarioId\":\"transfer-layer\","
                       << "\"captureFrame\":1,\"eventCount\":1,"
                       << "\"target\":\"canvas\"}";
            }
            else
            {
                output << "null";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareLayerUpdateOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgl_texture2darray_layerupdate\",\n"
                   << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"implementationLevel\":\"semantic-complete\",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"renderSetPolicy\":\"required\",\n"
                   << "  \"sceneRenderSetCount\":1,\n"
                   << "  \"renderableObjectCount\":1,\n"
                   << "  \"entityCount\":1,\n"
                   << "  \"instanceCount\":3,\n"
                   << "  \"vertexCount\":4,\n"
                   << "  \"indexCount\":6,\n"
                   << "  \"scenePassCount\":1,\n"
                   << "  \"screenPassCount\":0,\n"
                   << "  \"drawCommandCount\":1,\n"
                   << "  \"renderSetType\":\"WebglTexture2DArrayLayerUpdateSceneRenderSet\",\n"
                   << "  \"componentSchema\":["
                   << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                   << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                   << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                   << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                   << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                   << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                   << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
                   << "\"renderSetId\":\"scene-set\","
                   << "\"renderSetType\":\"WebglTexture2DArrayLayerUpdateSceneRenderSet\","
                   << "\"renderableObjectCount\":1,\"entityCount\":1,"
                   << "\"entities\":[{\"entityId\":" << entityIndex
                   << ",\"logicalRenderableId\":\"spirited-away-three-plane-stack\","
                   << "\"instanceCount\":3}],"
                   << "\"componentSchema\":["
                   << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                   << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                   << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                   << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                   << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                   << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],"
                   << "\"drawCommandCount\":1,"
                   << "\"directDrawFallback\":false,\"scenePasses\":[{"
                   << "\"name\":\"array-layer-instances\","
                   << "\"renderClass\":\"WebglTexture2DArrayLayerUpdateScenePass\","
                   << "\"renderSetId\":\"scene-set\","
                   << "\"renderSetBindingCount\":1,"
                   << "\"drawMode\":\"render-set-indexed-indirect\","
                   << "\"invocationCount\":1,\"drawCommandCount\":1,"
                   << "\"usesStandaloneGeometry\":false,"
                   << "\"usesExplicitDrawCount\":false}]}],\n"
                   << "  \"scenePassInvocations\":[{"
                   << "\"sceneRoot\":\"scene\","
                   << "\"scenePass\":\"array-layer-instances\","
                   << "\"invocationCount\":1}],\n"
                   << "  \"scenePassSequence\":[{"
                   << "\"sceneRoot\":\"scene\","
                   << "\"scenePass\":\"array-layer-instances\"}],\n"
                   << "  \"usesRenderEntityID\":true,\n"
                   << "  \"usesRenderEntityInstanceID\":true,\n"
                   << "  \"sourceAssetSha256\":\"" << SourceSha256 << "\",\n"
                   << "  \"transferredSourceLayer\":"
                   << (options.scenarioId == "transfer-layer" ? 4 : -1) << ",\n"
                   << "  \"transferredDestinationLayer\":"
                   << (options.scenarioId == "transfer-layer" ? 1 : -1) << ",\n"
                   << "  \"directDrawFallback\":false\n}\n";
        }
        captureWritten = true;
    }

    void WebglTexture2DArrayLayerUpdateRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        layers.clear();
        captureWritten = false;
    }
} // namespace GVM::ThreeSamples
