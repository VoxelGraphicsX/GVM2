#include "WebglSpritesRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <CommonCrypto/CommonDigest.h>

#include <glm/geometric.hpp>

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
        constexpr GVM::Core::RenderSetHandle WorldRenderSetHandle =
            ExportedRenderSet::worldSet;
        constexpr GVM::Core::RenderSetHandle HudRenderSetHandle =
            ExportedRenderSet::hudSet;
        constexpr uint32_t WorldSpriteCount = 200u;
        constexpr uint32_t HudSpriteCount = 5u;
        constexpr uint32_t PreSpriteRandomDrawCount = 128u;
        constexpr uint32_t MaterialCloneUuidDrawCount = 4u;
        constexpr uint32_t SpriteUuidDrawCount = 4u;
        constexpr uint32_t FirstSpriteGeometryRandomDrawCount = 8u;
        constexpr uint32_t RendererRandomDrawCount = 60u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr const char *TextureHashes[3u] = {
            "be7bd8a9708701d0a958eb602cc689ec329ab0af5482358fac23359721f58cf2",
            "ff09c4e044c8d33a115ae422ad5cb236bf4dc9742e6fd790d0006d3aa666ebbd",
            "fe94c53ea646aa465fb4f96b4fef2b3ed3f94d0006fe25f7987f00580e1e0356"};

        static_assert(sizeof(WebglSpritesHostVertex) == 16u);
        static_assert(sizeof(WebglSpritesHostObjectData) == 80u);
        static_assert(sizeof(WebglSpritesHostInstanceData) == 16u);
        static_assert(sizeof(WebglSpritesHostMaterialData) == 32u);
        static_assert(sizeof(WebglSpritesHostStateData) == 32u);

        /** Stores one logical world sprite before transparent allocation order. */
        struct WebglSpritesWorldRecord final
        {
            uint32_t ordinal = 0u;
            glm::dvec3 localPosition{};
            glm::dvec3 viewPosition{};
            glm::vec3 color{1.0f};
            uint32_t textureIndex = 1u;
        };

        /** Validates the two frozen WebGL sprite scenarios and host inputs. */
        void validateWebglSpritesScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            if (options.caseId != "webgl_sprites" ||
                (!initial && !animated) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty() || !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "WebGL sprites require the locked scenarios, extent, seed, assets, and no replay.");
            }
        }

        /** Reads one pinned sprite asset as exact bytes. */
        eastl::vector<uint8_t> readWebglSpritesAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open a WebGL sprite texture.");
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) >
                    std::numeric_limits<size_t>::max())
                throw std::runtime_error("A WebGL sprite texture has invalid size.");
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.seekg(0, std::ios::beg);
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input)
                throw std::runtime_error("Could not read a WebGL sprite texture.");
            return bytes;
        }

        /** Calculates one lowercase SHA-256 digest for asset locking. */
        eastl::string calculateWebglSpritesSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (const uint8_t value : digest)
                stream << std::setw(2) << unsigned(value);
            return eastl::string(stream.str().c_str());
        }

        /** Flips straight PNG rows to match Texture.flipY external upload. */
        RgbaImageData flipWebglSpritesRows(const RgbaImageData &source)
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

        /** Builds the generated-backend perspective projection. */
        glm::mat4 makeWebglSpritesPerspectiveProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 2100.0;
            const double top = NearDistance * std::tan(60.0 * Pi / 360.0);
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

        /** Builds the generated-backend 800x500 orthographic projection. */
        glm::mat4 makeWebglSpritesOrthographicProjection()
        {
            glm::mat4 result(1.0f);
            result[0u][0u] = 1.0f / 400.0f;
            result[1u][1u] = -1.0f / 250.0f;
            result[2u][2u] = -1.0f / 9.0f;
            result[3u][2u] = -1.0f / 9.0f;
            return result;
        }

        /** Converts one binary64 Three XYZ Euler rotation into a matrix. */
        glm::dmat4 makeWebglSpritesXyzRotation(
            double rotationX,
            double rotationY,
            double rotationZ)
        {
            const double cx = std::cos(rotationX * 0.5);
            const double cy = std::cos(rotationY * 0.5);
            const double cz = std::cos(rotationZ * 0.5);
            const double sx = std::sin(rotationX * 0.5);
            const double sy = std::sin(rotationY * 0.5);
            const double sz = std::sin(rotationZ * 0.5);
            const double qx = sx * cy * cz + cx * sy * sz;
            const double qy = cx * sy * cz - sx * cy * sz;
            const double qz = cx * cy * sz + sx * sy * cz;
            const double qw = cx * cy * cz - sx * sy * sz;
            const double x2 = qx + qx;
            const double y2 = qy + qy;
            const double z2 = qz + qz;
            const double xx = qx * x2;
            const double xy = qx * y2;
            const double xz = qx * z2;
            const double yy = qy * y2;
            const double yz = qy * z2;
            const double zz = qz * z2;
            const double wx = qw * x2;
            const double wy = qw * y2;
            const double wz = qw * z2;
            glm::dmat4 result(1.0);
            result[0u] = glm::dvec4(1.0 - yy - zz, xy + wz, xz - wy, 0.0);
            result[1u] = glm::dvec4(xy - wz, 1.0 - xx - zz, yz + wx, 0.0);
            result[2u] = glm::dvec4(xz + wy, yz - wx, 1.0 - xx - yy, 0.0);
            return result;
        }

        /** Evaluates the wrapped channel used by Three Color.setHSL. */
        double webglSpritesHueToRgb(double p, double q, double t)
        {
            if (t < 0.0) t += 1.0;
            if (t > 1.0) t -= 1.0;
            if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
            if (t < 0.5) return q;
            if (t < 2.0 / 3.0) return p + (q - p) * 6.0 * (2.0 / 3.0 - t);
            return p;
        }

        /** Converts the source HSL values directly to Three working RGB. */
        glm::vec3 makeWebglSpritesHslColor(double hue)
        {
            constexpr double Saturation = 0.75;
            constexpr double Lightness = 0.5;
            const double p = Lightness * (1.0 - Saturation);
            const double q = Lightness + Saturation - Lightness * Saturation;
            return glm::vec3(
                float(webglSpritesHueToRgb(p, q, hue + 1.0 / 3.0)),
                float(webglSpritesHueToRgb(p, q, hue)),
                float(webglSpritesHueToRgb(p, q, hue - 1.0 / 3.0)));
        }

        /** Creates parent directories for one requested evidence artifact. */
        void prepareWebglSpritesOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeWebglSpritesText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebglSpritesOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error("Could not write WebGL sprite evidence.");
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendWebglSpritesPayload(
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

        /** Allocates one complete Scene into the selected unique Set instance. */
        void allocateWebglSpritesScene(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::RenderSetHandle renderSetHandle,
            eastl::vector<WebglSpritesEntityState> &entities,
            const eastl::array<WebglSpritesTextureAsset, 3u> &textures,
            const char *prefix)
        {
            const WebglSpritesHostVertex quad[4u] = {
                {.cornerAndUv = {-0.5f, -0.5f, 0.0f, 0.0f}},
                {.cornerAndUv = {0.5f, -0.5f, 1.0f, 0.0f}},
                {.cornerAndUv = {0.5f, 0.5f, 1.0f, 1.0f}},
                {.cornerAndUv = {-0.5f, 0.5f, 0.0f, 1.0f}}};
            constexpr uint32_t QuadIndices[6u] = {0u, 1u, 2u, 0u, 2u, 3u};
            const auto encoder = renderer.createRenderSetCommandEncoder(
                renderSetHandle);
            if (!encoder)
                throw std::runtime_error("Could not create a WebGL sprite Set encoder.");
            for (uint32_t ordinal = 0u; ordinal < entities.size(); ++ordinal)
            {
                WebglSpritesEntityState &entity = entities[ordinal];
                entity.vertices.assign(quad, quad + 4u);
                entity.indices.assign(QuadIndices, QuadIndices + 6u);
                GVM::Core::RenderSetAllocInfo allocation;
                allocation.verticesCount = 4u;
                allocation.indicesCount = 6u;
                allocation.instanceCount = 1u;
                const std::string name =
                    std::string(prefix) + std::to_string(ordinal);
                appendWebglSpritesPayload(
                    allocation, WebglSpritesSceneRenderSetComponents::vertices,
                    (name + "Vertices").c_str(), entity.vertices.data(),
                    sizeof(WebglSpritesHostVertex) * 4u, 1u);
                appendWebglSpritesPayload(
                    allocation, WebglSpritesSceneRenderSetComponents::indices,
                    (name + "Indices").c_str(), entity.indices.data(),
                    sizeof(uint32_t) * 6u, 1u);
                appendWebglSpritesPayload(
                    allocation, WebglSpritesSceneRenderSetComponents::objects,
                    (name + "Object").c_str(), &entity.objectData,
                    sizeof(entity.objectData), 1u);
                appendWebglSpritesPayload(
                    allocation, WebglSpritesSceneRenderSetComponents::instances,
                    (name + "Instance").c_str(), &entity.instanceData,
                    sizeof(entity.instanceData), 1u);
                appendWebglSpritesPayload(
                    allocation, WebglSpritesSceneRenderSetComponents::materials,
                    (name + "Material").c_str(), &entity.materialData,
                    sizeof(entity.materialData), 1u);
                appendWebglSpritesPayload(
                    allocation, WebglSpritesSceneRenderSetComponents::spriteState,
                    (name + "State").c_str(), &entity.spriteState,
                    sizeof(entity.spriteState), 1u);
                const WebglSpritesTextureAsset &texture =
                    textures[entity.textureIndex];
                constexpr const char *TextureNames[3u] = {
                    "sprite0.png", "sprite1.png", "sprite2.png"};
                GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
                textureComponent.textureComponentHandle =
                    WebglSpritesSceneRenderSetComponents::textures;
                textureComponent.textures.push_back({
                    .textureName = TextureNames[entity.textureIndex],
                    .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                    .width = texture.width,
                    .height = texture.height,
                    .data = texture.bytes.data(),
                    .dataStorageBytes = texture.bytes.size(),
                    .mipmapOffsetBytes = texture.mipOffsets,
                });
                allocation.textureInfos.push_back(eastl::move(textureComponent));
                encoder->allocEntity(allocation);
            }
            renderer.executeRenderSetCommand(renderSetHandle, encoder);
        }
    }

    void WebglSpritesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebglSpritesScenario(options);
        device = inDevice;
        const std::filesystem::path textureRoot =
            std::filesystem::path(options.assetRoot.c_str()) / "textures";
        for (uint32_t textureIndex = 0u; textureIndex < textures.size(); ++textureIndex)
        {
            const std::filesystem::path texturePath =
                textureRoot / ("sprite" + std::to_string(textureIndex) + ".png");
            const eastl::vector<uint8_t> encoded =
                readWebglSpritesAsset(texturePath);
            if (calculateWebglSpritesSha256(encoded) != TextureHashes[textureIndex])
                throw std::runtime_error("A WebGL sprite asset differs from r185.");
            const RgbaImageData baseImage = flipWebglSpritesRows(
                decodeStraightPngRgba8(texturePath));
            if (baseImage.width != 128u || baseImage.height != 128u)
                throw std::runtime_error("WebGL sprite assets must remain 128x128.");
            WebglSpritesTextureAsset &asset = textures[textureIndex];
            asset.width = baseImage.width;
            asset.height = baseImage.height;
            const eastl::vector<RgbaImageData> mipChain =
                buildSrgbMipChain(baseImage);
            for (const RgbaImageData &mip : mipChain)
            {
                asset.mipOffsets.push_back(asset.bytes.size());
                asset.bytes.insert(
                    asset.bytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
        }

        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t draw = 0u; draw < PreSpriteRandomDrawCount; ++draw)
            (void)random.nextUint32();
        eastl::vector<WebglSpritesWorldRecord> records;
        records.reserve(WorldSpriteCount);
        for (uint32_t ordinal = 0u; ordinal < WorldSpriteCount; ++ordinal)
        {
            const double x = double(random.nextFloat()) - 0.5;
            const double y = double(random.nextFloat()) - 0.5;
            const double z = double(random.nextFloat()) - 0.5;
            WebglSpritesWorldRecord record;
            record.ordinal = ordinal;
            record.localPosition = glm::normalize(glm::dvec3(x, y, z)) * 500.0;
            record.textureIndex = z < 0.0 ? 1u : 2u;
            for (uint32_t draw = 0u; draw < MaterialCloneUuidDrawCount; ++draw)
                (void)random.nextUint32();
            if (record.textureIndex == 2u)
                record.color = makeWebglSpritesHslColor(
                    0.5 * double(random.nextFloat()));
            for (uint32_t draw = 0u; draw < SpriteUuidDrawCount; ++draw)
                (void)random.nextUint32();
            if (ordinal == 0u)
            {
                for (uint32_t draw = 0u;
                     draw < FirstSpriteGeometryRandomDrawCount;
                     ++draw)
                    (void)random.nextUint32();
            }
            records.push_back(record);
        }
        for (uint32_t draw = 0u; draw < RendererRandomDrawCount; ++draw)
            (void)random.nextUint32();
        finalRandomState = random.getState();

        const double time =
            (ReferenceEpochMilliseconds +
             double(options.targetFrame) * FrameStepMilliseconds) / 1000.0;
        const glm::dmat4 groupMatrix = makeWebglSpritesXyzRotation(
            time * 0.5, time * 0.75, time);
        for (WebglSpritesWorldRecord &record : records)
        {
            const glm::dvec3 worldPosition = glm::dvec3(
                groupMatrix * glm::dvec4(record.localPosition, 1.0));
            record.viewPosition =
                worldPosition - glm::dvec3(0.0, 0.0, 1500.0);
        }
        eastl::sort(
            records.begin(), records.end(),
            [](const WebglSpritesWorldRecord &left,
               const WebglSpritesWorldRecord &right)
            {
                return left.viewPosition.z < right.viewPosition.z;
            });

        const glm::mat4 perspective = makeWebglSpritesPerspectiveProjection();
        worldEntities.resize(WorldSpriteCount);
        for (uint32_t sortedOrdinal = 0u;
             sortedOrdinal < records.size();
             ++sortedOrdinal)
        {
            const WebglSpritesWorldRecord &record = records[sortedOrdinal];
            WebglSpritesEntityState &entity = worldEntities[sortedOrdinal];
            const double sine = std::sin(
                time + record.localPosition.x * 0.01);
            const float scale = float(sine * 0.3 + 1.0) * 128.0f;
            const float opacity = record.textureIndex == 2u
                ? 1.0f
                : float(sine * 0.4 + 0.6);
            entity.objectData = {
                .projection = perspective,
                .viewCenter = glm::vec4(glm::vec3(record.viewPosition), 1.0f)};
            entity.instanceData = {.reserved = glm::vec4(0.0f)};
            entity.materialData = {
                .colorAndOpacity = glm::vec4(record.color, opacity),
                .uvScaleAndOffset = record.textureIndex == 2u
                    ? glm::vec4(2.0f, 2.0f, -0.5f, -0.5f)
                    : glm::vec4(1.0f, 1.0f, 0.0f, 0.0f)};
            entity.spriteState = {
                .rotationScaleCenterX = glm::vec4(
                    float(options.targetFrame + 1u) * 0.1f *
                        float(record.ordinal) / float(WorldSpriteCount),
                    scale, scale, 0.5f),
                .centerYVisibilityFog = glm::vec4(0.5f, 1.0f, 1.0f, 0.0f)};
            entity.textureIndex = record.textureIndex;
        }

        const glm::mat4 orthographic = makeWebglSpritesOrthographicProjection();
        const glm::vec2 positions[HudSpriteCount] = {
            {-400.0f, 250.0f}, {400.0f, 250.0f},
            {-400.0f, -250.0f}, {400.0f, -250.0f}, {0.0f, 0.0f}};
        const glm::vec2 centers[HudSpriteCount] = {
            {0.0f, 1.0f}, {1.0f, 1.0f},
            {0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 0.5f}};
        hudEntities.resize(HudSpriteCount);
        for (uint32_t ordinal = 0u; ordinal < HudSpriteCount; ++ordinal)
        {
            WebglSpritesEntityState &entity = hudEntities[ordinal];
            entity.objectData = {
                .projection = orthographic,
                .viewCenter = glm::vec4(positions[ordinal], -9.0f, 1.0f)};
            entity.instanceData = {.reserved = glm::vec4(0.0f)};
            entity.materialData = {
                .colorAndOpacity = glm::vec4(1.0f),
                .uvScaleAndOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f)};
            entity.spriteState = {
                .rotationScaleCenterX = glm::vec4(
                    0.0f, 128.0f, 128.0f, centers[ordinal].x),
                .centerYVisibilityFog = glm::vec4(
                    centers[ordinal].y, 1.0f, 0.0f, 0.0f)};
            entity.textureIndex = 0u;
        }

        allocateWebglSpritesScene(
            renderer, WorldRenderSetHandle, worldEntities, textures, "WorldSprite");
        allocateWebglSpritesScene(
            renderer, HudRenderSetHandle, hudEntities, textures, "HudSprite");
    }

    void WebglSpritesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglSpritesRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
            prepareWebglSpritesOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error("Could not write WebGL sprite RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgl_sprites\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\"}\n";
        writeWebglSpritesText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgl_sprites\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":2,\"renderableObjectCount\":205,"
            << "\"entityCount\":205,\"instanceCount\":205,"
            << "\"vertexCount\":820,\"indexCount\":1230,"
            << "\"scenePassCount\":2,\"screenPassCount\":1,"
            << "\"drawCommandCount\":2,\"renderSetType\":"
            << "\"WebglSpritesSceneRenderSet\",\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],"
            << "\"sceneRoots\":[";
        const uint32_t counts[2u] = {WorldSpriteCount, HudSpriteCount};
        const char *rootNames[2u] = {"scene", "sceneOrtho"};
        const char *renderSetIds[2u] = {"world-set", "hud-set"};
        const char *entityPrefixes[2u] = {"world-sprite-", "hud-sprite-"};
        const char *passNames[2u] = {"world-transparent", "hud-transparent"};
        const char *renderClasses[2u] = {
            "WebglSpritesWorldPass", "WebglSpritesHudPass"};
        for (uint32_t root = 0u; root < 2u; ++root)
        {
            if (root != 0u) snapshot << ',';
            snapshot << "{\"id\":\"" << rootNames[root]
                     << "\",\"renderSetCount\":1,\"renderSetId\":\""
                     << renderSetIds[root] << "\",\"renderSetType\":"
                     << "\"WebglSpritesSceneRenderSet\",\"entityCount\":"
                     << counts[root] << ",\"renderableObjectCount\":"
                     << counts[root] << ",\"entities\":[";
            for (uint32_t ordinal = 0u; ordinal < counts[root]; ++ordinal)
            {
                if (ordinal != 0u) snapshot << ',';
                snapshot << "{\"entityId\":" << ordinal
                         << ",\"logicalRenderableId\":\""
                         << entityPrefixes[root] << ordinal
                         << "\",\"instanceCount\":1}";
            }
            snapshot
                << "],\"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],"
                << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
                << "\"scenePasses\":[{\"name\":\"" << passNames[root]
                << "\",\"renderClass\":\"" << renderClasses[root]
                << "\",\"renderSetId\":\"" << renderSetIds[root]
                << "\",\"renderSetBindingCount\":1,"
                << "\"drawMode\":\"render-set-indexed-indirect\","
                << "\"invocationCount\":1,\"drawCommandCount\":1,"
                << "\"usesStandaloneGeometry\":false,"
                << "\"usesExplicitDrawCount\":false}]}";
        }
        snapshot
            << "],\"scenePassSequence\":["
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"world-transparent\",\"entityOrdinal\":0},"
            << "{\"sceneRoot\":\"sceneOrtho\",\"scenePass\":\"hud-transparent\",\"entityOrdinal\":0}],"
            << "\"finalRandomState\":" << finalRandomState << "}\n";
        writeWebglSpritesText(options.sceneSnapshotPath, snapshot.str());
        writeWebglSpritesText(options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebglSpritesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        worldEntities.clear();
        hudEntities.clear();
        for (WebglSpritesTextureAsset &texture : textures)
        {
            texture.bytes.clear();
            texture.mipOffsets.clear();
        }
        device = {};
    }
}
