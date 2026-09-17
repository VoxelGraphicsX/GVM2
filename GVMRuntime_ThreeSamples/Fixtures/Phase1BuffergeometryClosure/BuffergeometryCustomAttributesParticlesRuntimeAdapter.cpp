#include "BuffergeometryCustomAttributesParticlesRuntimeAdapter.hpp"

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
        constexpr uint32_t ParticleCount = 100000u;
        constexpr uint32_t PrePositionRandomDrawCount = 100u;
        constexpr uint32_t PostPositionRandomDrawCount = 40u;
        constexpr uint32_t ExpectedFinalRandomState = 1288354509u;
        constexpr uint32_t SparkExtent = 32u;
        constexpr uint32_t SparkMipCount = 6u;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(BuffergeometryParticlesHostVertex) == 16u);
        static_assert(sizeof(BuffergeometryParticlesHostObjectData) == 128u);
        static_assert(sizeof(BuffergeometryParticlesHostInstanceData) == 32u);
        static_assert(sizeof(BuffergeometryParticlesHostMaterialData) == 16u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareParticlesOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Validates the two immutable manifest scenarios and host contract. */
        void validateParticlesScenario(const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u;
            if (options.caseId !=
                    "webgl_buffergeometry_custom_attributes_particles" ||
                (!initial && !animated) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                !options.inputReplayPath.empty() ||
                options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "Custom-attribute particles require the locked case, scenario, extent, seed, asset root, and no replay.");
            }
        }

        /** Returns one exact JavaScript Math.random binary64 value. */
        double nextParticlesRandom(
            ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Evaluates one wrapped HSL channel with Three r185 arithmetic. */
        double particlesHueToRgb(
            double minimum,
            double maximum,
            double hue)
        {
            if (hue < 0.0) hue += 1.0;
            if (hue > 1.0) hue -= 1.0;
            if (hue < 1.0 / 6.0)
            {
                return minimum + (maximum - minimum) * 6.0 * hue;
            }
            if (hue < 0.5) return maximum;
            if (hue < 2.0 / 3.0)
            {
                return minimum +
                    (maximum - minimum) * 6.0 * (2.0 / 3.0 - hue);
            }
            return minimum;
        }

        /** Converts one full-saturation HSL phase to Three's working RGB. */
        glm::vec3 makeParticlesHsl(double hue)
        {
            hue -= std::floor(hue);
            constexpr double Maximum = 1.0;
            constexpr double Minimum = 0.0;
            return glm::vec3(
                float(particlesHueToRgb(
                    Minimum,
                    Maximum,
                    hue + 1.0 / 3.0)),
                float(particlesHueToRgb(
                    Minimum,
                    Maximum,
                    hue)),
                float(particlesHueToRgb(
                    Minimum,
                    Maximum,
                    hue - 1.0 / 3.0)));
        }

        /** Builds Three's original OpenGL projection before backend conversion. */
        glm::mat4 makeParticlesProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10000.0;
            const double top =
                NearDistance * std::tan(40.0 * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                (800.0 / 500.0) * projectionHeight;
            const double projectionDepth =
                FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] =
                float(2.0 * NearDistance / projectionWidth);
            projection[1u][1u] =
                float(2.0 * NearDistance / projectionHeight);
            projection[2u][2u] =
                float(-(FarDistance + NearDistance) /
                      projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] =
                float(-2.0 * FarDistance * NearDistance /
                      projectionDepth);
            return projection;
        }

        /** Builds the target-frame Z rotation followed by camera translation. */
        glm::mat4 makeParticlesModelView(double rotation)
        {
            const double quaternionZ = std::sin(rotation * 0.5);
            const double quaternionW = std::cos(rotation * 0.5);
            const double doubledZ = quaternionZ + quaternionZ;
            const double zz = quaternionZ * doubledZ;
            const double wz = quaternionW * doubledZ;
            glm::mat4 modelView(1.0f);
            modelView[0u][0u] = float(1.0 - zz);
            modelView[0u][1u] = float(wz);
            modelView[1u][0u] = float(-wz);
            modelView[1u][1u] = float(1.0 - zz);
            modelView[3u][2u] = -300.0f;
            return modelView;
        }

        /** Flips decoded rows like Three's default browser texture upload. */
        RgbaImageData flipParticlesRows(const RgbaImageData &source)
        {
            if (source.width != SparkExtent ||
                source.height != SparkExtent)
            {
                throw std::runtime_error(
                    "spark1.png must remain 32x32 RGBA8.");
            }
            RgbaImageData result = source;
            const size_t rowBytes =
                static_cast<size_t>(source.width) * 4u;
            for (uint32_t row = 0u;
                 row < source.height;
                 ++row)
            {
                eastl::copy_n(
                    source.pixels.begin() +
                        static_cast<size_t>(
                            source.height - 1u - row) *
                            rowBytes,
                    rowBytes,
                    result.pixels.begin() +
                        static_cast<size_t>(row) * rowBytes);
            }
            return result;
        }

        /** Builds one raw-UNORM 2x2 mip with nearest integer rounding. */
        RgbaImageData buildNextParticlesMip(
            const RgbaImageData &source)
        {
            RgbaImageData result;
            result.width = source.width / 2u;
            result.height = source.height / 2u;
            result.pixels.resize(
                static_cast<size_t>(result.width) *
                result.height *
                4u);
            for (uint32_t y = 0u; y < result.height; ++y)
            {
                for (uint32_t x = 0u; x < result.width; ++x)
                {
                    for (uint32_t channel = 0u;
                         channel < 4u;
                         ++channel)
                    {
                        uint32_t sum = 0u;
                        for (uint32_t offsetY = 0u;
                             offsetY < 2u;
                             ++offsetY)
                        {
                            for (uint32_t offsetX = 0u;
                                 offsetX < 2u;
                                 ++offsetX)
                            {
                                sum += source.pixels[
                                    (static_cast<size_t>(
                                         y * 2u + offsetY) *
                                         source.width +
                                     x * 2u + offsetX) *
                                        4u +
                                    channel];
                            }
                        }
                        result.pixels[
                            (static_cast<size_t>(y) *
                                 result.width +
                             x) *
                                4u +
                            channel] =
                            uint8_t((sum + 2u) / 4u);
                    }
                }
            }
            return result;
        }

        /** Builds the exact quad template and target-frame instance stream. */
        uint32_t buildParticlesScene(
            uint32_t targetFrame,
            eastl::vector<BuffergeometryParticlesHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            eastl::vector<BuffergeometryParticlesHostInstanceData> &instances)
        {
            vertices.clear();
            vertices.push_back({
                .corner = {-1.0f, -1.0f, 0.0f, 0.0f}});
            vertices.push_back({
                .corner = {1.0f, -1.0f, 0.0f, 0.0f}});
            vertices.push_back({
                .corner = {1.0f, 1.0f, 0.0f, 0.0f}});
            vertices.push_back({
                .corner = {-1.0f, 1.0f, 0.0f, 0.0f}});
            indices = {0u, 1u, 2u, 0u, 2u, 3u};

            ThreeCompat::DeterministicRandom random(
                DefaultThreeRandomSeed);
            for (uint32_t draw = 0u;
                 draw < PrePositionRandomDrawCount;
                 ++draw)
            {
                (void)random.nextUint32();
            }
            double virtualTimeMilliseconds = 0.0;
            for (uint32_t frame = 0u;
                 frame < targetFrame;
                 ++frame)
            {
                virtualTimeMilliseconds +=
                    FrameStepMilliseconds;
            }
            const double time =
                (ReferenceEpochMilliseconds +
                 virtualTimeMilliseconds) *
                0.005;

            instances.clear();
            instances.reserve(ParticleCount);
            for (uint32_t particleIndex = 0u;
                 particleIndex < ParticleCount;
                 ++particleIndex)
            {
                const float x = float(
                    (nextParticlesRandom(random) * 2.0 - 1.0) *
                    200.0);
                const float y = float(
                    (nextParticlesRandom(random) * 2.0 - 1.0) *
                    200.0);
                const float z = float(
                    (nextParticlesRandom(random) * 2.0 - 1.0) *
                    200.0);
                const float size = float(
                    10.0 *
                    (1.0 +
                     std::sin(
                         0.1 * double(particleIndex) +
                         time)));
                const glm::vec3 color =
                    makeParticlesHsl(
                        double(particleIndex) /
                        double(ParticleCount));
                instances.push_back({
                    {x, y, z, size},
                    {color.x, color.y, color.z, 1.0f}});
            }
            for (uint32_t draw = 0u;
                 draw < PostPositionRandomDrawCount;
                 ++draw)
            {
                (void)random.nextUint32();
            }
            if (random.getState() != ExpectedFinalRandomState)
            {
                throw std::runtime_error(
                    "Particle random stream diverged from r185.");
            }
            return random.getState();
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendParticlesBufferPayload(
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

    void BuffergeometryCustomAttributesParticlesRuntimeAdapter::
        initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
    {
        validateParticlesScenario(options);
        device = inDevice;
        finalRandomState = buildParticlesScene(
            options.targetFrame,
            vertices,
            indices,
            instances);

        RgbaImageData mip = flipParticlesRows(
            decodeStraightPngRgba8(
                std::filesystem::path(options.assetRoot.c_str()) /
                "textures" /
                "sprites" /
                "spark1.png"));
        for (uint32_t mipLevel = 0u;
             mipLevel < SparkMipCount;
             ++mipLevel)
        {
            sparkMipOffsets.push_back(
                sparkTextureBytes.size());
            sparkTextureBytes.insert(
                sparkTextureBytes.end(),
                mip.pixels.begin(),
                mip.pixels.end());
            if (mipLevel + 1u < SparkMipCount)
            {
                mip = buildNextParticlesMip(mip);
            }
        }

        double virtualTimeMilliseconds = 0.0;
        for (uint32_t frame = 0u;
             frame < options.targetFrame;
             ++frame)
        {
            virtualTimeMilliseconds +=
                FrameStepMilliseconds;
        }
        const double time =
            (ReferenceEpochMilliseconds +
             virtualTimeMilliseconds) *
            0.005;
        const BuffergeometryParticlesHostObjectData objectData = {
            .projectionMatrix = makeParticlesProjection(),
            .modelViewMatrix =
                makeParticlesModelView(0.01 * time),
        };
        const BuffergeometryParticlesHostMaterialData materialData = {
            .colorMultiplier = {1.0f, 1.0f, 1.0f, 1.0f},
        };

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Particle example could not create its Scene RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(indices.size());
        allocation.instanceCount = ParticleCount;
        appendParticlesBufferPayload(
            allocation,
            WebglBuffergeometryCustomAttributesParticlesSceneRenderSetComponents::
                vertices,
            "WebglBuffergeometryCustomAttributesParticlesVertices",
            vertices.data(),
            vertices.size() *
                sizeof(BuffergeometryParticlesHostVertex),
            1u);
        appendParticlesBufferPayload(
            allocation,
            WebglBuffergeometryCustomAttributesParticlesSceneRenderSetComponents::
                indices,
            "WebglBuffergeometryCustomAttributesParticlesIndices",
            indices.data(),
            indices.size() * sizeof(uint32_t),
            1u);
        appendParticlesBufferPayload(
            allocation,
            WebglBuffergeometryCustomAttributesParticlesSceneRenderSetComponents::
                objects,
            "WebglBuffergeometryCustomAttributesParticlesObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendParticlesBufferPayload(
            allocation,
            WebglBuffergeometryCustomAttributesParticlesSceneRenderSetComponents::
                instances,
            "WebglBuffergeometryCustomAttributesParticlesInstances",
            instances.data(),
            instances.size() *
                sizeof(BuffergeometryParticlesHostInstanceData),
            ParticleCount);
        appendParticlesBufferPayload(
            allocation,
            WebglBuffergeometryCustomAttributesParticlesSceneRenderSetComponents::
                materials,
            "WebglBuffergeometryCustomAttributesParticlesMaterial",
            &materialData,
            sizeof(materialData),
            1u);
        GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
        textureComponent.textureComponentHandle =
            WebglBuffergeometryCustomAttributesParticlesSceneRenderSetComponents::
                textures;
        textureComponent.textures.push_back({
            .textureName =
                "WebglBuffergeometryCustomAttributesParticlesSpark",
            .format = GVM::RHI::TextureFormat::RGBA8Unorm,
            .width = SparkExtent,
            .height = SparkExtent,
            .data = sparkTextureBytes.data(),
            .dataStorageBytes = sparkTextureBytes.size(),
            .mipmapOffsetBytes = sparkMipOffsets,
        });
        allocation.textureInfos.push_back(
            eastl::move(textureComponent));
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void BuffergeometryCustomAttributesParticlesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void BuffergeometryCustomAttributesParticlesRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten ||
            frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount >
            std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Particle RGBA8 capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        writeArtifacts(
            options,
            frameIndex,
            width,
            height,
            rgba);
        captureWritten = true;
    }

    void BuffergeometryCustomAttributesParticlesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        instances.clear();
        sparkTextureBytes.clear();
        sparkMipOffsets.clear();
    }

    void BuffergeometryCustomAttributesParticlesRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareParticlesOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary |
                    std::ios::out |
                    std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(
                    rgba.data()),
                static_cast<std::streamsize>(
                    rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write particle RGBA capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareParticlesOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_buffergeometry_custom_attributes_particles\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\"\n"
                << "}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareParticlesOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_buffergeometry_custom_attributes_particles\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":1,\n"
                << "  \"entityCount\":1,\n"
                << "  \"instanceCount\":100000,\n"
                << "  \"vertexCount\":4,\n"
                << "  \"indexCount\":6,\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"screenPassCount\":0,\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"finalRandomState\":" << finalRandomState << ",\n"
                << "  \"renderSetType\":\"WebglBuffergeometryCustomAttributesParticlesSceneRenderSet\",\n"
                << "  \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                << "  \"sceneRoots\":[{\n"
                << "    \"id\":\"scene\",\n"
                << "    \"renderSetCount\":1,\n"
                << "    \"renderSetId\":\"scene-set\",\n"
                << "    \"renderSetType\":\"WebglBuffergeometryCustomAttributesParticlesSceneRenderSet\",\n"
                << "    \"renderableObjectCount\":1,\n"
                << "    \"entityCount\":1,\n"
                << "    \"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"particle-system\",\"instanceCount\":100000}],\n"
                << "    \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                << "    \"drawCommandCount\":1,\n"
                << "    \"directDrawFallback\":false,\n"
                << "    \"scenePasses\":[{\"name\":\"main-additive-particles\","
                << "\"renderClass\":\"WebglBuffergeometryCustomAttributesParticlesMainPass\","
                << "\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,"
                << "\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,"
                << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
                << "\"usesExplicitDrawCount\":false}]\n"
                << "  }],\n"
                << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
                << "\"scenePass\":\"main-additive-particles\",\"entityOrdinal\":0}],\n"
                << "  \"usesRenderEntityID\":true,\n"
                << "  \"usesRenderEntityInstanceID\":true,\n"
                << "  \"directDrawFallback\":false\n"
                << "}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareParticlesOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_buffergeometry_custom_attributes_particles\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"asset\":\"textures/sprites/spark1.png\",\n"
                << "  \"particleCount\":100000,\n"
                << "  \"randomDrawCount\":300140,\n"
                << "  \"finalRandomState\":" << finalRandomState << "\n"
                << "}\n";
        }
    }
} // namespace GVM::ThreeSamples
