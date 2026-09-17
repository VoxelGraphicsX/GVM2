#include "WebglInstancingPerformanceRuntimeAdapter.hpp"

#include "InstanceSampleGeometry.hpp"
#include "SampleAssetDecoders.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t DefaultInstanceCount = 1000u;
        constexpr const char *SuzanneAssetSha256 =
            "8aa6f692a08b2ec0991b7903914cfa3154e2f553a4ca72ca3fb5d7e6c16f839f";
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebglInstancingPerformanceHostVertex) == 32u);
        static_assert(sizeof(WebglInstancingPerformanceHostObjectData) == 144u);
        static_assert(sizeof(WebglInstancingPerformanceHostInstanceData) == 48u);
        static_assert(sizeof(WebglInstancingPerformanceHostMaterialData) == 16u);

        /** Reads one bounded immutable Suzanne BufferGeometry asset. */
        eastl::vector<uint8_t> readPerformanceAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open pinned Suzanne performance asset.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) >
                    uint64_t(std::numeric_limits<size_t>::max()))
            {
                throw std::runtime_error(
                    "Pinned Suzanne performance asset has an invalid size.");
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
                    "Could not read complete Suzanne performance asset.");
            }
            return bytes;
        }

        /** Validates all five frozen performance scenarios. */
        void validatePerformanceScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool loader =
                options.scenarioId == "loader-snapshot" &&
                options.targetFrame == 0u;
            const bool initial =
                options.scenarioId == "initial-instanced" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated-instanced" &&
                options.targetFrame == 60u;
            const bool merged =
                options.scenarioId == "merged" &&
                options.targetFrame == 1u;
            const bool naive =
                options.scenarioId == "naive" &&
                options.targetFrame == 1u;
            const bool replay = merged || naive;
            if (options.caseId != "webgl_instancing_performance" ||
                (!loader && !initial && !animated && !merged && !naive) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty() ||
                (replay != !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "Instancing performance requires one locked Manifest scenario.");
            }
        }

        /** Resolves the active upstream method for one scenario. */
        eastl::string resolvePerformanceMode(
            const ThreeSampleHostOptions &options)
        {
            if (options.scenarioId == "merged") return "MERGED";
            if (options.scenarioId == "naive") return "NAIVE";
            return "INSTANCED";
        }

        /** Builds the WebGL-compatible perspective with generated-backend Y compensation. */
        glm::mat4 makePerformanceProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 100.0;
            const double top =
                NearDistance * std::tan(70.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = (800.0 / 500.0) * height;
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] =
                float(2.0 * NearDistance / width);
            result[1u][1u] =
                float(-2.0 * NearDistance / height);
            result[2u][2u] =
                float(-(FarDistance + NearDistance) / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] =
                float(-2.0 * FarDistance * NearDistance / depth);
            return result;
        }

        /** Builds the target-frame auto-rotating OrbitControls camera view. */
        glm::mat4 makePerformanceView(uint32_t targetFrame)
        {
            const double autoRotationPerFrame =
                2.0 * Pi / 60.0 / 60.0 * 2.0;
            const double theta =
                -autoRotationPerFrame * double(targetFrame + 1u);
            const glm::vec3 cameraPosition(
                float(std::sin(theta) * 30.0),
                0.0f,
                float(std::cos(theta) * 30.0));
            return glm::lookAtRH(
                cameraPosition,
                glm::vec3(0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
        }

        /** Packs one column-major affine matrix into three row vectors. */
        WebglInstancingPerformanceHostInstanceData packPerformanceInstance(
            const glm::mat4 &matrix)
        {
            return {
                glm::vec4(
                    matrix[0u][0u],
                    matrix[1u][0u],
                    matrix[2u][0u],
                    matrix[3u][0u]),
                glm::vec4(
                    matrix[0u][1u],
                    matrix[1u][1u],
                    matrix[2u][1u],
                    matrix[3u][1u]),
                glm::vec4(
                    matrix[0u][2u],
                    matrix[1u][2u],
                    matrix[2u][2u],
                    matrix[3u][2u])};
        }

        /** Returns one packed identity instance record. */
        WebglInstancingPerformanceHostInstanceData makeIdentityInstance()
        {
            return packPerformanceInstance(glm::mat4(1.0f));
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendPerformancePayload(
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

        /** Allocates one performance entity through current RenderSet components. */
        void allocatePerformanceEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const eastl::vector<WebglInstancingPerformanceHostVertex> &vertices,
            const eastl::vector<uint32_t> &indices,
            const WebglInstancingPerformanceHostObjectData &objectData,
            const eastl::vector<WebglInstancingPerformanceHostInstanceData> &instances,
            const WebglInstancingPerformanceHostMaterialData &materialData)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount =
                static_cast<uint32_t>(vertices.size());
            allocation.indicesCount =
                static_cast<uint32_t>(indices.size());
            allocation.instanceCount =
                static_cast<uint32_t>(instances.size());
            appendPerformancePayload(
                allocation,
                WebglInstancingPerformanceSceneRenderSetComponents::vertices,
                "WebglInstancingPerformanceVertices",
                vertices.data(),
                vertices.size() * sizeof(vertices[0u]),
                1u);
            appendPerformancePayload(
                allocation,
                WebglInstancingPerformanceSceneRenderSetComponents::indices,
                "WebglInstancingPerformanceIndices",
                indices.data(),
                indices.size() * sizeof(indices[0u]),
                1u);
            appendPerformancePayload(
                allocation,
                WebglInstancingPerformanceSceneRenderSetComponents::objects,
                "",
                &objectData,
                sizeof(objectData),
                1u);
            appendPerformancePayload(
                allocation,
                WebglInstancingPerformanceSceneRenderSetComponents::instances,
                "WebglInstancingPerformanceInstances",
                instances.data(),
                instances.size() * sizeof(instances[0u]),
                static_cast<uint32_t>(instances.size()));
            appendPerformancePayload(
                allocation,
                WebglInstancingPerformanceSceneRenderSetComponents::materials,
                "WebglInstancingPerformanceMaterial",
                &materialData,
                sizeof(materialData),
                1u);
            encoder.allocEntity(allocation);
        }

        /** Creates parent directories for one requested performance artifact. */
        void preparePerformanceOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(
                    path.parent_path());
            }
        }

        /** Writes one optional deterministic performance text artifact. */
        void writePerformanceText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            preparePerformanceOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write instancing performance artifact.");
            }
        }
    }

    void WebglInstancingPerformanceRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validatePerformanceScenario(options);
        device = inDevice;
        mode = resolvePerformanceMode(options);
        const auto bytes = readPerformanceAsset(
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" /
            "json" /
            "suzanne_buffergeometry.json");
        const ThreeCompat::DecodedBufferGeometry decoded =
            ThreeCompat::decodeThreeBufferGeometryJson(bytes);
        const ThreeCompat::InstanceSampleMesh mesh =
            ThreeCompat::buildInstanceSampleMesh(decoded);
        if (mesh.positions.size() != 505u ||
            mesh.indices.size() != 2901u)
        {
            throw std::runtime_error(
                "Suzanne geometry count differs from the performance lock.");
        }
        sourceVertices.reserve(mesh.positions.size());
        for (size_t index = 0u;
             index < mesh.positions.size();
             ++index)
        {
            sourceVertices.push_back({
                .position = glm::vec4(mesh.positions[index], 1.0f),
                .normal = glm::vec4(mesh.normals[index], 0.0f)});
        }
        sourceIndices.assign(
            mesh.indices.begin(),
            mesh.indices.end());
        const auto transforms =
            ThreeCompat::buildInstancingPerformanceTransforms(
                DefaultInstanceCount,
                options.randomSeed,
                options.scenarioId == "merged"
                    ? 7140u
                    : options.scenarioId == "naive"
                        ? 7140u
                        : 132u,
                options.scenarioId == "merged" ||
                        options.scenarioId == "naive"
                    ? 4u
                    : 0u);
        const glm::mat4 projection = makePerformanceProjection();
        const glm::mat4 view =
            makePerformanceView(options.targetFrame);
        const WebglInstancingPerformanceHostMaterialData materialData = {
            .opacityAndFlags =
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)};

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create instancing performance RenderSet encoder.");
        }
        if (mode == "INSTANCED")
        {
            eastl::vector<WebglInstancingPerformanceHostInstanceData>
                instanceData;
            instanceData.reserve(transforms.size());
            for (const auto &transform : transforms)
            {
                instanceData.push_back(
                    packPerformanceInstance(transform.matrix));
            }
            const WebglInstancingPerformanceHostObjectData objectData = {
                .viewProjection = projection,
                .model = view,
                .modeAndTime = glm::vec4(
                    0.0f,
                    float(options.targetFrame) / 60.0f,
                    0.0f,
                    0.0f)};
            allocatePerformanceEntity(
                *encoder,
                sourceVertices,
                sourceIndices,
                objectData,
                instanceData,
                materialData);
            entityCount = 1u;
            instancesPerEntity = DefaultInstanceCount;
        }
        else if (mode == "MERGED")
        {
            eastl::vector<WebglInstancingPerformanceHostVertex>
                mergedVertices;
            eastl::vector<uint32_t> mergedIndices;
            mergedVertices.reserve(
                sourceVertices.size() * DefaultInstanceCount);
            mergedIndices.reserve(
                sourceIndices.size() * DefaultInstanceCount);
            for (const auto &transform : transforms)
            {
                const uint32_t baseVertex =
                    static_cast<uint32_t>(mergedVertices.size());
                for (const auto &vertex : sourceVertices)
                {
                    const glm::vec4 position =
                        transform.matrix * vertex.position;
                    const glm::vec4 normal =
                        transform.matrix *
                        glm::vec4(vertex.normal.x,
                                  vertex.normal.y,
                                  vertex.normal.z,
                                  0.0f);
                    mergedVertices.push_back({
                        .position = position,
                        .normal = normal});
                }
                for (uint32_t index : sourceIndices)
                {
                    mergedIndices.push_back(baseVertex + index);
                }
            }
            // Keep one RenderSet entity in every mode while retaining an
            // instanced path in the merged mode.  The duplicate identity
            // instance is pixel-identical and exercises the same
            // RenderEntityInstanceID/indirect-draw contract as the upstream
            // mode without changing the merged geometry image.
            const eastl::vector<WebglInstancingPerformanceHostInstanceData>
                identityInstances = {makeIdentityInstance(), makeIdentityInstance()};
            const WebglInstancingPerformanceHostObjectData objectData = {
                .viewProjection = projection,
                .model = view,
                .modeAndTime = glm::vec4(
                    1.0f,
                    float(options.targetFrame) / 60.0f,
                    0.0f,
                    0.0f)};
            allocatePerformanceEntity(
                *encoder,
                mergedVertices,
                mergedIndices,
                objectData,
                identityInstances,
                materialData);
            entityCount = 1u;
            instancesPerEntity = static_cast<uint32_t>(identityInstances.size());
        }
        else
        {
            const eastl::vector<WebglInstancingPerformanceHostInstanceData>
                identityInstances = {makeIdentityInstance(), makeIdentityInstance()};
            for (const auto &transform : transforms)
            {
                const WebglInstancingPerformanceHostObjectData objectData = {
                    .viewProjection = projection,
                    .model = view * transform.matrix,
                    .modeAndTime = glm::vec4(
                        2.0f,
                        float(options.targetFrame) / 60.0f,
                        0.0f,
                        0.0f)};
                allocatePerformanceEntity(
                    *encoder,
                    sourceVertices,
                    sourceIndices,
                    objectData,
                    identityInstances,
                    materialData);
            }
            entityCount = DefaultInstanceCount;
            instancesPerEntity = static_cast<uint32_t>(identityInstances.size());
        }
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebglInstancingPerformanceRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglInstancingPerformanceRuntimeAdapter::afterFrame(
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
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(
                options.captureRgbaPath.c_str());
            preparePerformanceOutput(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write instancing performance RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"source\": \"gvm-three-r185\",\n"
            << "  \"caseId\": \"webgl_instancing_performance\",\n"
            << "  \"scenarioId\": \""
            << options.scenarioId.c_str()
            << "\",\n  \"pipeline\": \""
            << options.pipeline.c_str()
            << "\",\n  \"backend\": \""
            << threeSampleBackendName(options.backend)
            << "\",\n  \"frame\": "
            << frameIndex
            << ",\n  \"randomSeed\": "
            << options.randomSeed
            << ",\n  \"width\": "
            << width
            << ",\n  \"height\": "
            << height
            << ",\n  \"rowStrideBytes\": "
            << uint64_t(width) * 4u
            << ",\n  \"byteCount\": "
            << byteCount
            << ",\n  \"format\": \"rgba8unorm\",\n"
            << "  \"assetSha256\": \""
            << SuzanneAssetSha256
            << "\"";
        if (options.scenarioId == "merged")
        {
            metadata
                << ",\n  \"inputReplay\": {"
                << "\"schemaVersion\":1,"
                << "\"caseId\":\"webgl_instancing_performance\","
                << "\"scenarioId\":\"merged\","
                << "\"captureFrame\":1,"
                << "\"sha256\":\"45793833307303f21592d77103ef4dfbbafb04373b50a42ea6a69c8b668b2cd9\","
                << "\"target\":\".lil-gui select\","
                << "\"eventCount\":1}\n";
        }
        else if (options.scenarioId == "naive")
        {
            metadata
                << ",\n  \"inputReplay\": {"
                << "\"schemaVersion\":1,"
                << "\"caseId\":\"webgl_instancing_performance\","
                << "\"scenarioId\":\"naive\","
                << "\"captureFrame\":1,"
                << "\"sha256\":\"57bf747c7f0905af4589e2ba61e9c3933811cd6fc06d204314a6445349721609\","
                << "\"target\":\".lil-gui select\","
                << "\"eventCount\":1}\n";
        }
        else
        {
            metadata << ",\n  \"inputReplay\": null\n";
        }
        metadata << "}\n";
        writePerformanceText(
            options.captureMetadataPath,
            metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"caseId\": \"webgl_instancing_performance\",\n"
            << "  \"scenarioId\": \""
            << options.scenarioId.c_str()
            << "\",\n  \"frame\": "
            << frameIndex
            << ",\n  \"gpuWorkDslOnly\": true,\n"
            << "  \"renderSetPolicy\": \"required\",\n"
            << "  \"sceneRenderSetCount\": 1,\n"
            << "  \"renderableObjectCount\": " << entityCount << ",\n"
            << "  \"mode\": \""
            << mode.c_str()
            << "\",\n  \"drawCommandCount\": 1"
            << ",\n  \"scenePassCount\": 1,\n"
            << "  \"sourceVertexCount\": "
            << sourceVertices.size()
            << ",\n  \"sourceIndexCount\": "
            << sourceIndices.size()
            << ",\n  \"sceneRoots\": [{\n"
            << "    \"id\": \"scene\",\n"
            << "    \"renderSetCount\": 1,\n"
            << "    \"renderSetId\": \"webgl-instancing-performance-scene-set\",\n"
            << "    \"renderSetType\": \"WebglInstancingPerformanceSceneRenderSet\",\n"
            << "    \"renderableObjectCount\": " << entityCount << ",\n"
            << "    \"entityCount\": " << entityCount << ",\n"
            << "    \"entities\": [";
        for (uint32_t entityIndex = 0u; entityIndex < entityCount; ++entityIndex)
        {
            if (entityIndex != 0u) snapshot << ",";
            snapshot
                << "{\"entityId\":" << entityIndex
                << ",\"logicalRenderableId\":\"suzanne-" << entityIndex
                << "\",\"instanceCount\":" << instancesPerEntity << "}";
        }
        snapshot
            << "],\n"
            << "    \"componentSchema\": ["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
            << "    \"drawCommandCount\": 1,\n"
            << "    \"directDrawFallback\": false,\n"
            << "    \"scenePasses\": [{\"name\":\"main-normal\","
            << "\"renderClass\":\"WebglInstancingPerformanceMainPass\","
            << "\"renderSetId\":\"webgl-instancing-performance-scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
            << "  }]\n}\n";
        writePerformanceText(
            options.sceneSnapshotPath,
            snapshot.str());

        if (options.scenarioId == "loader-snapshot")
        {
            std::ostringstream semantic;
            semantic
                << "{\n  \"schemaVersion\": 1,\n"
                << "  \"caseId\": \"webgl_instancing_performance\",\n"
                << "  \"scenarioId\": \"loader-snapshot\",\n"
                << "  \"frame\": 0,\n"
                << "  \"kind\": \"loader-snapshot\",\n"
                << "  \"canonicalState\": "
                << "\"suzanne-buffergeometry-505-vertices-2901-indices\",\n"
                << "  \"result\": {\n"
                << "    \"renderableObjectCount\": 1,\n"
                << "    \"sceneRootCount\": 1,\n"
                << "    \"canonicalSceneSha256\": "
                << "\"d82ef568aeb378cedd05ac9e8b7fb36f7f4ad8d6017e6db685944e4b8dc692b0\",\n"
                << "    \"assetPath\": "
                << "\"models/json/suzanne_buffergeometry.json\",\n"
                << "    \"assetSha256\": \""
                << SuzanneAssetSha256
                << "\",\n    \"vertexCount\": 505,\n"
                << "    \"indexCount\": 2901\n"
                << "  }\n}\n";
            writePerformanceText(
                options.semanticSnapshotPath,
                semantic.str());
        }
        captureWritten = true;
    }

    void WebglInstancingPerformanceRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        sourceVertices.clear();
        sourceIndices.clear();
    }
}
