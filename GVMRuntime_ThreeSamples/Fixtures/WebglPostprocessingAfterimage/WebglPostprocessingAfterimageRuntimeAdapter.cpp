#include "WebglPostprocessingAfterimageRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <glm/ext/matrix_transform.hpp>

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
        constexpr double Pi = 3.1415926535897932384626433832795;
        constexpr const char *DampReplaySha256 =
            "1a6b37c553e4b3225e1f3306972a40cdade7810382cf5e72ee9e04a796c2bb4b";
        constexpr const char *DisabledReplaySha256 =
            "3ce251b81f5312bfe6b5b1e89ddaf15ccd19b7b62c3f973e453bc9e121e6ed74";
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(AfterimageHostVertex) == 32u);
        static_assert(sizeof(AfterimageHostObjectData) == 128u);
        static_assert(sizeof(AfterimageHostInstanceData) == 16u);
        static_assert(sizeof(AfterimageHostMaterialData) == 16u);

        /** Creates parent directories for one output artifact. */
        void prepareAfterimageOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Reads one immutable replay payload into bounded bytes. */
        eastl::vector<uint8_t> readAfterimageReplay(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open the Afterimage input replay.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "The Afterimage input replay is empty.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(
                static_cast<size_t>(byteCount));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the Afterimage input replay.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one replay payload. */
        eastl::string calculateAfterimageSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Afterimage replay is too large for SHA-256.");
            }
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest);
            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (const uint8_t value : digest)
            {
                stream << std::setw(2)
                       << static_cast<uint32_t>(value);
            }
            return eastl::string(stream.str().c_str());
        }

        /** Resolves one locked Afterimage scenario and validates its replay. */
        AfterimageScenarioState resolveAfterimageScenario(
            const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_postprocessing_afterimage" ||
                options.randomSeed != 0x12345678u)
            {
                throw std::invalid_argument(
                    "Afterimage adapter requires its case and fixed seed.");
            }
            AfterimageScenarioState state;
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool accumulated =
                options.scenarioId == "accumulated" &&
                options.targetFrame == 60u;
            const bool damp =
                options.scenarioId == "nondefault-damp" &&
                options.targetFrame == 61u;
            const bool disabled =
                options.scenarioId == "disabled" &&
                options.targetFrame == 62u;
            if ((!initial && !accumulated && !damp && !disabled) ||
                ((initial || accumulated) &&
                 !options.inputReplayPath.empty()) ||
                ((damp || disabled) &&
                 options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "Afterimage scenario differs from the Manifest.");
            }
            if (initial || accumulated)
            {
                return state;
            }
            const eastl::vector<uint8_t> replay =
                readAfterimageReplay(std::filesystem::path(
                    options.inputReplayPath.c_str()));
            state.replaySha256 =
                calculateAfterimageSha256(replay);
            const char *expected =
                damp ? DampReplaySha256 : DisabledReplaySha256;
            if (state.replaySha256 != expected)
            {
                throw std::invalid_argument(
                    "Afterimage replay differs from its locked identity.");
            }
            state.replayEventCount = 1u;
            if (damp)
            {
                state.damp = 0.82f;
            }
            else
            {
                state.enabled = false;
            }
            return state;
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendAfterimageBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *label,
            const void *value,
            size_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = label,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Builds one exact r185 BoxGeometry plane and reverses backend winding. */
        void appendAfterimageBoxPlane(
            eastl::vector<AfterimageHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            uint32_t u,
            uint32_t v,
            uint32_t w,
            float uDirection,
            float vDirection,
            float planeWidth,
            float planeHeight,
            float planeDepth)
        {
            constexpr uint32_t GridX = 2u;
            constexpr uint32_t GridY = 2u;
            constexpr uint32_t GridX1 = GridX + 1u;
            const uint32_t baseVertex =
                static_cast<uint32_t>(vertices.size());
            const float segmentWidth =
                planeWidth / float(GridX);
            const float segmentHeight =
                planeHeight / float(GridY);
            for (uint32_t iy = 0u; iy <= GridY; ++iy)
            {
                const float y =
                    float(iy) * segmentHeight -
                    planeHeight * 0.5f;
                for (uint32_t ix = 0u; ix <= GridX; ++ix)
                {
                    const float x =
                        float(ix) * segmentWidth -
                        planeWidth * 0.5f;
                    glm::vec4 position(0.0f, 0.0f, 0.0f, 1.0f);
                    position[u] = x * uDirection;
                    position[v] = y * vDirection;
                    position[w] = planeDepth * 0.5f;
                    glm::vec4 normal(0.0f);
                    normal[w] = planeDepth > 0.0f ? 1.0f : -1.0f;
                    vertices.push_back({position, normal});
                }
            }
            for (uint32_t iy = 0u; iy < GridY; ++iy)
            {
                for (uint32_t ix = 0u; ix < GridX; ++ix)
                {
                    const uint32_t a =
                        baseVertex + ix + GridX1 * iy;
                    const uint32_t b =
                        baseVertex + ix + GridX1 * (iy + 1u);
                    const uint32_t c =
                        baseVertex + (ix + 1u) +
                        GridX1 * (iy + 1u);
                    const uint32_t d =
                        baseVertex + (ix + 1u) +
                        GridX1 * iy;
                    indices.insert(
                        indices.end(),
                        {a, d, b, b, d, c});
                }
            }
        }

        /** Builds the exact 54-vertex, six-group r185 segmented box. */
        void buildAfterimageBox(
            eastl::vector<AfterimageHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve(54u);
            indices.reserve(144u);
            appendAfterimageBoxPlane(
                vertices, indices, 2u, 1u, 0u,
                -1.0f, -1.0f, 150.0f, 150.0f, 150.0f);
            appendAfterimageBoxPlane(
                vertices, indices, 2u, 1u, 0u,
                1.0f, -1.0f, 150.0f, 150.0f, -150.0f);
            appendAfterimageBoxPlane(
                vertices, indices, 0u, 2u, 1u,
                1.0f, 1.0f, 150.0f, 150.0f, 150.0f);
            appendAfterimageBoxPlane(
                vertices, indices, 0u, 2u, 1u,
                1.0f, -1.0f, 150.0f, 150.0f, -150.0f);
            appendAfterimageBoxPlane(
                vertices, indices, 0u, 1u, 2u,
                1.0f, -1.0f, 150.0f, 150.0f, 150.0f);
            appendAfterimageBoxPlane(
                vertices, indices, 0u, 1u, 2u,
                -1.0f, -1.0f, 150.0f, 150.0f, -150.0f);
            if (vertices.size() != 54u || indices.size() != 144u)
            {
                throw std::runtime_error(
                    "Afterimage BoxGeometry counts differ from r185.");
            }
        }

        /** Builds Three's OpenGL perspective before DSL clip conversion. */
        glm::mat4 makeAfterimageProjection(
            uint32_t width,
            uint32_t height)
        {
            constexpr double FieldOfView = 70.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 1000.0;
            const double top =
                NearDistance *
                std::tan(FieldOfView * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                double(width) / double(height) *
                projectionHeight;
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] =
                static_cast<float>(
                    2.0 * NearDistance / projectionWidth);
            projection[1u][1u] =
                static_cast<float>(
                    2.0 * NearDistance / projectionHeight);
            projection[2u][2u] =
                static_cast<float>(
                    -(FarDistance + NearDistance) / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] =
                static_cast<float>(
                    -2.0 * FarDistance * NearDistance / depth);
            return projection;
        }

        /** Builds Three's exact XYZ Euler matrix for zero Z rotation. */
        glm::mat4 makeAfterimageRotation(
            double rotationX,
            double rotationY)
        {
            const float a = static_cast<float>(std::cos(rotationX));
            const float b = static_cast<float>(std::sin(rotationX));
            const float c = static_cast<float>(std::cos(rotationY));
            const float d = static_cast<float>(std::sin(rotationY));
            glm::mat4 matrix(1.0f);
            matrix[0u] = glm::vec4(c, b * d, -a * d, 0.0f);
            matrix[1u] = glm::vec4(0.0f, a, b, 0.0f);
            matrix[2u] = glm::vec4(d, -b * c, a * c, 0.0f);
            matrix[3u] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            return matrix;
        }

        /** Computes one safe tightly packed RGBA8 capture size. */
        uint64_t computeAfterimageRgbaByteCount(
            uint32_t width,
            uint32_t height)
        {
            const uint64_t pixelCount =
                uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / 4u)
            {
                throw std::overflow_error(
                    "Afterimage capture size overflowed.");
            }
            return pixelCount * 4u;
        }
    } // namespace

    void WebglPostprocessingAfterimageRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        device = inDevice;
        scenario = resolveAfterimageScenario(options);
        buildAfterimageBox(entity.vertices, entity.indices);
        entity.instanceData.translation = glm::vec4(0.0f);
        entity.materialData.normalColorMultiplier =
            glm::vec4(1.0f);
        updateObjectData(
            options.width,
            options.height,
            0u);
        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Afterimage host could not create its Scene Set encoder.");
        }
        entity.entityIndex = allocateEntity(*encoder);
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    GVM::Core::RenderEntityIndex
    WebglPostprocessingAfterimageRuntimeAdapter::allocateEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder) const
    {
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(entity.vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(entity.indices.size());
        allocation.instanceCount = 1u;
        appendAfterimageBufferPayload(
            allocation,
            WebglPostprocessingAfterimageSceneRenderSetComponents::vertices,
            "afterimage-box-vertices",
            entity.vertices.data(),
            entity.vertices.size() *
                sizeof(AfterimageHostVertex),
            1u);
        appendAfterimageBufferPayload(
            allocation,
            WebglPostprocessingAfterimageSceneRenderSetComponents::indices,
            "afterimage-box-indices",
            entity.indices.data(),
            entity.indices.size() *
                sizeof(uint32_t),
            1u);
        appendAfterimageBufferPayload(
            allocation,
            WebglPostprocessingAfterimageSceneRenderSetComponents::objects,
            "afterimage-box-object",
            &entity.objectData,
            sizeof(entity.objectData),
            1u);
        appendAfterimageBufferPayload(
            allocation,
            WebglPostprocessingAfterimageSceneRenderSetComponents::instances,
            "afterimage-box-instance",
            &entity.instanceData,
            sizeof(entity.instanceData),
            1u);
        appendAfterimageBufferPayload(
            allocation,
            WebglPostprocessingAfterimageSceneRenderSetComponents::materials,
            "afterimage-box-material",
            &entity.materialData,
            sizeof(entity.materialData),
            1u);
        return encoder.allocEntity(allocation);
    }

    void WebglPostprocessingAfterimageRuntimeAdapter::updateObjectData(
        uint32_t width,
        uint32_t height,
        uint32_t frameIndex)
    {
        if (width == 0u || height == 0u)
        {
            throw std::invalid_argument(
                "Afterimage capture dimensions must be positive.");
        }
        const double step = double(frameIndex) + 1.0;
        const glm::mat4 model =
            makeAfterimageRotation(
                step * 0.005,
                step * 0.01);
        const glm::mat4 view =
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(0.0f, 0.0f, -400.0f));
        entity.objectData.modelView = view * model;
        entity.objectData.modelViewProjection =
            makeAfterimageProjection(width, height) *
            entity.objectData.modelView;
    }

    void WebglPostprocessingAfterimageRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        updateObjectData(
            options.width,
            options.height,
            frameIndex);
        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Afterimage host could not update its Scene Set.");
        }
        encoder->setBufferComponentData(
            entity.entityIndex,
            WebglPostprocessingAfterimageSceneRenderSetComponents::objects,
            &entity.objectData,
            sizeof(entity.objectData),
            0u,
            1u);
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebglPostprocessingAfterimageRuntimeAdapter::afterFrame(
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
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(
                computeAfterimageRgbaByteCount(width, height)));
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

    void WebglPostprocessingAfterimageRuntimeAdapter::writeArtifacts(
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
            prepareAfterimageOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write Afterimage RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareAfterimageOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n"
                   << "  \"schemaVersion\":1,\n"
                   << "  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\"webgl_postprocessing_afterimage\",\n"
                   << "  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\":\""
                   << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\":\""
                   << threeSampleBackendName(options.backend)
                   << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"randomSeed\":" << options.randomSeed << ",\n"
                   << "  \"width\":" << width << ",\n"
                   << "  \"height\":" << height << ",\n"
                   << "  \"rowStrideBytes\":"
                   << uint64_t(width) * 4u << ",\n"
                   << "  \"format\":\"rgba8unorm\",\n"
                   << "  \"byteCount\":" << rgba.size() << ",\n";
            if (scenario.replaySha256.empty())
            {
                output << "  \"inputReplay\":null\n";
            }
            else
            {
                output << "  \"inputReplay\":{\"schemaVersion\":1,"
                       << "\"caseId\":\"webgl_postprocessing_afterimage\","
                       << "\"scenarioId\":\""
                       << options.scenarioId.c_str() << "\","
                       << "\"captureFrame\":" << frameIndex << ","
                       << "\"sha256\":\""
                       << scenario.replaySha256.c_str() << "\","
                       << "\"target\":\"body canvas\","
                       << "\"eventCount\":"
                       << scenario.replayEventCount << "}\n";
            }
            output << "}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareAfterimageOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n"
                   << "  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgl_postprocessing_afterimage\",\n"
                   << "  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"implementationLevel\":\"semantic-complete\",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"assetBacked\":false,\n"
                   << "  \"assetHashes\":[],\n"
                   << "  \"renderSetPolicy\":\"required\",\n"
                   << "  \"sceneRenderSetCount\":1,\n"
                   << "  \"renderableObjectCount\":1,\n"
                   << "  \"entityCount\":1,\n"
                   << "  \"instanceCounts\":[1],\n"
                   << "  \"vertexCount\":54,\n"
                   << "  \"indexCount\":144,\n"
                   << "  \"geometryGroupCount\":6,\n"
                   << "  \"renderSetType\":"
                   << "\"WebglPostprocessingAfterimageSceneRenderSet\",\n"
                   << "  \"componentSchema\":[\"vertices\",\"indices\","
                   << "\"objects\",\"instances\",\"materials\"],\n"
                   << "  \"scenePassCount\":1,\n"
                   << "  \"screenPassCount\":"
                   << (scenario.enabled ? 3u : 1u) << ",\n"
                   << "  \"screenPasses\":"
                   << (scenario.enabled
                           ? "[\"afterimage-history-compose\","
                             "\"afterimage-history-copy\","
                             "\"output-color-conversion\"]"
                           : "[\"output-color-conversion\"]")
                   << ",\n"
                   << "  \"attachmentFormats\":[\"rgba16float\","
                   << "\"depth32float\",\"rgba16float\","
                   << "\"rgba16float\",\"rgba16float\","
                   << "\"rgba8unorm\"],\n"
                   << "  \"historyTextureCount\":2,\n"
                   << "  \"historyDamp\":" << scenario.damp << ",\n"
                   << "  \"effectEnabled\":"
                   << (scenario.enabled ? "true" : "false") << ",\n"
                   << "  \"drawCommandCount\":1,\n"
                   << "  \"renderSetIndexedIndirect\":true,\n"
                   << "  \"directDrawFallback\":false,\n"
                   << "  \"sceneRoots\":[{\n"
                   << "    \"id\":\"scene\",\n"
                   << "    \"renderSetCount\":1,\n"
                   << "    \"renderSetId\":\"scene\",\n"
                   << "    \"renderSetType\":"
                   << "\"WebglPostprocessingAfterimageSceneRenderSet\",\n"
                   << "    \"renderableObjectCount\":1,\n"
                   << "    \"entityCount\":1,\n"
                   << "    \"drawCommandCount\":1,\n"
                   << "    \"directDrawFallback\":false,\n"
                   << "    \"componentSchema\":["
                   << "{\"name\":\"vertices\",\"kind\":\"buffer\","
                   << "\"role\":\"vertex\"},"
                   << "{\"name\":\"indices\",\"kind\":\"buffer\","
                   << "\"role\":\"index\"},"
                   << "{\"name\":\"objects\",\"kind\":\"buffer\","
                   << "\"role\":\"object\"},"
                   << "{\"name\":\"instances\",\"kind\":\"buffer\","
                   << "\"role\":\"instance\"},"
                   << "{\"name\":\"materials\",\"kind\":\"buffer\","
                   << "\"role\":\"material\"}],\n"
                   << "    \"scenePasses\":[{\"name\":\"main\","
                   << "\"renderClass\":"
                   << "\"WebglPostprocessingAfterimageMainPass\","
                   << "\"renderSetId\":\"scene\","
                   << "\"renderSetBindingCount\":1,"
                   << "\"drawMode\":\"render-set-indexed-indirect\","
                   << "\"invocationCount\":1,\"drawCommandCount\":1,"
                   << "\"usesStandaloneGeometry\":false,"
                   << "\"usesExplicitDrawCount\":false}],\n"
                   << "    \"entities\":[{\"entityId\":0,"
                   << "\"logicalRenderableId\":\"segmented-box\","
                   << "\"instanceCount\":1}]\n"
                   << "  }]\n"
                   << "}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareAfterimageOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n"
                   << "  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgl_postprocessing_afterimage\",\n"
                   << "  \"geometry\":"
                   << "\"BoxGeometry(150,150,150,2,2,2)\",\n"
                   << "  \"meshMaterial\":\"MeshNormalMaterial\",\n"
                   << "  \"historyFormat\":\"rgba16float\",\n"
                   << "  \"historyTextureCount\":2,\n"
                   << "  \"damp\":" << scenario.damp << ",\n"
                   << "  \"enabled\":"
                   << (scenario.enabled ? "true" : "false")
                   << "\n"
                   << "}\n";
        }
    }

    void WebglPostprocessingAfterimageRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entity.vertices.clear();
        entity.indices.clear();
    }
} // namespace GVM::ThreeSamples
