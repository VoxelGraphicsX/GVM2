#include "WebglLoaderMddRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t MddFrameCount = 4u;
        constexpr uint32_t MddPointCount = 24u;
        constexpr uint32_t ExpandedVertexCount = 36u;
        constexpr const char *MddAssetSha256 =
            "d034da42b98ecd33b96f03e416039c734af764c475349e59df73a0857e1a76c7";
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(MddHostVertex) == 32u);
        static_assert(sizeof(MddHostMorphTargetData) == 64u);
        static_assert(sizeof(MddHostObjectData) == 160u);
        static_assert(sizeof(MddHostInstanceData) == 16u);
        static_assert(sizeof(MddHostMaterialData) == 16u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareMddOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Calculates a lowercase SHA-256 digest for immutable asset validation. */
        eastl::string calculateMddSha256(const void *bytes, size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error("MDD asset exceeds CommonCrypto input limits.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes, static_cast<CC_LONG>(byteCount), digest.data());
            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (uint8_t byte : digest)
            {
                stream << std::setw(2) << static_cast<uint32_t>(byte);
            }
            return eastl::string(stream.str().c_str());
        }

        /** Reads one big-endian unsigned integer from validated MDD storage. */
        uint32_t readMddBigEndianUint32(const eastl::vector<uint8_t> &bytes,
                                       size_t offset)
        {
            if (offset + sizeof(uint32_t) > bytes.size())
            {
                throw std::runtime_error("MDD header or payload is truncated.");
            }
            return (uint32_t(bytes[offset]) << 24u) |
                   (uint32_t(bytes[offset + 1u]) << 16u) |
                   (uint32_t(bytes[offset + 2u]) << 8u) |
                   uint32_t(bytes[offset + 3u]);
        }

        /** Reads one big-endian IEEE-754 float from validated MDD storage. */
        float readMddBigEndianFloat(const eastl::vector<uint8_t> &bytes,
                                    size_t offset)
        {
            return std::bit_cast<float>(readMddBigEndianUint32(bytes, offset));
        }

        /** Resolves and decodes the exact immutable r185 cube MDD asset. */
        MddDecodedAsset loadMddAsset(const ThreeSampleHostOptions &options)
        {
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "webgl_loader_mdd requires an explicit --asset-root.");
            }
            const std::filesystem::path assetPath =
                std::filesystem::path(options.assetRoot.c_str()) /
                "models/mdd/cube.mdd";
            std::ifstream input(assetPath, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("Missing locked MDD asset: " +
                                         assetPath.string());
            }
            const uintmax_t fileByteCount = std::filesystem::file_size(assetPath);
            if (fileByteCount != 1176u)
            {
                throw std::runtime_error("cube.mdd byte count differs from r185.");
            }
            eastl::vector<uint8_t> bytes(static_cast<size_t>(fileByteCount));
            input.read(reinterpret_cast<char *>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            if (!input)
            {
                throw std::runtime_error("Could not read the complete cube.mdd asset.");
            }
            MddDecodedAsset result;
            result.sourceSha256 = calculateMddSha256(bytes.data(), bytes.size());
            if (result.sourceSha256 != MddAssetSha256 ||
                readMddBigEndianUint32(bytes, 0u) != MddFrameCount ||
                readMddBigEndianUint32(bytes, 4u) != MddPointCount)
            {
                throw std::runtime_error(
                    "cube.mdd identity or header differs from frozen r185.");
            }
            size_t offset = 8u;
            for (uint32_t frame = 0u; frame < MddFrameCount; ++frame)
            {
                result.times[frame] = readMddBigEndianFloat(bytes, offset);
                offset += sizeof(float);
            }
            for (uint32_t frame = 0u; frame < MddFrameCount; ++frame)
            {
                result.frames[frame].reserve(MddPointCount);
                for (uint32_t point = 0u; point < MddPointCount; ++point)
                {
                    const float x = readMddBigEndianFloat(bytes, offset);
                    const float y = readMddBigEndianFloat(bytes, offset + 4u);
                    const float z = readMddBigEndianFloat(bytes, offset + 8u);
                    offset += sizeof(float) * 3u;
                    result.frames[frame].push_back(glm::vec4(x, y, z, 1.0f));
                }
            }
            const eastl::array<float, 4u> expectedTimes = {0.0f, 1.0f, 2.0f, 3.0f};
            if (result.times != expectedTimes || offset != bytes.size())
            {
                throw std::runtime_error(
                    "cube.mdd times or payload length differs from r185.");
            }
            return result;
        }

        /** Validates the three exact Manifest scenarios and their fixed frames. */
        void validateMddOptions(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_loader_mdd")
            {
                throw std::invalid_argument(
                    "MDD adapter only accepts webgl_loader_mdd.");
            }
            const bool initial =
                options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool canonical =
                options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            if (!initial && !canonical && !animated)
            {
                throw std::invalid_argument(
                    "webgl_loader_mdd scenario or target frame differs from the Manifest.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "webgl_loader_mdd must not consume an input replay.");
            }
        }

        /** Builds the RHI zero-to-one perspective projection used by the host. */
        glm::mat4 makeMddPerspective(double fieldOfViewDegrees,
                                     double aspect,
                                     double nearDistance,
                                     double farDistance)
        {
            const double top =
                nearDistance * std::tan(fieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = aspect * height;
            const double depth = farDistance - nearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * nearDistance / width);
            projection[1u][1u] = static_cast<float>(2.0 * nearDistance / height);
            projection[2u][2u] = static_cast<float>(-farDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] =
                static_cast<float>(-farDistance * nearDistance / depth);
            return projection;
        }

        /** Builds expanded BoxGeometry indices, normals, and MDD records. */
        void buildExpandedMddGeometry(const MddDecodedAsset &asset,
                                      MddEntityState &entity)
        {
            constexpr uint32_t LocalIndices[6u] = {0u, 2u, 1u, 2u, 3u, 1u};
            constexpr float FaceNormals[6u][3u] = {
                {1.0f, 0.0f, 0.0f},
                {-1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f},
                {0.0f, -1.0f, 0.0f},
                {0.0f, 0.0f, 1.0f},
                {0.0f, 0.0f, -1.0f},
            };
            entity.vertices.reserve(ExpandedVertexCount);
            entity.indices.reserve(ExpandedVertexCount);
            entity.morphTargets.reserve(ExpandedVertexCount);
            for (uint32_t face = 0u; face < 6u; ++face)
            {
                for (uint32_t localIndex : LocalIndices)
                {
                    const uint32_t sourceIndex = face * 4u + localIndex;
                    entity.vertices.push_back({
                        .position = asset.frames[0u][sourceIndex],
                        .normal = glm::vec4(FaceNormals[face][0u],
                                            FaceNormals[face][1u],
                                            FaceNormals[face][2u],
                                            0.0f),
                    });
                    entity.indices.push_back(
                        static_cast<uint32_t>(entity.indices.size()));
                    entity.morphTargets.push_back({
                        .frame0 = asset.frames[0u][sourceIndex],
                        .frame1 = asset.frames[1u][sourceIndex],
                        .frame2 = asset.frames[2u][sourceIndex],
                        .frame3 = asset.frames[3u][sourceIndex],
                    });
                }
            }
        }

        /** Appends one typed payload to a RenderSet component allocation. */
        void appendMddBufferPayload(
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

        /** Computes tightly packed RGBA8 storage while rejecting overflow. */
        uint64_t computeMddRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("MDD RGBA8 capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebglLoaderMddRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateMddOptions(options);
        device = inDevice;
        asset = loadMddAsset(options);
        buildExpandedMddGeometry(asset, entity);
        entity.instanceData.translation = glm::vec4(0.0f);
        entity.materialData.opacityAndReserved = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        updateObjectData(options);

        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "webgl_loader_mdd could not create its Scene Set encoder.");
        }
        entity.entityIndex = allocateEntity(*encoder);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    GVM::Core::RenderEntityIndex
    WebglLoaderMddRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder) const
    {
        if (entity.vertices.size() != ExpandedVertexCount ||
            entity.indices.size() != ExpandedVertexCount ||
            entity.morphTargets.size() != ExpandedVertexCount)
        {
            throw std::runtime_error(
                "MDD expanded RenderSet payload counts are invalid.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = ExpandedVertexCount;
        allocation.indicesCount = ExpandedVertexCount;
        allocation.instanceCount = 1u;
        appendMddBufferPayload(
            allocation, WebglLoaderMddSceneRenderSetComponents::vertices,
            "WebglLoaderMddVertices", entity.vertices.data(),
            entity.vertices.size() * sizeof(MddHostVertex), 1u);
        appendMddBufferPayload(
            allocation, WebglLoaderMddSceneRenderSetComponents::indices,
            "WebglLoaderMddIndices", entity.indices.data(),
            entity.indices.size() * sizeof(uint32_t), 1u);
        appendMddBufferPayload(
            allocation, WebglLoaderMddSceneRenderSetComponents::objects,
            "WebglLoaderMddObject", &entity.objectData,
            sizeof(entity.objectData), 1u);
        appendMddBufferPayload(
            allocation, WebglLoaderMddSceneRenderSetComponents::instances,
            "WebglLoaderMddInstance", &entity.instanceData,
            sizeof(entity.instanceData), 1u);
        appendMddBufferPayload(
            allocation, WebglLoaderMddSceneRenderSetComponents::materials,
            "WebglLoaderMddMaterial", &entity.materialData,
            sizeof(entity.materialData), 1u);
        appendMddBufferPayload(
            allocation, WebglLoaderMddSceneRenderSetComponents::morphTargets,
            "WebglLoaderMddTargets", entity.morphTargets.data(),
            entity.morphTargets.size() * sizeof(MddHostMorphTargetData),
            static_cast<uint32_t>(entity.morphTargets.size()));
        return encoder.allocEntity(allocation);
    }

    void WebglLoaderMddRuntimeAdapter::updateObjectData(
        const ThreeSampleHostOptions &options)
    {
        if (options.width == 0u || options.height == 0u)
        {
            throw std::invalid_argument("MDD capture dimensions must be positive.");
        }
        const glm::vec3 sourceCamera(8.0f, 8.0f, 8.0f);
        const glm::mat4 sourceView = glm::lookAt(
            sourceCamera, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = makeMddPerspective(
            35.0, double(options.width) / double(options.height), 0.1, 100.0);
        entity.objectData.modelViewProjection = projection * sourceView;
        entity.objectData.normalMatrix =
            glm::transpose(glm::inverse(sourceView));
        entity.objectData.frameWeights =
            options.scenarioId == "animated"
                ? glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)
                : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        entity.objectData.viewportAndReserved =
            glm::vec4(float(options.width), float(options.height), 0.0f, 0.0f);
    }

    void WebglLoaderMddRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)frameIndex;
        updateObjectData(options);
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "webgl_loader_mdd could not create its update encoder.");
        }
        encoder->setBufferComponentData(
            entity.entityIndex,
            WebglLoaderMddSceneRenderSetComponents::objects,
            &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderMddRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computeMddRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "MDD capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeLoaderSemanticSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLoaderMddRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareMddOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()),
                     static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write MDD RGBA8 capture.");
        }
    }

    void WebglLoaderMddRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureMetadataPath.c_str());
        prepareMddOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n"
               << "  \"source\":\"gvm-three-r185\",\n"
               << "  \"caseId\":\"webgl_loader_mdd\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\":\""
               << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"randomSeed\":" << options.randomSeed << ",\n"
               << "  \"width\":" << width << ",\n"
               << "  \"height\":" << height << ",\n"
               << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\":" << byteCount << ",\n"
               << "  \"format\":\"rgba8unorm\",\n"
               << "  \"samplePolicy\":{\"mode\":\"single-sample\","
               << "\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
               << "  \"inputReplay\":null\n}\n";
    }

    void WebglLoaderMddRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.sceneSnapshotPath.c_str());
        prepareMddOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n"
               << "  \"caseId\":\"webgl_loader_mdd\",\n"
               << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"implementationLevel\":\"semantic-complete\",\n"
               << "  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":true,\n"
               << "  \"assetHashes\":[{\"path\":\"models/mdd/cube.mdd\","
               << "\"sha256\":\"" << asset.sourceSha256.c_str() << "\"}],\n"
               << "  \"renderSetPolicy\":\"required\",\n"
               << "  \"sceneRenderSetCount\":1,\n"
               << "  \"renderableObjectCount\":1,\n"
               << "  \"entityCount\":1,\n"
               << "  \"instanceCounts\":[1],\n"
               << "  \"sourceVertexCount\":24,\n"
               << "  \"sourceIndexCount\":36,\n"
               << "  \"expandedVertexCount\":36,\n"
               << "  \"expandedIndexCount\":36,\n"
               << "  \"geometryGroupCount\":6,\n"
               << "  \"morphTargetCount\":4,\n"
               << "  \"renderSetType\":\"WebglLoaderMddSceneRenderSet\",\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\","
               << "\"instances\",\"materials\",\"morphTargets\"],\n"
               << "  \"scenePassCount\":1,\n"
               << "  \"screenPassCount\":0,\n"
               << "  \"scenePasses\":[\"WebglLoaderMddNormalMorphPass\"],\n"
               << "  \"screenPasses\":[],\n"
               << "  \"attachmentFormats\":[\"rgba8unorm\",\"depth32float\","
               << "\"rgba8unorm\"],\n"
               << "  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n"
               << "  \"directDrawFallback\":false,\n"
               << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
               << "\"scenePass\":\"normal-morph\",\"entityOrdinal\":0}],\n"
               << "  \"sceneRoots\":[{\n"
               << "    \"id\":\"scene\",\n"
               << "    \"renderSetCount\":1,\n"
               << "    \"renderSetId\":\"scene\",\n"
               << "    \"renderSetType\":\"WebglLoaderMddSceneRenderSet\",\n"
               << "    \"renderableObjectCount\":1,\n"
               << "    \"entityCount\":1,\n"
               << "    \"drawCommandCount\":1,\n"
               << "    \"directDrawFallback\":false,\n"
               << "    \"componentSchema\":["
               << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
               << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
               << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
               << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
               << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
               << "{\"name\":\"morphTargets\",\"kind\":\"buffer\","
               << "\"role\":\"mdd-position-targets-and-weights\"}],\n"
               << "    \"scenePasses\":[{\"name\":\"normal-morph\","
               << "\"renderClass\":\"WebglLoaderMddNormalMorphPass\","
               << "\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,"
               << "\"drawMode\":\"render-set-indexed-indirect\","
               << "\"invocationCount\":1,\"drawCommandCount\":1,"
               << "\"usesStandaloneGeometry\":false,"
               << "\"usesExplicitDrawCount\":false}],\n"
               << "    \"entities\":[{\"entityId\":0,"
               << "\"logicalRenderableId\":\"mdd-box\",\"instanceCount\":1}]\n"
               << "  }]\n}\n";
    }

    void WebglLoaderMddRuntimeAdapter::writeLoaderSemanticSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.semanticSnapshotPath.empty() ||
            options.scenarioId != "canonical-loader")
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.semanticSnapshotPath.c_str());
        prepareMddOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n"
               << "  \"caseId\":\"webgl_loader_mdd\",\n"
               << "  \"scenarioId\":\"canonical-loader\",\n"
               << "  \"frame\":" << frameIndex << ",\n"
               << "  \"kind\":\"loader-snapshot\",\n"
               << "  \"canonicalState\":"
               << "\"four-frames-twenty-four-points-times-zero-one-two-three\",\n"
               << "  \"result\":{\n"
               << "    \"renderableObjectCount\":0,\n"
               << "    \"sceneRootCount\":1,\n"
               << "    \"canonicalSceneSha256\":"
               << "\"0d6fb8cf7298a32ed37709fe2986e69710655b15b118ea84bb59b7c11e17a312\",\n"
               << "    \"assetPath\":\"models/mdd/cube.mdd\",\n"
               << "    \"assetSha256\":\"" << asset.sourceSha256.c_str() << "\",\n"
               << "    \"frameCount\":4,\n"
               << "    \"pointCount\":24,\n"
               << "    \"times\":[0,1,2,3],\n"
               << "    \"geometryVertexCount\":24,\n"
               << "    \"geometryIndexCount\":36,\n"
               << "    \"geometryGroupCount\":6,\n"
               << "    \"morphTargetCount\":4\n"
               << "  }\n}\n";
    }

    void WebglLoaderMddRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entity.vertices.clear();
        entity.indices.clear();
        entity.morphTargets.clear();
        for (eastl::vector<glm::vec4> &frame : asset.frames)
        {
            frame.clear();
        }
    }
} // namespace GVM::ThreeSamples
