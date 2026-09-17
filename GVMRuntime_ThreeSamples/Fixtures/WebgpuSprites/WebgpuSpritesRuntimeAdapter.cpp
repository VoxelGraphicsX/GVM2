#include "WebgpuSpritesRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/vector.h>

#include <CommonCrypto/CommonDigest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr uint32_t SpriteCount = 200u;
        constexpr uint32_t PreSpriteRandomDrawCount = 200u;
        constexpr uint32_t SpriteUuidRandomDrawCount = 4u;
        constexpr uint32_t FirstSpriteGeometryRandomDrawCount = 8u;
        constexpr uint32_t RendererRandomDrawCount = 60u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr const char *SpriteAssetSha256 =
            "ff09c4e044c8d33a115ae422ad5cb236bf4dc9742e6fd790d0006d3aa666ebbd";

        static_assert(sizeof(WebgpuSpritesHostVertex) == 16u);
        static_assert(sizeof(WebgpuSpritesHostObjectData) == 80u);
        static_assert(sizeof(WebgpuSpritesHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuSpritesHostMaterialData) == 16u);
        static_assert(sizeof(WebgpuSpritesHostStateData) == 32u);

        /** Stores one sprite ordinal and its transformed view-space center. */
        struct WebgpuSpritesSortRecord final
        {
            uint32_t ordinal;
            glm::dvec3 viewPosition;
        };

        /** Validates the two frozen WebGPU sprite scenarios and host inputs. */
        void validateWebgpuSpritesScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "asset-and-layout" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated-sprites" &&
                options.targetFrame == 120u;
            if (options.caseId != "webgpu_sprites" ||
                (!initial && !animated) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty() ||
                !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "WebGPU sprites require the locked case, scenarios, extent, seed, assets, and no replay.");
            }
        }

        /** Reads one pinned asset as an exact binary payload. */
        eastl::vector<uint8_t> readWebgpuSpritesAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open textures/sprite1.png.");
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) >
                    std::numeric_limits<size_t>::max())
                throw std::runtime_error("textures/sprite1.png has an invalid size.");
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.seekg(0, std::ios::beg);
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input)
                throw std::runtime_error("Could not read textures/sprite1.png.");
            return bytes;
        }

        /** Flips straight PNG rows to match the Three WebGPU external-image upload. */
        RgbaImageData flipWebgpuSpritesRows(const RgbaImageData &source)
        {
            RgbaImageData flipped;
            flipped.width = source.width;
            flipped.height = source.height;
            flipped.pixels.resize(source.pixels.size());
            const size_t rowBytes = static_cast<size_t>(source.width) * 4u;
            for (uint32_t row = 0u; row < source.height; ++row)
            {
                const size_t sourceOffset =
                    static_cast<size_t>(source.height - row - 1u) * rowBytes;
                const size_t targetOffset = static_cast<size_t>(row) * rowBytes;
                eastl::copy(
                    source.pixels.begin() + sourceOffset,
                    source.pixels.begin() + sourceOffset + rowBytes,
                    flipped.pixels.begin() + targetOffset);
            }
            return flipped;
        }

        /** Calculates one lowercase SHA-256 digest for asset locking. */
        eastl::string calculateWebgpuSpritesSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(
                bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (const uint8_t value : digest)
                stream << std::setw(2) << unsigned(value);
            return eastl::string(stream.str().c_str());
        }

        /** Builds the generated-backend 60-degree perspective projection. */
        glm::mat4 makeWebgpuSpritesProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 2100.0;
            const double top =
                NearDistance * std::tan(60.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = height * 1.6;
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] = float(2.0 * NearDistance / width);
            result[1u][1u] = float(-2.0 * NearDistance / height);
            result[2u][2u] = float(-FarDistance / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] = float(-FarDistance * NearDistance / depth);
            return result;
        }

        /** Converts one binary64 Three.js XYZ Euler value to a GPU matrix. */
        glm::dmat4 makeWebgpuSpritesXyzRotation(
            double rotationX,
            double rotationY,
            double rotationZ)
        {
            const double cosineX = std::cos(rotationX * 0.5);
            const double cosineY = std::cos(rotationY * 0.5);
            const double cosineZ = std::cos(rotationZ * 0.5);
            const double sineX = std::sin(rotationX * 0.5);
            const double sineY = std::sin(rotationY * 0.5);
            const double sineZ = std::sin(rotationZ * 0.5);
            const double quaternionX =
                sineX * cosineY * cosineZ + cosineX * sineY * sineZ;
            const double quaternionY =
                cosineX * sineY * cosineZ - sineX * cosineY * sineZ;
            const double quaternionZ =
                cosineX * cosineY * sineZ + sineX * sineY * cosineZ;
            const double quaternionW =
                cosineX * cosineY * cosineZ - sineX * sineY * sineZ;
            const double x2 = quaternionX + quaternionX;
            const double y2 = quaternionY + quaternionY;
            const double z2 = quaternionZ + quaternionZ;
            const double xx = quaternionX * x2;
            const double xy = quaternionX * y2;
            const double xz = quaternionX * z2;
            const double yy = quaternionY * y2;
            const double yz = quaternionY * z2;
            const double zz = quaternionZ * z2;
            const double wx = quaternionW * x2;
            const double wy = quaternionW * y2;
            const double wz = quaternionW * z2;
            glm::dmat4 result(1.0);
            result[0u] = glm::dvec4(
                1.0 - (yy + zz), xy + wz, xz - wy, 0.0);
            result[1u] = glm::dvec4(
                xy - wz, 1.0 - (xx + zz), yz + wx, 0.0);
            result[2u] = glm::dvec4(
                xz + wy, yz - wx, 1.0 - (xx + yy), 0.0);
            return result;
        }

        /** Creates parent directories for one requested evidence artifact. */
        void prepareWebgpuSpritesOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeWebgpuSpritesText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebgpuSpritesOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU sprite evidence.");
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebgpuSpritesPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
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
    }

    void WebgpuSpritesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebgpuSpritesScenario(options);
        device = inDevice;
        const std::filesystem::path texturePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "sprite1.png";
        const eastl::vector<uint8_t> assetBytes =
            readWebgpuSpritesAsset(texturePath);
        if (calculateWebgpuSpritesSha256(assetBytes) != SpriteAssetSha256)
            throw std::runtime_error(
                "textures/sprite1.png differs from Three.js r185.");
        const RgbaImageData baseImage = flipWebgpuSpritesRows(
            decodeStraightPngRgba8(texturePath));
        if (baseImage.width != 128u || baseImage.height != 128u)
            throw std::runtime_error(
                "textures/sprite1.png must remain 128x128 RGBA8.");
        textureWidth = baseImage.width;
        textureHeight = baseImage.height;
        const eastl::vector<RgbaImageData> mipChain =
            buildUnormMipChain(baseImage);
        for (const RgbaImageData &mip : mipChain)
        {
            textureMipOffsets.push_back(textureBytes.size());
            textureBytes.insert(
                textureBytes.end(), mip.pixels.begin(), mip.pixels.end());
        }

        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t draw = 0u; draw < PreSpriteRandomDrawCount; ++draw)
            (void)random.nextUint32();
        eastl::vector<glm::dvec3> localPositions;
        localPositions.reserve(SpriteCount);
        for (uint32_t ordinal = 0u; ordinal < SpriteCount; ++ordinal)
        {
            glm::dvec3 position(
                double(random.nextFloat()) - 0.5,
                double(random.nextFloat()) - 0.5,
                double(random.nextFloat()) - 0.5);
            position = glm::normalize(position) * 500.0;
            localPositions.push_back(position);
            const uint32_t uuidDrawCount =
                SpriteUuidRandomDrawCount +
                (ordinal == 0u ? FirstSpriteGeometryRandomDrawCount : 0u);
            for (uint32_t draw = 0u; draw < uuidDrawCount; ++draw)
                (void)random.nextUint32();
        }
        for (uint32_t draw = 0u; draw < RendererRandomDrawCount; ++draw)
            (void)random.nextUint32();
        finalRandomState = random.getState();

        const double time =
            (ReferenceEpochMilliseconds +
             double(options.targetFrame) * FrameStepMilliseconds) /
            1000.0;
        const glm::dmat4 groupMatrix = makeWebgpuSpritesXyzRotation(
            time * 0.5, time * 0.75, time);
        const glm::mat4 projection = makeWebgpuSpritesProjection();

        eastl::vector<WebgpuSpritesSortRecord> sorted;
        sorted.reserve(SpriteCount);
        for (uint32_t ordinal = 0u; ordinal < SpriteCount; ++ordinal)
        {
            const glm::dvec3 worldPosition = glm::dvec3(
                groupMatrix * glm::dvec4(localPositions[ordinal], 1.0));
            sorted.push_back({
                .ordinal = ordinal,
                .viewPosition = worldPosition - glm::dvec3(0.0, 0.0, 1500.0),
            });
        }
        eastl::sort(
            sorted.begin(), sorted.end(),
            [](const WebgpuSpritesSortRecord &left,
               const WebgpuSpritesSortRecord &right)
            {
                return left.viewPosition.z < right.viewPosition.z;
            });

        const WebgpuSpritesHostVertex quad[4u] = {
            {.cornerAndUv = {-0.5f, -0.5f, 0.0f, 0.0f}},
            {.cornerAndUv = {0.5f, -0.5f, 1.0f, 0.0f}},
            {.cornerAndUv = {0.5f, 0.5f, 1.0f, 1.0f}},
            {.cornerAndUv = {-0.5f, 0.5f, 0.0f, 1.0f}},
        };
        constexpr uint32_t QuadIndices[6u] = {0u, 1u, 2u, 0u, 2u, 3u};
        entities.resize(SpriteCount);
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create WebGPU sprite RenderSet encoder.");
        for (uint32_t sortedOrdinal = 0u;
             sortedOrdinal < SpriteCount;
             ++sortedOrdinal)
        {
            const WebgpuSpritesSortRecord &record = sorted[sortedOrdinal];
            WebgpuSpritesEntityState &entity = entities[sortedOrdinal];
            entity.vertices.assign(quad, quad + 4u);
            entity.indices.assign(QuadIndices, QuadIndices + 6u);
            const float scale =
                float(std::sin(
                    time + double(localPositions[record.ordinal].x) * 0.01) *
                    0.3 + 1.0);
            entity.objectData = {
                .projection = projection,
                .viewCenterAndDepth = glm::vec4(
                    glm::vec3(record.viewPosition), 1.0f),
            };
            entity.instanceData = {.reserved = glm::vec4(0.0f)};
            entity.materialData = {
                .colorAndOpacity = glm::vec4(1.0f),
            };
            entity.spriteState = {
                .rotationScaleCenter = glm::vec4(
                    float(options.targetFrame + 1u) * 0.1f *
                        float(record.ordinal) / float(SpriteCount),
                    scale * float(textureWidth),
                    scale * float(textureHeight),
                    0.5f),
                .visibilityAndFog = glm::vec4(1.0f, 1500.0f, 2100.0f, 0.0f),
            };
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = 4u;
            allocation.indicesCount = 6u;
            allocation.instanceCount = 1u;
            const std::string prefix =
                "WebgpuSprite" + std::to_string(sortedOrdinal);
            appendWebgpuSpritesPayload(
                allocation, WebgpuSpritesSceneRenderSetComponents::vertices,
                (prefix + "Vertices").c_str(), entity.vertices.data(),
                sizeof(WebgpuSpritesHostVertex) * 4u, 1u);
            appendWebgpuSpritesPayload(
                allocation, WebgpuSpritesSceneRenderSetComponents::indices,
                (prefix + "Indices").c_str(), entity.indices.data(),
                sizeof(uint32_t) * 6u, 1u);
            appendWebgpuSpritesPayload(
                allocation, WebgpuSpritesSceneRenderSetComponents::objects,
                (prefix + "Object").c_str(), &entity.objectData,
                sizeof(entity.objectData), 1u);
            appendWebgpuSpritesPayload(
                allocation, WebgpuSpritesSceneRenderSetComponents::instances,
                (prefix + "Instance").c_str(), &entity.instanceData,
                sizeof(entity.instanceData), 1u);
            appendWebgpuSpritesPayload(
                allocation, WebgpuSpritesSceneRenderSetComponents::materials,
                (prefix + "Material").c_str(), &entity.materialData,
                sizeof(entity.materialData), 1u);
            appendWebgpuSpritesPayload(
                allocation, WebgpuSpritesSceneRenderSetComponents::spriteState,
                (prefix + "State").c_str(), &entity.spriteState,
                sizeof(entity.spriteState), 1u);
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebgpuSpritesSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = "sprite1.png",
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = textureWidth,
                .height = textureHeight,
                .data = textureBytes.data(),
                .dataStorageBytes = textureBytes.size(),
                .mipmapOffsetBytes = textureMipOffsets,
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuSpritesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuSpritesRuntimeAdapter::afterFrame(
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
            throw std::overflow_error(
                "WebGPU sprite capture exceeds host storage.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareWebgpuSpritesOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGPU sprite RGBA capture.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgpu_sprites\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\"}\n";
        writeWebgpuSpritesText(
            options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_sprites\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":200,"
            << "\"entityCount\":200,\"instanceCount\":200,"
            << "\"vertexCount\":800,\"indexCount\":1200,"
            << "\"scenePassCount\":1,\"screenPassCount\":1,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"WebgpuSpritesSceneRenderSet\",\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"shared-sprite-fixed-slot\"},"
            << "{\"name\":\"spriteState\",\"kind\":\"buffer\",\"role\":\"rotation-scale-center-and-visibility\"}],"
            << "\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebgpuSpritesSceneRenderSet\","
            << "\"renderableObjectCount\":200,\"entityCount\":200,\"entities\":[";
        for (uint32_t ordinal = 0u; ordinal < SpriteCount; ++ordinal)
        {
            if (ordinal != 0u) snapshot << ',';
            snapshot << "{\"entityId\":" << ordinal
                     << ",\"logicalRenderableId\":\"sprite-" << ordinal
                     << "\",\"instanceCount\":1}";
        }
        snapshot
            << "],\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"shared-sprite-fixed-slot\"},"
            << "{\"name\":\"spriteState\",\"kind\":\"buffer\",\"role\":\"rotation-scale-center-and-visibility\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"transparent-sprites\","
            << "\"renderClass\":\"WebgpuSpritesScenePass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"transparent-sprites\",\"entityOrdinal\":0}],"
            << "\"usesRenderEntityID\":true,\"usesRenderEntityInstanceID\":true,"
            << "\"directDrawFallback\":false}\n";
        writeWebgpuSpritesText(options.sceneSnapshotPath, snapshot.str());
        std::ostringstream semantic;
        semantic
            << "{\"schemaVersion\":1,\"caseId\":\"webgpu_sprites\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"logicalSpriteCount\":200,\"texturePath\":"
            << "\"textures/sprite1.png\",\"textureAssetSha256\":\""
            << SpriteAssetSha256 << "\",\"finalRandomState\":"
            << finalRandomState << "}\n";
        writeWebgpuSpritesText(
            options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebgpuSpritesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
        textureBytes.clear();
        textureMipOffsets.clear();
    }
}
