#include "WebgpuInstanceSpritesRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/string.h>

#include <CommonCrypto/CommonDigest.h>

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
        constexpr uint32_t SpriteInstanceCount = 10000u;
        constexpr uint32_t PrePositionRandomDrawCount = 184u;
        constexpr uint32_t PostPositionRandomDrawCount = 89u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(WebgpuInstanceSpritesHostVertex) == 16u);
        static_assert(sizeof(WebgpuInstanceSpritesHostObjectData) == 112u);
        static_assert(sizeof(WebgpuInstanceSpritesHostInstanceData) == 16u);
        static_assert(sizeof(WebgpuInstanceSpritesHostMaterialData) == 32u);

        /** Creates parent directories for one requested artifact. */
        void prepareInstanceSpritesOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
                std::filesystem::create_directories(outputPath.parent_path());
        }

        /** Validates all three deterministic scenarios and host parameters. */
        void validateInstanceSpritesScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial-assets" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            const bool pointer =
                options.scenarioId == "no-attenuation-pointer" &&
                options.targetFrame == 61u && !options.inputReplayPath.empty();
            if (options.caseId != "webgpu_instance_sprites" ||
                (!initial && !animated && !pointer) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty() ||
                ((initial || animated) && !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "webgpu_instance_sprites requires the locked case, extent, seed, assets, and replay contract.");
            }
        }

        /** Returns one exact JavaScript Math.random binary32 value. */
        double nextInstanceSpritesRandom(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Returns Three's wrapped HSL channel interpolation. */
        double instanceSpritesHueToRgb(
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

        /** Evaluates Color.setHSL in Three's linear working color space. */
        glm::vec3 makeInstanceSpritesColor(double hue)
        {
            hue -= std::floor(hue);
            constexpr double Saturation = 0.5;
            constexpr double Lightness = 0.5;
            const double maximum = Lightness * (1.0 + Saturation);
            const double minimum = 2.0 * Lightness - maximum;
            return glm::vec3(
                float(instanceSpritesHueToRgb(
                    minimum, maximum, hue + 1.0 / 3.0)),
                float(instanceSpritesHueToRgb(minimum, maximum, hue)),
                float(instanceSpritesHueToRgb(
                    minimum, maximum, hue - 1.0 / 3.0)));
        }

        /** Builds the zero-to-one, Y-inverted r185 perspective projection. */
        glm::mat4 makeInstanceSpritesProjection()
        {
            constexpr double NearDistance = 2.0;
            constexpr double FarDistance = 2000.0;
            const double top = NearDistance * std::tan(55.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = height * 1.6;
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = float(2.0 * NearDistance / width);
            projection[1u][1u] = float(-2.0 * NearDistance / height);
            projection[2u][2u] = float(-FarDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = float(-FarDistance * NearDistance / depth);
            return projection;
        }

        /** Flips decoded rows like Three's default external texture upload. */
        RgbaImageData flipInstanceSpritesRows(const RgbaImageData &source)
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
        void appendInstanceSpritesBuffer(
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

        /** Calculates the SHA-256 identity of the locked input replay. */
        std::string calculateInstanceSpritesReplaySha256(
            const eastl::string &path)
        {
            if (path.empty()) return {};
            std::ifstream input(
                std::filesystem::path(path.c_str()),
                std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open instance sprites replay.");
            const std::streamoff size = input.tellg();
            if (size <= 0)
                throw std::runtime_error("Instance sprites replay is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), size);
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
            constexpr char HexDigits[] = "0123456789abcdef";
            std::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }
    } // namespace

    void WebgpuInstanceSpritesRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateInstanceSpritesScenario(options);
        device = inDevice;
        vertices = {
            {{-1.0f, -1.0f, 0.0f, 0.0f}},
            {{1.0f, -1.0f, 0.0f, 0.0f}},
            {{1.0f, 1.0f, 0.0f, 0.0f}},
            {{-1.0f, 1.0f, 0.0f, 0.0f}},
        };
        indices = {0u, 1u, 3u, 1u, 2u, 3u};

        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t draw = 0u; draw < PrePositionRandomDrawCount; ++draw)
            (void)random.nextUint32();
        instances.clear();
        instances.reserve(SpriteInstanceCount);
        for (uint32_t index = 0u; index < SpriteInstanceCount; ++index)
        {
            instances.push_back({glm::vec4(
                float(nextInstanceSpritesRandom(random) * 2000.0 - 1000.0),
                float(nextInstanceSpritesRandom(random) * 2000.0 - 1000.0),
                float(nextInstanceSpritesRandom(random) * 2000.0 - 1000.0),
                float(index))});
        }
        for (uint32_t draw = 0u; draw < PostPositionRandomDrawCount; ++draw)
            (void)random.nextUint32();
        finalRandomState = random.getState();
        if (finalRandomState != 3964576005u)
            throw std::runtime_error(
                "webgpu_instance_sprites random stream diverged from r185.");

        const RgbaImageData baseImage = flipInstanceSpritesRows(
            decodePngRgba8(
                std::filesystem::path(options.assetRoot.c_str()) /
                "textures" / "sprites" / "snowflake1.png"));
        textureWidth = baseImage.width;
        textureHeight = baseImage.height;
        const eastl::vector<RgbaImageData> mipChain = buildSrgbMipChain(baseImage);
        for (const RgbaImageData &mip : mipChain)
        {
            mipOffsets.push_back(textureBytes.size());
            textureBytes.insert(
                textureBytes.end(), mip.pixels.begin(), mip.pixels.end());
        }

        updateFrameState(options, options.targetFrame);
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create webgpu_instance_sprites RenderSet encoder.");
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = SpriteInstanceCount;
        appendInstanceSpritesBuffer(
            allocation,
            WebgpuInstanceSpritesSceneRenderSetComponents::vertices,
            "WebgpuInstanceSpritesVertices",
            vertices.data(), vertices.size() * sizeof(vertices[0u]), 1u);
        appendInstanceSpritesBuffer(
            allocation,
            WebgpuInstanceSpritesSceneRenderSetComponents::indices,
            "WebgpuInstanceSpritesIndices",
            indices.data(), indices.size() * sizeof(indices[0u]), 1u);
        appendInstanceSpritesBuffer(
            allocation,
            WebgpuInstanceSpritesSceneRenderSetComponents::objects,
            "WebgpuInstanceSpritesObject",
            &objectData, sizeof(objectData), 1u);
        appendInstanceSpritesBuffer(
            allocation,
            WebgpuInstanceSpritesSceneRenderSetComponents::instances,
            "WebgpuInstanceSpritesInstances",
            instances.data(), instances.size() * sizeof(instances[0u]),
            SpriteInstanceCount);
        appendInstanceSpritesBuffer(
            allocation,
            WebgpuInstanceSpritesSceneRenderSetComponents::materials,
            "WebgpuInstanceSpritesMaterial",
            &materialData, sizeof(materialData), 1u);
        GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
        textureComponent.textureComponentHandle =
            WebgpuInstanceSpritesSceneRenderSetComponents::textures;
        textureComponent.textures.push_back({
            .textureName = "snowflake1.png",
            .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
            .width = textureWidth,
            .height = textureHeight,
            .data = textureBytes.data(),
            .dataStorageBytes = textureBytes.size(),
            .mipmapOffsetBytes = mipOffsets,
        });
        allocation.textureInfos.push_back(eastl::move(textureComponent));
        entityIndex = encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuInstanceSpritesRuntimeAdapter::updateFrameState(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        const bool pointerScenario =
            options.scenarioId == "no-attenuation-pointer";
        const double cameraFactor = pointerScenario
            ? 1.0 - std::pow(0.95, double(frameIndex + 1u))
            : 0.0;
        const glm::vec3 cameraPosition(
            float(250.0 * cameraFactor),
            float(150.0 * cameraFactor),
            1000.0f);
        const glm::mat4 view = glm::lookAt(
            cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 cameraWorld = glm::inverse(view);
        const double virtualMilliseconds =
            double(frameIndex) * FrameStepMilliseconds;
        const double timeSeconds = virtualMilliseconds * 0.001;
        const double hue = std::fmod(
            1.0 + (ReferenceEpochMilliseconds + virtualMilliseconds) * 0.00005,
            1.0);
        const glm::vec3 color = makeInstanceSpritesColor(hue);
        objectData.viewProjection = makeInstanceSpritesProjection() * view;
        objectData.cameraRightAndTime = glm::vec4(
            glm::vec3(cameraWorld[0u]), float(timeSeconds));
        objectData.cameraUpAndFogDensity = glm::vec4(
            glm::vec3(cameraWorld[1u]), 0.001f);
        objectData.cameraPositionAndReserved = glm::vec4(cameraPosition, 0.0f);
        materialData.colorAndScale = glm::vec4(
            color, pointerScenario ? 0.03f : 15.0f);
        materialData.flags = glm::vec4(
            pointerScenario ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f);
    }

    void WebgpuInstanceSpritesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateFrameState(options, frameIndex);
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create webgpu_instance_sprites update encoder.");
        encoder->setBufferComponentData(
            entityIndex,
            WebgpuInstanceSpritesSceneRenderSetComponents::objects,
            &objectData, sizeof(objectData), 0u, 1u);
        encoder->setBufferComponentData(
            entityIndex,
            WebgpuInstanceSpritesSceneRenderSetComponents::materials,
            &materialData, sizeof(materialData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuInstanceSpritesRuntimeAdapter::afterFrame(
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
        if (byteCount > std::numeric_limits<size_t>::max())
            throw std::overflow_error(
                "webgpu_instance_sprites capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeArtifacts(options, frameIndex, width, height, rgba);
        captureWritten = true;
    }

    void WebgpuInstanceSpritesRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareInstanceSpritesOutputPath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path path(options.captureMetadataPath.c_str());
            prepareInstanceSpritesOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgpu_instance_sprites\",\"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\"pipeline\":\""
                   << options.pipeline.c_str() << "\",\"backend\":\""
                   << threeSampleBackendName(options.backend) << "\",\"frame\":"
                   << frameIndex << ",\"randomSeed\":" << options.randomSeed
                   << ",\"randomState\":" << finalRandomState
                   << ",\"width\":" << width << ",\"height\":" << height
                   << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
                   << ",\"byteCount\":" << rgba.size()
                   << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false";
            if (!options.inputReplayPath.empty())
            {
                output << ",\"inputReplay\":{\"sha256\":\""
                       << calculateInstanceSpritesReplaySha256(
                              options.inputReplayPath)
                       << "\",\"caseId\":\"webgpu_instance_sprites\",\"scenarioId\":\"no-attenuation-pointer\",\"captureFrame\":61,\"eventCount\":1,\"target\":\"body\"}";
            }
            output << "}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path path(options.sceneSnapshotPath.c_str());
            prepareInstanceSpritesOutputPath(path);
            std::ofstream output(path, std::ios::trunc);
            output << "{\"schemaVersion\":1,\"caseId\":\"webgpu_instance_sprites\",\"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\"frame\":" << frameIndex
                   << ",\"implementationLevel\":\"semantic-complete\",\"gpuWorkDslOnly\":true,\"assetBacked\":true,\"renderSetPolicy\":\"required\",\"sceneRenderSetCount\":1,\"renderSetType\":\"WebgpuInstanceSpritesSceneRenderSet\",\"renderableObjectCount\":1,\"entityCount\":1,\"instanceCount\":10000,\"instanceCounts\":[10000],\"vertexCount\":4,\"indexCount\":6,\"scenePassCount\":1,\"screenPassCount\":2,\"drawCommandCount\":1,\"renderSetIndexedIndirect\":true,\"directDrawFallback\":false,\"sampleCount\":1,\"msaaEnabled\":false,\"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\",\"textures\"],\"assetAndAlgorithmState\":\"r185-snowflake1-srgb-mips-instance-rotation-size-attenuation-fog\",\"scenePasses\":[{\"name\":\"transparent-sprites\",\"renderClass\":\"WebgpuInstanceSpritesMainPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}],\"screenPasses\":[{\"name\":\"snowflake-mipmap-generation\",\"redrawsSceneGeometry\":false},{\"name\":\"deterministic-output-quantization\",\"redrawsSceneGeometry\":false}],\"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene-set\",\"renderSetType\":\"WebgpuInstanceSpritesSceneRenderSet\",\"renderableObjectCount\":1,\"entityCount\":1,\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"instanced-snowflake-sprite\",\"instanceCount\":10000}],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"transparent-sprites\",\"renderClass\":\"WebgpuInstanceSpritesMainPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"transparent-sprites\",\"entityOrdinal\":0}]}\n";
        }
    }

    void WebgpuInstanceSpritesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        instances.clear();
        textureBytes.clear();
        mipOffsets.clear();
    }
} // namespace GVM::ThreeSamples
