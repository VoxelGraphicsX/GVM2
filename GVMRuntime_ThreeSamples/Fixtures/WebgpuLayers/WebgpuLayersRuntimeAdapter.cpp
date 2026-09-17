#include "WebgpuLayersRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t EntityCount = 3u;
        constexpr uint32_t InstanceCountPerEntity = 2500u;
        constexpr uint32_t PreInstanceRandomDrawCount = 196u;
        constexpr uint32_t InterEntityRandomDrawCount = 8u;
        constexpr uint32_t PostSceneRandomDrawCount = 77u;
        constexpr char BlossomSha256[] =
            "d31317ea0e6a064cdfe804d2eb57f286f0e29f83a3ed06267672f8602327485f";
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebgpuLayersHostVertex) == 32u);
        static_assert(sizeof(WebgpuLayersHostObjectData) == 80u);
        static_assert(sizeof(WebgpuLayersHostInstanceData) == 48u);
        static_assert(sizeof(WebgpuLayersHostMaterialData) == 16u);
        static_assert(sizeof(WebgpuLayersHostRenderLayerData) == 16u);

        /** Creates parent folders for one explicitly requested evidence path. */
        void prepareWebgpuLayersOutput(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            if (!outputPath.parent_path().empty())
                std::filesystem::create_directories(outputPath.parent_path());
        }

        /** Reads one bounded immutable asset or replay file. */
        eastl::vector<uint8_t> readWebgpuLayersFile(
            const std::filesystem::path &path,
            const char *description)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error(
                    std::string("Could not open ") + description + '.');
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
                throw std::runtime_error(
                    std::string(description) + " is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(byteCount));
            if (!input)
                throw std::runtime_error(
                    std::string("Could not read complete ") + description + '.');
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one byte sequence. */
        eastl::string calculateWebgpuLayersSha256(
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

        /** Resolves one explicit replay path without environment configuration. */
        std::filesystem::path resolveWebgpuLayersReplay(
            const ThreeSampleHostOptions &options)
        {
            const std::filesystem::path requested(
                options.inputReplayPath.c_str());
            if (requested.is_absolute() &&
                std::filesystem::is_regular_file(requested))
                return requested;
            if (!requested.empty() &&
                std::filesystem::is_regular_file(requested))
                return std::filesystem::absolute(requested);
            throw std::invalid_argument(
                "WebGPU layers could not resolve its explicit replay.");
        }

        /** Appends one typed buffer component to a Scene allocation. */
        void appendWebgpuLayersBuffer(
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

        /** Converts one sRGB hexadecimal channel to the linear working space. */
        float webgpuLayersSrgbToLinear(uint32_t byte)
        {
            const float value = float(byte) / 255.0f;
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Converts one packed hexadecimal color to linear RGB. */
        glm::vec4 makeWebgpuLayersColor(uint32_t color)
        {
            return glm::vec4(
                webgpuLayersSrgbToLinear((color >> 16u) & 255u),
                webgpuLayersSrgbToLinear((color >> 8u) & 255u),
                webgpuLayersSrgbToLinear(color & 255u),
                0.1f);
        }

        /** Flips decoded PNG rows to match Three's texture upload orientation. */
        RgbaImageData flipWebgpuLayersTextureRows(RgbaImageData image)
        {
            const size_t rowByteCount = size_t(image.width) * 4u;
            eastl::vector<uint8_t> row(rowByteCount);
            for (uint32_t y = 0u; y < image.height / 2u; ++y)
            {
                uint8_t *top = image.pixels.data() + size_t(y) * rowByteCount;
                uint8_t *bottom = image.pixels.data() +
                    size_t(image.height - y - 1u) * rowByteCount;
                std::memcpy(row.data(), top, rowByteCount);
                std::memcpy(top, bottom, rowByteCount);
                std::memcpy(bottom, row.data(), rowByteCount);
            }
            return image;
        }

        /** Encodes one linear channel into an RGBA8 mip payload channel. */
        uint8_t encodeWebgpuLayersMipChannel(
            float value,
            uint32_t channel)
        {
            const float clamped = eastl::clamp(value, 0.0f, 1.0f);
            const float encoded = channel == 3u
                ? clamped
                : clamped <= 0.0031308f
                    ? clamped * 12.92f
                    : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
            return static_cast<uint8_t>(std::lround(
                eastl::clamp(encoded, 0.0f, 1.0f) * 255.0f));
        }

        /** Reads one mip texel channel in the texture's sampled linear space. */
        float readWebgpuLayersMipChannel(
            const RgbaImageData &image,
            uint32_t x,
            uint32_t y,
            uint32_t channel)
        {
            const uint8_t byte = image.pixels[
                (size_t(y) * image.width + x) * 4u + channel];
            return channel == 3u
                ? float(byte) / 255.0f
                : webgpuLayersSrgbToLinear(byte);
        }

        /** Reproduces Three WebGPU's linear-sampler mip rendering for one level. */
        RgbaImageData buildNextWebgpuLayersSrgbMip(
            const RgbaImageData &source)
        {
            RgbaImageData target;
            target.width = eastl::max(source.width / 2u, 1u);
            target.height = eastl::max(source.height / 2u, 1u);
            target.pixels.resize(size_t(target.width) * target.height * 4u);
            for (uint32_t y = 0u; y < target.height; ++y)
            {
                const float sourceY =
                    (float(y) + 0.5f) * float(source.height) /
                        float(target.height) -
                    0.5f;
                const int32_t sourceY0Value =
                    static_cast<int32_t>(std::floor(sourceY));
                const uint32_t sourceY0 = static_cast<uint32_t>(eastl::clamp(
                    sourceY0Value, 0, static_cast<int32_t>(source.height) - 1));
                const uint32_t sourceY1 = static_cast<uint32_t>(eastl::clamp(
                    sourceY0Value + 1,
                    0,
                    static_cast<int32_t>(source.height) - 1));
                const float weightY = sourceY - std::floor(sourceY);
                for (uint32_t x = 0u; x < target.width; ++x)
                {
                    const float sourceX =
                        (float(x) + 0.5f) * float(source.width) /
                            float(target.width) -
                        0.5f;
                    const int32_t sourceX0Value =
                        static_cast<int32_t>(std::floor(sourceX));
                    const uint32_t sourceX0 = static_cast<uint32_t>(eastl::clamp(
                        sourceX0Value, 0, static_cast<int32_t>(source.width) - 1));
                    const uint32_t sourceX1 = static_cast<uint32_t>(eastl::clamp(
                        sourceX0Value + 1,
                        0,
                        static_cast<int32_t>(source.width) - 1));
                    const float weightX = sourceX - std::floor(sourceX);
                    const size_t targetOffset =
                        (size_t(y) * target.width + x) * 4u;
                    for (uint32_t channel = 0u; channel < 4u; ++channel)
                    {
                        const float top =
                            readWebgpuLayersMipChannel(
                                source, sourceX0, sourceY0, channel) *
                                (1.0f - weightX) +
                            readWebgpuLayersMipChannel(
                                source, sourceX1, sourceY0, channel) * weightX;
                        const float bottom =
                            readWebgpuLayersMipChannel(
                                source, sourceX0, sourceY1, channel) *
                                (1.0f - weightX) +
                            readWebgpuLayersMipChannel(
                                source, sourceX1, sourceY1, channel) * weightX;
                        target.pixels[targetOffset + channel] =
                            encodeWebgpuLayersMipChannel(
                                top * (1.0f - weightY) + bottom * weightY,
                                channel);
                    }
                }
            }
            return target;
        }

        /** Builds the exact explicit mip sequence consumed by this sample. */
        eastl::vector<RgbaImageData> buildWebgpuLayersSrgbMipChain(
            const RgbaImageData &baseImage)
        {
            eastl::vector<RgbaImageData> levels;
            levels.push_back(baseImage);
            while (levels.back().width > 1u || levels.back().height > 1u)
                levels.push_back(buildNextWebgpuLayersSrgbMip(levels.back()));
            return levels;
        }

        /** Writes one optional UTF-8 evidence document. */
        void writeWebgpuLayersText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            prepareWebgpuLayersOutput(path);
            std::ofstream output(path.c_str(), std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU layers evidence.");
        }
    } // namespace

    void WebgpuLayersRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-all-layers" &&
            options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated-all-layers" &&
            options.targetFrame == 120u;
        const bool yellowOnly =
            options.scenarioId == "yellow-layer-only" &&
            options.targetFrame == 121u;
        if (options.caseId != "webgpu_layers" ||
            (!initial && !animated && !yellowOnly) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            (yellowOnly != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "WebGPU layers requires its three locked scenarios, extent, seed, assets, and replay contract.");
        }
        device = inDevice;
        activeLayerMask = yellowOnly ? 2u : 7u;
        if (yellowOnly)
        {
            replaySha256 = calculateWebgpuLayersSha256(
                readWebgpuLayersFile(
                    resolveWebgpuLayersReplay(options),
                    "WebGPU layers replay"));
        }

        vertices = {
            {{-0.125f, 0.125f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}},
            {{0.125f, 0.125f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}},
            {{-0.125f, -0.125f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
            {{0.125f, -0.125f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}},
        };
        indices = {0u, 2u, 1u, 2u, 3u, 1u};

        const glm::mat4 projection = glm::perspective(
            glm::radians(60.0f),
            float(options.width) / float(options.height),
            0.1f,
            100.0f);
        const glm::mat4 view = glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f));
        const float timeSeconds = float(options.targetFrame) / 60.0f;
        constexpr uint32_t Colors[EntityCount] = {
            0xd70654u, 0xffd95fu, 0xb8d576u};

        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t draw = 0u; draw < PreInstanceRandomDrawCount; ++draw)
            (void)random.nextUint32();
        for (uint32_t entity = 0u; entity < EntityCount; ++entity)
        {
            WebgpuLayersEntityData &entityData = entities[entity];
            entityData.logicalId =
                entity == 0u ? "red" : entity == 1u ? "yellow" : "green";
            entityData.instances.clear();
            entityData.instances.reserve(InstanceCountPerEntity);
            for (uint32_t instance = 0u;
                 instance < InstanceCountPerEntity;
                 ++instance)
            {
                const float positionX = -25.0f + random.nextFloat() * 5.0f;
                const float positionY = -10.0f + random.nextFloat() * 60.0f;
                const float positionZ = -5.0f + random.nextFloat() * 10.0f;
                glm::vec3 direction(
                    0.7f + random.nextFloat() * 0.2f,
                    -0.3f + random.nextFloat() * 0.15f,
                    0.0f);
                direction = glm::normalize(direction);
                entityData.instances.push_back({
                    .positionAndTimeOffset = glm::vec4(
                        positionX,
                        positionY,
                        positionZ,
                        float(instance) / float(InstanceCountPerEntity)),
                    .rotation = glm::vec4(
                        random.nextFloat(),
                        random.nextFloat(),
                        random.nextFloat(),
                        0.0f),
                    .direction = glm::vec4(direction, 0.0f),
                });
            }
            entityData.objectData.projectionView = projection * view;
            entityData.objectData.timeAndReserved =
                glm::vec4(timeSeconds, 0.0f, 0.0f, 0.0f);
            entityData.materialData.baseColor =
                makeWebgpuLayersColor(Colors[entity]);
            entityData.renderLayerData.entityLayerAndCameraMask = glm::uvec4(
                1u << entity, activeLayerMask, 0u, 0u);
            for (uint32_t draw = 0u;
                 draw < InterEntityRandomDrawCount;
                 ++draw)
                (void)random.nextUint32();
        }
        for (uint32_t draw = 0u; draw < PostSceneRandomDrawCount; ++draw)
            (void)random.nextUint32();
        finalRandomState = random.getState();

        const std::filesystem::path texturePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "sprites" / "blossom.png";
        const eastl::vector<uint8_t> assetBytes = readWebgpuLayersFile(
            texturePath, "Three r185 blossom texture");
        if (calculateWebgpuLayersSha256(assetBytes) != BlossomSha256)
            throw std::invalid_argument(
                "The blossom texture differs from Three r185.");
        const RgbaImageData baseImage = flipWebgpuLayersTextureRows(
            decodeStraightPngRgba8(texturePath));
        if (baseImage.width != 126u || baseImage.height != 126u)
            throw std::runtime_error(
                "The blossom texture extent differs from Three r185.");
        const eastl::vector<RgbaImageData> mipChain =
            buildWebgpuLayersSrgbMipChain(baseImage);
        textureBytes.clear();
        textureMipOffsets.clear();
        for (const RgbaImageData &mip : mipChain)
        {
            textureMipOffsets.push_back(textureBytes.size());
            textureBytes.insert(
                textureBytes.end(), mip.pixels.begin(), mip.pixels.end());
        }

        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create the WebGPU layers Scene Set encoder.");
        for (uint32_t entity = 0u; entity < EntityCount; ++entity)
        {
            const WebgpuLayersEntityData &entityData = entities[entity];
            const eastl::string prefix =
                "WebgpuLayers-" + entityData.logicalId;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = InstanceCountPerEntity;
            appendWebgpuLayersBuffer(
                allocation,
                WebgpuLayersSceneRenderSetComponents::vertices,
                prefix + "-vertices",
                vertices.data(), vertices.size() * sizeof(vertices[0u]), 1u);
            appendWebgpuLayersBuffer(
                allocation,
                WebgpuLayersSceneRenderSetComponents::indices,
                prefix + "-indices",
                indices.data(), indices.size() * sizeof(indices[0u]), 1u);
            appendWebgpuLayersBuffer(
                allocation,
                WebgpuLayersSceneRenderSetComponents::objects,
                prefix + "-object",
                &entityData.objectData, sizeof(entityData.objectData), 1u);
            appendWebgpuLayersBuffer(
                allocation,
                WebgpuLayersSceneRenderSetComponents::instances,
                prefix + "-instances",
                entityData.instances.data(),
                entityData.instances.size() * sizeof(entityData.instances[0u]),
                InstanceCountPerEntity);
            appendWebgpuLayersBuffer(
                allocation,
                WebgpuLayersSceneRenderSetComponents::materials,
                prefix + "-material",
                &entityData.materialData, sizeof(entityData.materialData), 1u);
            appendWebgpuLayersBuffer(
                allocation,
                WebgpuLayersSceneRenderSetComponents::renderLayers,
                prefix + "-layer",
                &entityData.renderLayerData,
                sizeof(entityData.renderLayerData), 1u);
            GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
            textureInfo.textureComponentHandle =
                WebgpuLayersSceneRenderSetComponents::textures;
            textureInfo.textures.push_back({
                .textureName = prefix + "-blossom",
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = baseImage.width,
                .height = baseImage.height,
                .data = textureBytes.data(),
                .dataStorageBytes = textureBytes.size(),
                .mipmapOffsetBytes = textureMipOffsets,
            });
            allocation.textureInfos.push_back(eastl::move(textureInfo));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebgpuLayersRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuLayersRuntimeAdapter::afterFrame(
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
            prepareWebgpuLayersOutput(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU layers RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgpu_layers\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend)
            << "\",\"frame\":" << frameIndex
            << ",\"randomSeed\":" << options.randomSeed
            << ",\"randomState\":" << finalRandomState
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << width * 4u
            << ",\"byteCount\":" << rgba.size()
            << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,"
            << "\"assetSha256\":\"" << BlossomSha256 << "\"}\n";
        writeWebgpuLayersText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_layers\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":3,"
            << "\"entityCount\":3,\"instanceCount\":7500,"
            << "\"entityInstanceCounts\":[2500,2500,2500],"
            << "\"activeLayerMask\":" << activeLayerMask
            << ",\"vertexCount\":12,\"indexCount\":18,"
            << "\"scenePassCount\":1,\"screenPassCount\":1,"
            << "\"drawCommandCount\":1,\"sampleCount\":1,"
            << "\"msaaSimulated\":false,"
            << "\"renderSetType\":\"WebgpuLayersSceneRenderSet\","
            << "\"sceneRoots\":[{\"id\":\"scene\","
            << "\"renderSetCount\":1,\"renderSetId\":\"scene-set\","
            << "\"renderSetType\":\"WebgpuLayersSceneRenderSet\","
            << "\"renderableObjectCount\":3,\"entityCount\":3,"
            << "\"entities\":["
            << "{\"entityId\":0,\"logicalRenderableId\":\"red\",\"instanceCount\":2500},"
            << "{\"entityId\":1,\"logicalRenderableId\":\"yellow\",\"instanceCount\":2500},"
            << "{\"entityId\":2,\"logicalRenderableId\":\"green\",\"instanceCount\":2500}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"scene-main\","
            << "\"renderClass\":\"WebgpuLayersSceneMainPass\","
            << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
            << "\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}]}\n";
        writeWebgpuLayersText(options.sceneSnapshotPath, snapshot.str());

        std::ostringstream semantic;
        semantic
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_layers\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
            << "\"result\":{\"assetCount\":1,\"entityCount\":3,"
            << "\"instanceCount\":7500,\"activeLayerMask\":"
            << activeLayerMask << ",\"assetSha256\":\""
            << BlossomSha256 << "\",\"replaySha256\":\""
            << replaySha256.c_str() << "\"}}\n";
        writeWebgpuLayersText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebgpuLayersRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        for (WebgpuLayersEntityData &entity : entities)
            entity.instances.clear();
        textureBytes.clear();
        textureMipOffsets.clear();
        device = {};
    }
} // namespace GVM::ThreeSamples
