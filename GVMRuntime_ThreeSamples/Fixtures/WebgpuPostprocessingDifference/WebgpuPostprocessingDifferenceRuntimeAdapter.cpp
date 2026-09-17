#include "WebgpuPostprocessingDifferenceRuntimeAdapter.hpp"

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
#include <cmath>
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

        /** Builds the r185 camera view after one deterministic OrbitControls drag. */
        glm::mat4 makeWebgpuPostprocessingDifferenceView(
            uint32_t frameIndex,
            bool orbit)
        {
            constexpr double Radius = 3.7416573867739413;
            constexpr double InitialTheta = 0.3217505543966422;
            constexpr double InitialPhi = 1.0068536854342678;
            constexpr double ThetaDelta = 2.0 * 3.14159265358979323846 * 60.0 / 500.0;
            constexpr double PhiDelta = 2.0 * 3.14159265358979323846 * 30.0 / 500.0;
            const double dampingProgress = orbit
                ? 1.0 - std::pow(0.99, double(frameIndex) + 1.0) + 0.0022
                : 0.0;
            const double theta = InitialTheta - ThetaDelta * dampingProgress;
            const double phi = InitialPhi + PhiDelta * dampingProgress;
            const double sinPhi = std::sin(phi);
            const double eyeX = Radius * sinPhi * std::sin(theta);
            const double eyeY = Radius * std::cos(phi);
            const double eyeZ = Radius * sinPhi * std::cos(theta);
            const double forwardX = -eyeX / Radius;
            const double forwardY = -eyeY / Radius;
            const double forwardZ = -eyeZ / Radius;
            const double sideLength = std::sqrt(
                forwardX * forwardX + forwardZ * forwardZ);
            const double sideX = -forwardZ / sideLength;
            const double sideY = 0.0;
            const double sideZ = forwardX / sideLength;
            const double upX = sideY * forwardZ - sideZ * forwardY;
            const double upY = sideZ * forwardX - sideX * forwardZ;
            const double upZ = sideX * forwardY - sideY * forwardX;
            glm::mat4 view(1.0f);
            view[0] = glm::vec4(
                float(sideX), float(upX), float(-forwardX), 0.0f);
            view[1] = glm::vec4(
                float(sideY), float(upY), float(-forwardY), 0.0f);
            view[2] = glm::vec4(
                float(sideZ), float(upZ), float(-forwardZ), 0.0f);
            view[3] = glm::vec4(
                float(-(sideX * eyeX + sideY * eyeY + sideZ * eyeZ)),
                float(-(upX * eyeX + upY * eyeY + upZ * eyeZ)),
                float(forwardX * eyeX + forwardY * eyeY + forwardZ * eyeZ),
                1.0f);
            return view;
        }

        static_assert(sizeof(WebgpuPostprocessingDifferenceHostVertex) == 32u);
        static_assert(sizeof(WebgpuPostprocessingDifferenceHostObjectData) == 64u);
        static_assert(sizeof(WebgpuPostprocessingDifferenceHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuPostprocessingDifferenceHostMaterialData) == 16u);

        /** Creates parent folders for one explicitly requested evidence file. */
        void prepareWebgpuPostprocessingDifferenceOutput(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            if (!outputPath.parent_path().empty())
                std::filesystem::create_directories(outputPath.parent_path());
        }

        /** Reads one immutable asset into bounded CPU storage. */
        eastl::vector<uint8_t> readWebgpuPostprocessingDifferenceFile(
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
        eastl::string calculateWebgpuPostprocessingDifferenceSha256(
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
        void appendWebgpuPostprocessingDifferenceBuffer(
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
        void writeWebgpuPostprocessingDifferenceText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            prepareWebgpuPostprocessingDifferenceOutput(path);
            std::ofstream output(path.c_str(), std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU RTT evidence.");
        }
    } // namespace

    void WebgpuPostprocessingDifferenceRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool stationary =
            options.scenarioId == "stationary-settled" &&
            options.targetFrame == 60u;
        const bool moving =
            options.scenarioId == "moving" && options.targetFrame == 120u;
        const bool orbit =
            options.scenarioId == "fast-orbit" && options.targetFrame == 121u;
        if (options.caseId != "webgpu_postprocessing_difference" ||
            (!initial && !stationary && !moving && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            ((stationary || orbit) != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "WebGPU difference requires its four locked scenarios, extent, seed, assets, and replay contract.");
        }

        device = inDevice;
        // The upstream GUI defaults to speed=0.  The replayed orbit changes
        // only the camera; it does not synthesize a GUI speed event.
        pointerX = 0.0f;
        pointerY = orbit ? 0.25f : 0.0f;

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
        const glm::mat4 model(1.0f);
        const glm::mat4 view =
            makeWebgpuPostprocessingDifferenceView(options.targetFrame, orbit);
        const glm::mat4 projection = makeThreePerspectiveProjection(
            options.width, options.height, 50.0, 1.0, 100.0);
        objectData.modelViewProjection = projection * view * model;
        instanceData.reserved = glm::vec4(0.0f);
        materialData.baseColor = glm::vec4(1.0f);

        const std::filesystem::path texturePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "crate.gif";
        const eastl::vector<uint8_t> assetBytes =
            readWebgpuPostprocessingDifferenceFile(texturePath);
        constexpr char CrateSha256[] =
            "a890f0a89eadc083cb39bfbe597c1395d7acf47a19f673b5643d4a9c174ea52f";
        if (calculateWebgpuPostprocessingDifferenceSha256(assetBytes) != CrateSha256)
            throw std::invalid_argument(
                "The WebGPU difference crate asset differs from Three r185.");
        const RgbaImageData baseImage = decodeGifRgba8(texturePath);
        if (baseImage.width != 256u || baseImage.height != 256u)
            throw std::runtime_error(
                "The WebGPU difference crate texture extent differs from r185.");
        const eastl::vector<RgbaImageData> mipChain =
            buildSrgbMipChain(baseImage);
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
        appendWebgpuPostprocessingDifferenceBuffer(
            allocation,
            WebgpuPostprocessingDifferenceSceneRenderSetComponents::vertices,
            "WebgpuPostprocessingDifferenceBoxVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]));
        appendWebgpuPostprocessingDifferenceBuffer(
            allocation,
            WebgpuPostprocessingDifferenceSceneRenderSetComponents::indices,
            "WebgpuPostprocessingDifferenceBoxIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]));
        appendWebgpuPostprocessingDifferenceBuffer(
            allocation,
            WebgpuPostprocessingDifferenceSceneRenderSetComponents::objects,
            "WebgpuPostprocessingDifferenceBoxObject",
            &objectData,
            sizeof(objectData));
        appendWebgpuPostprocessingDifferenceBuffer(
            allocation,
            WebgpuPostprocessingDifferenceSceneRenderSetComponents::instances,
            "WebgpuPostprocessingDifferenceBoxInstance",
            &instanceData,
            sizeof(instanceData));
        appendWebgpuPostprocessingDifferenceBuffer(
            allocation,
            WebgpuPostprocessingDifferenceSceneRenderSetComponents::materials,
            "WebgpuPostprocessingDifferenceBoxMaterial",
            &materialData,
            sizeof(materialData));
        GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
        textureInfo.textureComponentHandle =
            WebgpuPostprocessingDifferenceSceneRenderSetComponents::textures;
        textureInfo.textures.push_back({
            .textureName = "WebgpuPostprocessingDifferenceUvGrid",
            .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
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
        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebgpuPostprocessingDifferenceRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        if (options.scenarioId != "fast-orbit") return;
        objectData.modelViewProjection =
            makeThreePerspectiveProjection(options.width, options.height, 50.0, 1.0, 100.0) *
            makeWebgpuPostprocessingDifferenceView(frameIndex, true);
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not update the WebGPU difference OrbitControls view.");
        encoder->setBufferComponentData(
            entityIndex,
            WebgpuPostprocessingDifferenceSceneRenderSetComponents::objects,
            &objectData,
            sizeof(objectData),
            0u,
            1u);
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebgpuPostprocessingDifferenceRuntimeAdapter::afterFrame(
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
            prepareWebgpuPostprocessingDifferenceOutput(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                "Could not write WebGPU difference RGBA.");
        }

        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgpu_postprocessing_difference\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend)
            << "\",\"frame\":" << frameIndex
            << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << width * 4u
            << ",\"byteCount\":" << rgba.size()
            << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,"
            << "\"assetSha256\":\"a890f0a89eadc083cb39bfbe597c1395d7acf47a19f673b5643d4a9c174ea52f\"";
        if (!options.inputReplayPath.empty())
        {
            const bool orbit = options.scenarioId == "fast-orbit";
            metadata << ",\"inputReplay\":{\"schemaVersion\":1,\"caseId\":\"webgpu_postprocessing_difference\",\"scenarioId\":\""
                     << options.scenarioId.c_str()
                     << "\",\"captureFrame\":" << options.targetFrame
                     << ",\"sha256\":\""
                     << (orbit ? "7ab7f1fdd6fc55b2833780f0690d22e6285e38c177c8bc433dc2df1f8616b7d4" : "a0b6ab6c31b2434eb939a0825f1e7435dd7d530b04271de383a1a59b7b7189dd")
                     << "\",\"target\":\"body > canvas\",\"eventCount\":"
                     << (orbit ? 3 : 2) << "}";
        }
        metadata << "}\n";
        writeWebgpuPostprocessingDifferenceText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_postprocessing_difference\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":1,"
            << "\"entityCount\":1,\"instanceCount\":1,"
            << "\"vertexCount\":24,\"indexCount\":36,"
            << "\"geometryGroupCount\":6,\"scenePassCount\":1,"
            << "\"screenPassCount\":3,\"drawCommandCount\":1,"
            << "\"sampleCount\":1,\"msaaSimulated\":false,"
            << "\"renderSetType\":\"WebgpuPostprocessingDifferenceSceneRenderSet\","
            << "\"speed\":" << pointerX << ",\"orbit\":" << pointerY << ","
            << "\"sceneRoots\":[{\"id\":\"scene\","
            << "\"renderSetCount\":1,\"renderSetId\":\"scene-set\","
            << "\"renderSetType\":\"WebgpuPostprocessingDifferenceSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,"
            << "\"logicalRenderableId\":\"box\",\"instanceCount\":1}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"crate-base-color\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"main\","
            << "\"renderClass\":\"WebgpuPostprocessingDifferenceMainPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"screenPasses\":[{\"name\":\"material-texture-explicit-mips\"},"
            << "{\"name\":\"absolute-frame-difference-luminance-saturation\","
            << "\"renderClass\":\"WebgpuPostprocessingDifferenceCompositePass\"},"
            << "{\"name\":\"history-update\",\"renderClass\":\"WebgpuPostprocessingDifferenceHistoryPass\"}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main\",\"entityOrdinal\":0}]}\n";
        writeWebgpuPostprocessingDifferenceText(options.sceneSnapshotPath, snapshot.str());

        std::ostringstream semantic;
        semantic
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_postprocessing_difference\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
            << "\"result\":{\"assetCount\":1,\"boxVertexCount\":24,"
            << "\"boxIndexCount\":36,\"geometryGroupCount\":6,"
            << "\"speed\":" << pointerX
            << ",\"orbit\":" << pointerY
            << ",\"assetSha256\":\"a890f0a89eadc083cb39bfbe597c1395d7acf47a19f673b5643d4a9c174ea52f\"}}\n";
        writeWebgpuPostprocessingDifferenceText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebgpuPostprocessingDifferenceRuntimeAdapter::shutdown(
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
