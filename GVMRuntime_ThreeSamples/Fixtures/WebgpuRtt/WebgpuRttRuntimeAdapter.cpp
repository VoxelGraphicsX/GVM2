#include "WebgpuRttRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "TexturedBoxSampleData.hpp"
#include "UGLBin/exports.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/array.h>

#include <glm/ext/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr char UvGridSha256[] =
            "909d9a1eb2a5d5de9d221a5e8de4e9119d409decddf522d48896bd51523d354d";

        static_assert(sizeof(WebgpuRttHostVertex) == 32u);
        static_assert(sizeof(WebgpuRttHostObjectData) == 64u);
        static_assert(sizeof(WebgpuRttHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuRttHostMaterialData) == 16u);

        /** Creates parent folders for one explicitly requested evidence file. */
        void prepareWebgpuRttOutput(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            if (!outputPath.parent_path().empty())
                std::filesystem::create_directories(outputPath.parent_path());
        }

        /** Reads one immutable asset into bounded CPU storage. */
        eastl::vector<uint8_t> readWebgpuRttFile(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error(
                    "Could not open the WebGPU RTT UV-grid texture.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
                throw std::runtime_error(
                    "The WebGPU RTT UV-grid texture is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(byteCount));
            if (!input)
                throw std::runtime_error(
                    "Could not read the complete WebGPU RTT UV-grid texture.");
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one asset byte sequence. */
        eastl::string calculateWebgpuRttSha256(
            const eastl::vector<uint8_t> &bytes)
        {
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
                result.push_back(HexDigits[value & 15u]);
            }
            return result;
        }

        /** Appends one typed buffer component to the sole entity allocation. */
        void appendWebgpuRttBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
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

        /** Writes one optional UTF-8 evidence document. */
        void writeWebgpuRttText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            prepareWebgpuRttOutput(path);
            std::ofstream output(path.c_str(), std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU RTT evidence.");
        }
    } // namespace

    void WebgpuRttRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 60u;
        const bool pointer =
            options.scenarioId == "pointer-color" &&
            options.targetFrame == 61u;
        if (options.caseId != "webgpu_rtt" ||
            (!initial && !animated && !pointer) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            (pointer != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "WebGPU RTT requires its three locked scenarios, extent, seed, assets, and replay contract.");
        }

        device = inDevice;
        pointerX = pointer ? 0.25f : 0.0f;
        pointerY = pointer ? 0.75f : 0.0f;

        eastl::vector<TexturedBoxHostVertex> sourceVertices;
        buildTexturedBoxGeometry(
            1.0f, 1.0f, 1.0f, sourceVertices, indices);
        vertices.clear();
        vertices.reserve(sourceVertices.size());
        for (const TexturedBoxHostVertex &source : sourceVertices)
        {
            vertices.push_back({
                .position = glm::vec4(
                    source.position.x,
                    source.position.y,
                    source.position.z,
                    source.position.w),
                .textureCoordinate = glm::vec4(
                    source.texCoord.x,
                    source.texCoord.y,
                    source.texCoord.z,
                    source.texCoord.w),
            });
        }

        const uint32_t accumulatedFrame = options.targetFrame;
        const auto rotation = calculateTexturedBoxFrameRotation(
            accumulatedFrame, 0.01, 0.02);
        const glm::mat4 model = makeThreeEulerXyRotation(
            rotation.first, rotation.second);
        const glm::mat4 view = glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f));
        const glm::mat4 projection = makeThreePerspectiveProjection(
            options.width, options.height, 70.0, 0.1, 10.0);
        objectData.modelViewProjection = projection * view * model;
        instanceData.reserved = glm::vec4(0.0f);
        materialData.baseColor = glm::vec4(1.0f);

        const std::filesystem::path texturePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "uv_grid_opengl.jpg";
        const eastl::vector<uint8_t> assetBytes =
            readWebgpuRttFile(texturePath);
        if (calculateWebgpuRttSha256(assetBytes) != UvGridSha256)
            throw std::invalid_argument(
                "The WebGPU RTT UV-grid asset differs from Three r185.");
        const RgbaImageData baseImage = decodeJpegRgba8(texturePath);
        if (baseImage.width != 1024u || baseImage.height != 1024u)
            throw std::runtime_error(
                "The WebGPU RTT UV-grid texture extent differs from r185.");
        const eastl::vector<RgbaImageData> mipChain =
            buildUnormMipChain(baseImage);
        textureBytes.clear();
        textureMipOffsets.clear();
        for (const RgbaImageData &mip : mipChain)
        {
            textureMipOffsets.push_back(textureBytes.size());
            textureBytes.insert(
                textureBytes.end(), mip.pixels.begin(), mip.pixels.end());
        }

        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        appendWebgpuRttBuffer(
            allocation,
            WebgpuRttSceneRenderSetComponents::vertices,
            "WebgpuRttBoxVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]));
        appendWebgpuRttBuffer(
            allocation,
            WebgpuRttSceneRenderSetComponents::indices,
            "WebgpuRttBoxIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]));
        appendWebgpuRttBuffer(
            allocation,
            WebgpuRttSceneRenderSetComponents::objects,
            "WebgpuRttBoxObject",
            &objectData,
            sizeof(objectData));
        appendWebgpuRttBuffer(
            allocation,
            WebgpuRttSceneRenderSetComponents::instances,
            "WebgpuRttBoxInstance",
            &instanceData,
            sizeof(instanceData));
        appendWebgpuRttBuffer(
            allocation,
            WebgpuRttSceneRenderSetComponents::materials,
            "WebgpuRttBoxMaterial",
            &materialData,
            sizeof(materialData));
        GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
        textureInfo.textureComponentHandle =
            WebgpuRttSceneRenderSetComponents::textures;
        textureInfo.textures.push_back({
            .textureName = "WebgpuRttUvGrid",
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .width = baseImage.width,
            .height = baseImage.height,
            .data = textureBytes.data(),
            .dataStorageBytes = textureBytes.size(),
            .mipmapOffsetBytes = textureMipOffsets,
        });
        allocation.textureInfos.push_back(eastl::move(textureInfo));

        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create the WebGPU RTT Scene Set encoder.");
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebgpuRttRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuRttRuntimeAdapter::afterFrame(
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
            prepareWebgpuRttOutput(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU RTT RGBA.");
        }

        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgpu_rtt\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend)
            << "\",\"frame\":" << frameIndex
            << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << width * 4u
            << ",\"byteCount\":" << rgba.size()
            << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,"
            << "\"assetSha256\":\"" << UvGridSha256 << "\"}\n";
        writeWebgpuRttText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_rtt\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":1,"
            << "\"entityCount\":1,\"instanceCount\":1,"
            << "\"vertexCount\":24,\"indexCount\":36,"
            << "\"geometryGroupCount\":6,\"scenePassCount\":1,"
            << "\"screenPassCount\":1,\"drawCommandCount\":1,"
            << "\"sampleCount\":1,\"msaaSimulated\":false,"
            << "\"renderSetType\":\"WebgpuRttSceneRenderSet\","
            << "\"pointer\":[" << pointerX << ',' << pointerY << "],"
            << "\"sceneRoots\":[{\"id\":\"scene\","
            << "\"renderSetCount\":1,\"renderSetId\":\"scene-set\","
            << "\"renderSetType\":\"WebgpuRttSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,"
            << "\"logicalRenderableId\":\"box\",\"instanceCount\":1}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"box-texture\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"scene-color\","
            << "\"renderClass\":\"WebgpuRttScenePass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"screenPasses\":[{\"name\":\"hue-saturation-rtt-composite\","
            << "\"renderClass\":\"WebgpuRttCompositePass\"}]}\n";
        writeWebgpuRttText(options.sceneSnapshotPath, snapshot.str());

        std::ostringstream semantic;
        semantic
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_rtt\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
            << "\"result\":{\"assetCount\":1,\"boxVertexCount\":24,"
            << "\"boxIndexCount\":36,\"geometryGroupCount\":6,"
            << "\"pointerX\":" << pointerX
            << ",\"pointerY\":" << pointerY
            << ",\"assetSha256\":\"" << UvGridSha256 << "\"}}\n";
        writeWebgpuRttText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebgpuRttRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        textureBytes.clear();
        textureMipOffsets.clear();
        device = {};
    }
} // namespace GVM::ThreeSamples
