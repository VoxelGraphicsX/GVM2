#include "BuffergeometryBillboardsRuntimeAdapter.hpp"

#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/algorithm.h>

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
        constexpr uint32_t InstanceCount = 75000u;
        constexpr uint32_t PreInstanceRandomDrawCount = 74u;
        constexpr uint32_t PostInstanceRandomDrawCount = 70u;
        constexpr uint32_t ExpectedFinalRandomState = 3824316873u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(BuffergeometryBillboardsHostVertex) == 32u);
        static_assert(sizeof(BuffergeometryBillboardsHostObjectData) == 144u);
        static_assert(sizeof(BuffergeometryBillboardsHostInstanceData) == 16u);
        static_assert(sizeof(BuffergeometryBillboardsHostMaterialData) == 16u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareBillboardsOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Returns the upper-24-bit JavaScript random unit value. */
        double nextBillboardsRandomUnit(
            ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Validates the two locked scenarios and host contract. */
        void validateBillboardsScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u;
            if (options.caseId !=
                    "webgl_buffergeometry_instancing_billboards" ||
                (!initial && !animated) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                !options.inputReplayPath.empty() ||
                options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "Billboard instancing requires its locked case, scenarios, extent, seed, asset root, and no replay.");
            }
        }

        /** Reproduces performance.now at one deterministic 60 Hz frame. */
        double billboardsVirtualTimeMilliseconds(uint32_t frameIndex)
        {
            double milliseconds = 0.0;
            for (uint32_t frame = 0u; frame < frameIndex; ++frame)
            {
                milliseconds += 1000.0 / 60.0;
            }
            return milliseconds;
        }

        /** Builds the exact six-segment CircleGeometry vertex and index records. */
        void buildBillboardsCircleGeometry(
            eastl::vector<BuffergeometryBillboardsHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr double Pi = 3.14159265358979323846;
            vertices.clear();
            indices.clear();
            vertices.push_back({
                .position = {0.0f, 0.0f, 0.0f, 1.0f},
                .texCoord = {0.5f, 0.5f, 0.0f, 0.0f}});
            for (uint32_t segment = 0u; segment <= 6u; ++segment)
            {
                const double angle =
                    double(segment) / 6.0 * Pi * 2.0;
                const float x = float(std::cos(angle));
                const float y = float(std::sin(angle));
                vertices.push_back({
                    .position = {x, y, 0.0f, 1.0f},
                    .texCoord = {
                        (x + 1.0f) * 0.5f,
                        (y + 1.0f) * 0.5f,
                        0.0f,
                        0.0f}});
            }
            for (uint32_t segment = 1u; segment <= 6u; ++segment)
            {
                indices.push_back(segment);
                indices.push_back(segment + 1u);
                indices.push_back(0u);
            }
        }

        /** Builds all seeded translate attributes and returns the final RNG state. */
        uint32_t buildBillboardsInstances(
            eastl::vector<BuffergeometryBillboardsHostInstanceData> &instances)
        {
            ThreeCompat::DeterministicRandom random(DefaultThreeRandomSeed);
            for (uint32_t draw = 0u;
                 draw < PreInstanceRandomDrawCount;
                 ++draw)
            {
                (void)random.nextUint32();
            }
            instances.clear();
            instances.reserve(InstanceCount);
            for (uint32_t instanceIndex = 0u;
                 instanceIndex < InstanceCount;
                 ++instanceIndex)
            {
                instances.push_back({
                    .translate = {
                        float(nextBillboardsRandomUnit(random) * 2.0 - 1.0),
                        float(nextBillboardsRandomUnit(random) * 2.0 - 1.0),
                        float(nextBillboardsRandomUnit(random) * 2.0 - 1.0),
                        0.0f}});
            }
            for (uint32_t draw = 0u;
                 draw < PostInstanceRandomDrawCount;
                 ++draw)
            {
                (void)random.nextUint32();
            }
            if (random.getState() != ExpectedFinalRandomState)
            {
                throw std::runtime_error(
                    "Billboard instance random stream diverged from r185.");
            }
            return random.getState();
        }

        /** Flips decoded PNG rows to match TextureLoader's default WebGL upload orientation. */
        RgbaImageData flipBillboardTextureRows(const RgbaImageData &source)
        {
            RgbaImageData result = source;
            const size_t rowByteCount =
                static_cast<size_t>(source.width) * 4u;
            for (uint32_t row = 0u; row < source.height; ++row)
            {
                eastl::copy_n(
                    source.pixels.begin() +
                        static_cast<size_t>(source.height - 1u - row) *
                            rowByteCount,
                    rowByteCount,
                    result.pixels.begin() +
                        static_cast<size_t>(row) * rowByteCount);
            }
            return result;
        }

        /** Builds the mesh-scale model-view and WebGL-compatible perspective matrices. */
        BuffergeometryBillboardsHostObjectData buildBillboardsObjectData(
            uint32_t width,
            uint32_t height,
            uint32_t frameIndex)
        {
            const double time =
                billboardsVirtualTimeMilliseconds(frameIndex) * 0.0005;
            glm::mat4 model(1.0f);
            model = glm::rotate(
                model,
                float(time * 0.2),
                glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(
                model,
                float(time * 0.4),
                glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(
                model,
                glm::vec3(500.0f, 500.0f, 500.0f));
            const glm::mat4 view = glm::translate(
                glm::mat4(1.0f),
                glm::vec3(0.0f, 0.0f, -1400.0f));

            constexpr double Pi = 3.14159265358979323846;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 5000.0;
            const double top =
                NearDistance * std::tan(50.0 * Pi / 360.0);
            const double projectionHeight = top * 2.0;
            const double projectionWidth =
                (double(width) / double(height)) *
                projectionHeight;
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] =
                float(2.0 * NearDistance / projectionWidth);
            projection[1u][1u] =
                float(-2.0 * NearDistance / projectionHeight);
            projection[2u][2u] =
                float(-(FarDistance + NearDistance) / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] =
                float(-2.0 * FarDistance * NearDistance / depth);
            return {
                .modelView = view * model,
                .projection = projection,
                .timeAndFlags = {float(time), 0.0f, 0.0f, 0.0f}};
        }

        /** Appends one typed host buffer payload to a RenderSet allocation. */
        void appendBillboardsBufferPayload(
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

        /** Appends the pinned circle mip chain to the entity texture component. */
        void appendBillboardsCircleTexture(
            GVM::Core::RenderSetAllocInfo &allocation,
            uint32_t extent,
            const eastl::vector<uint8_t> &textureBytes,
            const eastl::vector<uint64_t> &mipOffsets)
        {
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebglBuffergeometryInstancingBillboardsSceneRenderSetComponents::
                    textures;
            textureComponent.textures.push_back({
                .textureName =
                    "WebglBuffergeometryInstancingBillboardsCircle",
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = extent,
                .height = extent,
                .data = textureBytes.data(),
                .dataStorageBytes = textureBytes.size(),
                .mipmapOffsetBytes = mipOffsets,
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
        }
    } // namespace

    void BuffergeometryBillboardsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateBillboardsScenario(options);
        device = inDevice;
        buildBillboardsCircleGeometry(vertices, indices);
        finalRandomState = buildBillboardsInstances(instances);
        const BuffergeometryBillboardsHostObjectData objectData =
            buildBillboardsObjectData(
                options.width,
                options.height,
                options.targetFrame);
        const BuffergeometryBillboardsHostMaterialData materialData = {
            .colorMultiplier = {1.0f, 1.0f, 1.0f, 1.0f}};

        const std::filesystem::path circlePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "sprites" /
            "circle.png";
        const RgbaImageData circle =
            flipBillboardTextureRows(
                decodeStraightPngRgba8(circlePath));
        if (circle.width != circle.height || circle.width == 0u)
        {
            throw std::runtime_error(
                "Billboard circle asset must remain a nonempty square RGBA8 image.");
        }
        const eastl::vector<RgbaImageData> mipChain =
            buildUnormMipChain(circle);
        for (const RgbaImageData &mip : mipChain)
        {
            circleMipOffsets.push_back(circleTextureBytes.size());
            circleTextureBytes.insert(
                circleTextureBytes.end(),
                mip.pixels.begin(),
                mip.pixels.end());
        }

        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Billboard example could not create its Scene RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(indices.size());
        allocation.instanceCount = InstanceCount;
        appendBillboardsBufferPayload(
            allocation,
            WebglBuffergeometryInstancingBillboardsSceneRenderSetComponents::
                vertices,
            "WebglBuffergeometryInstancingBillboardsVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]),
            1u);
        appendBillboardsBufferPayload(
            allocation,
            WebglBuffergeometryInstancingBillboardsSceneRenderSetComponents::
                indices,
            "WebglBuffergeometryInstancingBillboardsIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]),
            1u);
        appendBillboardsBufferPayload(
            allocation,
            WebglBuffergeometryInstancingBillboardsSceneRenderSetComponents::
                objects,
            "WebglBuffergeometryInstancingBillboardsObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendBillboardsBufferPayload(
            allocation,
            WebglBuffergeometryInstancingBillboardsSceneRenderSetComponents::
                instances,
            "WebglBuffergeometryInstancingBillboardsInstances",
            instances.data(),
            instances.size() * sizeof(instances[0u]),
            InstanceCount);
        appendBillboardsBufferPayload(
            allocation,
            WebglBuffergeometryInstancingBillboardsSceneRenderSetComponents::
                materials,
            "WebglBuffergeometryInstancingBillboardsMaterial",
            &materialData,
            sizeof(materialData),
            1u);
        appendBillboardsCircleTexture(
            allocation,
            circle.width,
            circleTextureBytes,
            circleMipOffsets);
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void BuffergeometryBillboardsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void BuffergeometryBillboardsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Billboard capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
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

    void BuffergeometryBillboardsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        instances.clear();
        circleTextureBytes.clear();
        circleMipOffsets.clear();
    }

    void BuffergeometryBillboardsRuntimeAdapter::writeArtifacts(
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
            prepareBillboardsOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::out | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write billboard RGBA capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareBillboardsOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"" << options.caseId.c_str() << "\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\""
                << threeSampleBackendName(options.backend)
                << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << width * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\"\n"
                << "}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareBillboardsOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"" << options.caseId.c_str() << "\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":1,\n"
                << "  \"entityCount\":1,\n"
                << "  \"instanceCount\":75000,\n"
                << "  \"vertexCount\":8,\n"
                << "  \"indexCount\":18,\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"screenPassCount\":0,\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"finalRandomState\":" << finalRandomState << ",\n"
                << "  \"renderSetType\":\"WebglBuffergeometryInstancingBillboardsSceneRenderSet\",\n"
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
                << "    \"renderSetType\":\"WebglBuffergeometryInstancingBillboardsSceneRenderSet\",\n"
                << "    \"renderableObjectCount\":1,\n"
                << "    \"entityCount\":1,\n"
                << "    \"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"instanced-billboards\",\"instanceCount\":75000}],\n"
                << "    \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                << "    \"drawCommandCount\":1,\n"
                << "    \"directDrawFallback\":false,\n"
                << "    \"scenePasses\":[{\"name\":\"main-billboards\",\"renderClass\":\"WebglBuffergeometryInstancingBillboardsMainPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
                << "  }],\n"
                << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-billboards\",\"entityOrdinal\":0}],\n"
                << "  \"usesRenderEntityID\":true,\n"
                << "  \"usesRenderEntityInstanceID\":true,\n"
                << "  \"directDrawFallback\":false\n"
                << "}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareBillboardsOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"" << options.caseId.c_str() << "\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"instanceCount\":75000,\n"
                << "  \"finalRandomState\":" << finalRandomState << "\n"
                << "}\n";
        }
    }
} // namespace GVM::ThreeSamples
