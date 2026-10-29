#include "WebglPointsSpritesRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>

#include <glm/vec3.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t PointCount = 10000u;
        constexpr uint32_t PreGeometryRandomDrawCount = 128u;
        constexpr uint32_t ObjectRandomDrawCount = 8u;
        constexpr uint32_t RendererRandomDrawCount = 36u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr const char *TextureNames[5u] = {
            "snowflake2.png",
            "snowflake3.png",
            "snowflake1.png",
            "snowflake5.png",
            "snowflake4.png",
        };
        constexpr double BaseHue[5u] = {1.0, 0.95, 0.90, 0.85, 0.80};
        constexpr double Saturation[5u] = {0.2, 0.1, 0.05, 0.0, 0.0};
        constexpr float PointSize[5u] = {20.0f, 15.0f, 10.0f, 8.0f, 5.0f};
        constexpr const char *ObjectBufferNames[5u] = {
            "WebglPointsSpritesObject0",
            "WebglPointsSpritesObject1",
            "WebglPointsSpritesObject2",
            "WebglPointsSpritesObject3",
            "WebglPointsSpritesObject4",
        };
        constexpr const char *MaterialBufferNames[5u] = {
            "WebglPointsSpritesMaterial0",
            "WebglPointsSpritesMaterial1",
            "WebglPointsSpritesMaterial2",
            "WebglPointsSpritesMaterial3",
            "WebglPointsSpritesMaterial4",
        };

        static_assert(sizeof(PointsSpritesHostVertex) == 32u);
        static_assert(sizeof(PointsSpritesHostObjectData) == 128u);
        static_assert(sizeof(PointsSpritesHostInstanceData) == 16u);
        static_assert(sizeof(PointsSpritesHostMaterialData) == 32u);

        /** Creates parent directories for one requested artifact. */
        void preparePointsSpritesOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Validates all three immutable scenarios and host parameters. */
        void validatePointsSpritesScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool textureOff =
                options.scenarioId == "texture-off" &&
                options.targetFrame == 61u &&
                !options.inputReplayPath.empty();
            if (options.caseId != "webgl_points_sprites" ||
                (!initial && !animated && !textureOff) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty() ||
                ((initial || animated) && !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "Points sprites require the locked case, scenario, extent, seed, assets, and replay contract.");
            }
        }

        /** Returns one exact JavaScript Math.random binary64 value. */
        double nextPointsSpritesRandom(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Returns Three's wrapped HSL channel interpolation. */
        double pointsSpritesHueToRgb(
            double minimum,
            double maximum,
            double hue)
        {
            if (hue < 0.0) hue += 1.0;
            if (hue > 1.0) hue -= 1.0;
            if (hue < 1.0 / 6.0)
                return minimum + (maximum - minimum) * 6.0 * hue;
            if (hue < 0.5) return maximum;
            if (hue < 2.0 / 3.0)
                return minimum +
                    (maximum - minimum) * 6.0 * (2.0 / 3.0 - hue);
            return minimum;
        }

        /** Converts one sRGB channel to Three's linear working space. */
        double pointsSpritesSrgbToLinear(double value)
        {
            return value < 0.04045
                ? value * 0.0773993808
                : std::pow(value * 0.9478672986 + 0.0521327014, 2.4);
        }

        /** Evaluates Color.setHSL with an explicit sRGB source space. */
        glm::vec3 makePointsSpritesColor(
            double hue,
            double saturation)
        {
            hue -= std::floor(hue);
            const double maximum = 0.5 * (1.0 + saturation);
            const double minimum = 1.0 - maximum;
            return glm::vec3(
                static_cast<float>(pointsSpritesSrgbToLinear(
                    pointsSpritesHueToRgb(
                        minimum, maximum, hue + 1.0 / 3.0))),
                static_cast<float>(pointsSpritesSrgbToLinear(
                    pointsSpritesHueToRgb(minimum, maximum, hue))),
                static_cast<float>(pointsSpritesSrgbToLinear(
                    pointsSpritesHueToRgb(
                        minimum, maximum, hue - 1.0 / 3.0))));
        }

        /** Builds Three's 75-degree OpenGL perspective matrix. */
        glm::mat4 makePointsSpritesProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 2000.0;
            const double top = NearDistance * std::tan(75.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = height * 1.6;
            glm::mat4 result(0.0f);
            result[0u][0u] = static_cast<float>(2.0 * NearDistance / width);
            result[1u][1u] = static_cast<float>(2.0 * NearDistance / height);
            result[2u][2u] = static_cast<float>(
                -(FarDistance + NearDistance) / (FarDistance - NearDistance));
            result[2u][3u] = -1.0f;
            result[3u][2u] = static_cast<float>(
                -2.0 * FarDistance * NearDistance /
                (FarDistance - NearDistance));
            return result;
        }

        /** Builds an exact Euler XYZ model rotation followed by camera translation. */
        glm::mat4 makePointsSpritesModelView(double x, double y, double z)
        {
            const double c1 = std::cos(x * 0.5);
            const double c2 = std::cos(y * 0.5);
            const double c3 = std::cos(z * 0.5);
            const double s1 = std::sin(x * 0.5);
            const double s2 = std::sin(y * 0.5);
            const double s3 = std::sin(z * 0.5);
            const double qx = s1 * c2 * c3 + c1 * s2 * s3;
            const double qy = c1 * s2 * c3 - s1 * c2 * s3;
            const double qz = c1 * c2 * s3 + s1 * s2 * c3;
            const double qw = c1 * c2 * c3 - s1 * s2 * s3;
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
            glm::mat4 matrix(1.0f);
            matrix[0u][0u] = static_cast<float>(1.0 - (yy + zz));
            matrix[0u][1u] = static_cast<float>(xy + wz);
            matrix[0u][2u] = static_cast<float>(xz - wy);
            matrix[1u][0u] = static_cast<float>(xy - wz);
            matrix[1u][1u] = static_cast<float>(1.0 - (xx + zz));
            matrix[1u][2u] = static_cast<float>(yz + wx);
            matrix[2u][0u] = static_cast<float>(xz + wy);
            matrix[2u][1u] = static_cast<float>(yz - wx);
            matrix[2u][2u] = static_cast<float>(1.0 - (xx + yy));
            matrix[3u][2u] = -1000.0f;
            return matrix;
        }

        /** Flips decoded rows like Three's default WebGL upload. */
        RgbaImageData flipPointsSpritesRows(const RgbaImageData &source)
        {
            RgbaImageData result = source;
            const size_t rowBytes = static_cast<size_t>(source.width) * 4u;
            for (uint32_t row = 0u; row < source.height; ++row)
            {
                eastl::copy_n(
                    source.pixels.begin() +
                        static_cast<size_t>(source.height - 1u - row) * rowBytes,
                    rowBytes,
                    result.pixels.begin() + static_cast<size_t>(row) * rowBytes);
            }
            return result;
        }

        /** Appends one typed buffer payload to a RenderSet allocation. */
        void appendPointsSpritesBufferPayload(
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
    } // namespace

    void WebglPointsSpritesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validatePointsSpritesScenario(options);
        device = inDevice;
        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t draw = 0u; draw < PreGeometryRandomDrawCount; ++draw)
            (void)random.nextUint32();

        eastl::vector<glm::vec3> positions;
        positions.reserve(PointCount);
        for (uint32_t point = 0u; point < PointCount; ++point)
        {
            positions.push_back(glm::vec3(
                static_cast<float>(nextPointsSpritesRandom(random) * 2000.0 - 1000.0),
                static_cast<float>(nextPointsSpritesRandom(random) * 2000.0 - 1000.0),
                static_cast<float>(nextPointsSpritesRandom(random) * 2000.0 - 1000.0)));
        }
        const TexturedBoxHostFloat4 corners[4u] = {
            {-1.0f, -1.0f, 0.0f, 0.0f},
            {1.0f, -1.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 0.0f, 0.0f},
            {-1.0f, 1.0f, 0.0f, 0.0f},
        };
        const glm::mat4 projection = makePointsSpritesProjection();
        const double virtualTime =
            double(options.targetFrame) * FrameStepMilliseconds;
        const double time =
            (ReferenceEpochMilliseconds + virtualTime) * 0.00005;
        const bool texturesEnabled = options.scenarioId != "texture-off";

        for (uint32_t layer = 0u; layer < entities.size(); ++layer)
        {
            for (uint32_t draw = 0u; draw < ObjectRandomDrawCount; ++draw)
                (void)random.nextUint32();
            const double rotationX = nextPointsSpritesRandom(random) * 6.0;
            (void)nextPointsSpritesRandom(random);
            const double rotationZ = nextPointsSpritesRandom(random) * 6.0;
            const double rotationY = time *
                (layer < 4u ? double(layer + 1u) : -double(layer + 1u));
            PointsSpritesEntityState &entity = entities[layer];
            entity.vertices.reserve(PointCount * 4u);
            entity.indices.reserve(PointCount * 6u);
            for (uint32_t point = 0u; point < PointCount; ++point)
            {
                const uint32_t baseVertex = point * 4u;
                for (const TexturedBoxHostFloat4 &corner : corners)
                {
                    entity.vertices.push_back({
                        {positions[point].x, positions[point].y, positions[point].z, 1.0f},
                        corner,
                    });
                }
                entity.indices.push_back(baseVertex + 0u);
                entity.indices.push_back(baseVertex + 1u);
                entity.indices.push_back(baseVertex + 3u);
                entity.indices.push_back(baseVertex + 1u);
                entity.indices.push_back(baseVertex + 2u);
                entity.indices.push_back(baseVertex + 3u);
            }
            entity.objectData = {
                .modelView = makePointsSpritesModelView(
                    rotationX, rotationY, rotationZ),
                .projection = projection,
            };
            const double hue = std::fmod(BaseHue[layer] + time, 1.0);
            const glm::vec3 color =
                makePointsSpritesColor(hue, Saturation[layer]);
            entity.materialData = {
                .colorAndSize = {color.x, color.y, color.z, PointSize[layer]},
                .flags = {
                    texturesEnabled ? 1.0f : 0.0f,
                    0.0f,
                    0.0f,
                    0.0f},
            };
            const RgbaImageData baseImage = texturesEnabled
                ? flipPointsSpritesRows(
                    decodePngRgba8(
                        std::filesystem::path(options.assetRoot.c_str()) /
                        "textures" / "sprites" / TextureNames[layer]))
                : RgbaImageData{
                    .width = 1u,
                    .height = 1u,
                    .pixels = {255u, 255u, 255u, 255u},
                };
            entity.textureWidth = baseImage.width;
            entity.textureHeight = baseImage.height;
            const eastl::vector<RgbaImageData> mipChain =
                buildSrgbMipChain(baseImage);
            for (const RgbaImageData &mip : mipChain)
            {
                entity.mipOffsets.push_back(entity.textureBytes.size());
                entity.textureBytes.insert(
                    entity.textureBytes.end(), mip.pixels.begin(), mip.pixels.end());
            }
        }
        for (uint32_t draw = 0u; draw < RendererRandomDrawCount; ++draw)
            (void)random.nextUint32();
        finalRandomState = random.getState();

        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error("Could not create points sprites RenderSet encoder.");
        for (uint32_t layer = 0u; layer < entities.size(); ++layer)
        {
            PointsSpritesEntityState &entity = entities[layer];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendPointsSpritesBufferPayload(allocation,
                WebglPointsSpritesSceneRenderSetComponents::vertices,
                "WebglPointsSpritesVertices", entity.vertices.data(),
                entity.vertices.size() * sizeof(entity.vertices[0u]), 1u);
            appendPointsSpritesBufferPayload(allocation,
                WebglPointsSpritesSceneRenderSetComponents::indices,
                "WebglPointsSpritesIndices", entity.indices.data(),
                entity.indices.size() * sizeof(entity.indices[0u]), 1u);
            appendPointsSpritesBufferPayload(allocation,
                WebglPointsSpritesSceneRenderSetComponents::objects,
                ObjectBufferNames[layer], &entity.objectData,
                sizeof(entity.objectData), 1u);
            appendPointsSpritesBufferPayload(allocation,
                WebglPointsSpritesSceneRenderSetComponents::instances,
                "WebglPointsSpritesInstance", &entity.instanceData,
                sizeof(entity.instanceData), 1u);
            appendPointsSpritesBufferPayload(allocation,
                WebglPointsSpritesSceneRenderSetComponents::materials,
                MaterialBufferNames[layer], &entity.materialData,
                sizeof(entity.materialData), 1u);
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebglPointsSpritesSceneRenderSetComponents::textures;
            textureComponent.textures.push_back({
                .textureName = TextureNames[layer],
                .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width = entity.textureWidth,
                .height = entity.textureHeight,
                .data = entity.textureBytes.data(),
                .dataStorageBytes = entity.textureBytes.size(),
                .mipmapOffsetBytes = entity.mipOffsets,
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglPointsSpritesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPointsSpritesRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeArtifacts(options, frameIndex, width, height, rgba);
        captureWritten = true;
    }

    void WebglPointsSpritesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        for (PointsSpritesEntityState &entity : entities)
        {
            entity.vertices.clear();
            entity.indices.clear();
            entity.textureBytes.clear();
            entity.mipOffsets.clear();
        }
    }

    void WebglPointsSpritesRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            preparePointsSpritesOutputPath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write points sprites RGBA.");
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path path(options.captureMetadataPath.c_str());
            preparePointsSpritesOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
                << "\"caseId\":\"webgl_points_sprites\",\"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\"pipeline\":\""
                << options.pipeline.c_str() << "\",\"backend\":\""
                << threeSampleBackendName(options.backend) << "\",\"frame\":"
                << frameIndex << ",\"randomSeed\":" << options.randomSeed
                << ",\"width\":" << width << ",\"height\":" << height
                << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
                << ",\"byteCount\":" << rgba.size()
                << ",\"format\":\"rgba8unorm\"";
            if (options.scenarioId == "texture-off")
            {
                output << ",\"inputReplay\":{\"schemaVersion\":1,"
                    << "\"caseId\":\"webgl_points_sprites\","
                    << "\"scenarioId\":\"texture-off\","
                    << "\"captureFrame\":61,"
                    << "\"sha256\":\"a100baef95c1ac4c3974ddf85307ce761a16d9c81420da40168fc1e17b490787\","
                    << "\"target\":\".controller input[type=\\\"checkbox\\\"]\","
                    << "\"eventCount\":1,\"lastEventFrame\":1}";
            }
            output << "}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path path(options.sceneSnapshotPath.c_str());
            preparePointsSpritesOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output << "{\"schemaVersion\":1,\"caseId\":\"webgl_points_sprites\","
                << "\"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\"frame\":" << frameIndex
                << ",\"implementationLevel\":\"semantic-complete\","
                << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
                << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":5,"
                << "\"entityCount\":5,\"instanceCount\":5,"
                << "\"vertexCount\":200000,\"indexCount\":300000,"
                << "\"scenePassCount\":1,\"screenPassCount\":0,"
                << "\"drawCommandCount\":1,\"finalRandomState\":"
                << finalRandomState
                << ",\"renderSetType\":\"WebglPointsSpritesSceneRenderSet\","
                << "\"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],"
                << "\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
                << "\"renderSetId\":\"scene-set\","
                << "\"renderSetType\":\"WebglPointsSpritesSceneRenderSet\","
                << "\"renderableObjectCount\":5,\"entityCount\":5,"
                << "\"entities\":["
                << "{\"entityId\":0,\"logicalRenderableId\":\"snowflake-layer-1\",\"instanceCount\":1},"
                << "{\"entityId\":1,\"logicalRenderableId\":\"snowflake-layer-2\",\"instanceCount\":1},"
                << "{\"entityId\":2,\"logicalRenderableId\":\"snowflake-layer-3\",\"instanceCount\":1},"
                << "{\"entityId\":3,\"logicalRenderableId\":\"snowflake-layer-4\",\"instanceCount\":1},"
                << "{\"entityId\":4,\"logicalRenderableId\":\"snowflake-layer-5\",\"instanceCount\":1}],"
                << "\"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],"
                << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
                << "\"scenePasses\":[{\"name\":\"additive-points\","
                << "\"renderClass\":\"WebglPointsSpritesAdditivePass\","
                << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
                << "\"drawMode\":\"render-set-indexed-indirect\","
                << "\"invocationCount\":1,\"drawCommandCount\":1,"
                << "\"usesStandaloneGeometry\":false,"
                << "\"usesExplicitDrawCount\":false}]}],"
                << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
                << "\"scenePass\":\"additive-points\",\"entityOrdinal\":0}],"
                << "\"usesRenderEntityID\":true,"
                << "\"usesRenderEntityInstanceID\":false,"
                << "\"directDrawFallback\":false}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path path(options.semanticSnapshotPath.c_str());
            preparePointsSpritesOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output << "{\"schemaVersion\":1,\"caseId\":\"webgl_points_sprites\","
                << "\"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\"frame\":" << frameIndex
                << ",\"logicalPointCount\":10000,\"layerCount\":5,"
                << "\"textureEnabled\":"
                << (options.scenarioId == "texture-off" ? "false" : "true")
                << "}\n";
        }
    }
} // namespace GVM::ThreeSamples
