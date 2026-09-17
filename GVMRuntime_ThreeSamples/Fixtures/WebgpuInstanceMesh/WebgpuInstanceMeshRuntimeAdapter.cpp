#include "WebgpuInstanceMeshRuntimeAdapter.hpp"

#include "InstanceSampleGeometry.hpp"
#include "SampleAssetDecoders.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

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
        constexpr uint32_t GridAmount = 10u;
        constexpr uint32_t FullInstanceCount = 1000u;
        constexpr const char *SuzanneAssetSha256 =
            "8aa6f692a08b2ec0991b7903914cfa3154e2f553a4ca72ca3fb5d7e6c16f839f";
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebgpuInstanceMeshHostVertex) == 32u);
        static_assert(sizeof(WebgpuInstanceMeshHostObjectData) == 144u);
        static_assert(sizeof(WebgpuInstanceMeshHostInstanceData) == 80u);
        static_assert(sizeof(WebgpuInstanceMeshHostMaterialData) == 16u);

        /** Reads one bounded Suzanne BufferGeometry asset. */
        eastl::vector<uint8_t> readInstanceMeshAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open pinned suzanne_buffergeometry.json.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) >
                    uint64_t(std::numeric_limits<size_t>::max()))
            {
                throw std::runtime_error(
                    "Pinned Suzanne asset has an invalid byte count.");
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
                    "Could not read complete Suzanne asset.");
            }
            return bytes;
        }

        /** Validates the four frozen webgpu_instance_mesh scenarios. */
        void validateInstanceMeshScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial-assets" &&
                options.targetFrame == 0u;
            const bool loader =
                options.scenarioId == "loader-snapshot" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 60u;
            const bool reduced =
                options.scenarioId == "reduced-count" &&
                options.targetFrame == 61u;
            if (options.caseId != "webgpu_instance_mesh" ||
                (!initial && !loader && !animated && !reduced) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty() ||
                (reduced != !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "WebGPU instance mesh requires one locked Manifest scenario.");
            }
        }

        /** Builds the WebGPU depth-range perspective with generated-backend Y compensation. */
        glm::mat4 makeInstanceMeshProjection()
        {
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 100.0;
            const double top =
                NearDistance * std::tan(60.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = (800.0 / 500.0) * height;
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] =
                float(2.0 * NearDistance / width);
            result[1u][1u] =
                float(-2.0 * NearDistance / height);
            result[2u][2u] =
                float(-FarDistance / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] =
                float(-FarDistance * NearDistance / depth);
            return result;
        }

        /** Returns the deterministic virtual time at one 60 Hz target frame. */
        double instanceMeshVirtualTimeSeconds(uint32_t targetFrame)
        {
            return 1700000000.0 + double(targetFrame) / 60.0;
        }

        /** Advances the exact xorshift32 stream installed by the reference bootstrap. */
        float nextInstanceMeshReferenceRandom(uint32_t &state)
        {
            uint32_t value = state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<float>(value >> 8u) / 16777216.0f;
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareInstanceMeshOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(
                    path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeInstanceMeshText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareInstanceMeshOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU instance mesh artifact.");
            }
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendInstanceMeshPayload(
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

    void WebgpuInstanceMeshRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateInstanceMeshScenario(options);
        device = inDevice;
        const auto bytes = readInstanceMeshAsset(
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
                "Suzanne geometry count differs from the r185 lock.");
        }
        vertices.reserve(mesh.positions.size());
        for (size_t index = 0u;
             index < mesh.positions.size();
             ++index)
        {
            vertices.push_back({
                .position = glm::vec4(
                    mesh.positions[index] * 0.5f,
                    1.0f),
                .normal = glm::vec4(mesh.normals[index], 0.0f)});
        }
        indices.assign(mesh.indices.begin(), mesh.indices.end());
        activeInstanceCount =
            options.scenarioId == "reduced-count"
            ? 125u
            : FullInstanceCount;
        const double timeSeconds =
            instanceMeshVirtualTimeSeconds(options.targetFrame);
        const auto transforms =
            ThreeCompat::buildWebgpuInstanceMeshTransforms(
                GridAmount,
                activeInstanceCount,
                timeSeconds);
        uint32_t randomState = DefaultThreeRandomSeed;
        for (uint32_t draw = 0u; draw < 261u; ++draw)
        {
            nextInstanceMeshReferenceRandom(randomState);
        }
        instances.reserve(transforms.size());
        for (const ThreeCompat::InstanceSampleTransform &transform :
             transforms)
        {
            const glm::vec4 rangeColor(
                nextInstanceMeshReferenceRandom(randomState),
                nextInstanceMeshReferenceRandom(randomState),
                nextInstanceMeshReferenceRandom(randomState),
                nextInstanceMeshReferenceRandom(randomState));
            instances.push_back({
                .transformColumn0 = transform.matrix[0u],
                .transformColumn1 = transform.matrix[1u],
                .transformColumn2 = transform.matrix[2u],
                .transformColumn3 = transform.matrix[3u],
                .ordinal = rangeColor});
        }

        const float rotationX =
            static_cast<float>(std::sin(timeSeconds / 4.0));
        const float rotationY =
            static_cast<float>(std::sin(timeSeconds / 2.0));
        const float cosineX = std::cos(rotationX * 0.5f);
        const float cosineY = std::cos(rotationY * 0.5f);
        const float sineX = std::sin(rotationX * 0.5f);
        const float sineY = std::sin(rotationY * 0.5f);
        const glm::quat modelOrientation(
            cosineX * cosineY,
            sineX * cosineY,
            cosineX * sineY,
            sineX * sineY);
        const glm::mat4 model = glm::mat4_cast(modelOrientation);
        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(9.0f, 9.0f, 9.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const WebgpuInstanceMeshHostObjectData objectData = {
            .viewProjection = makeInstanceMeshProjection() * view,
            .model = model,
            .timeAndCount = glm::vec4(
                float(double(options.targetFrame) / 60.0),
                float(activeInstanceCount),
                0.0f,
                0.0f)};
        const WebgpuInstanceMeshHostMaterialData materialData = {
            .colorAndOpacity =
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)};

        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create WebGPU instance mesh RenderSet encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount =
            static_cast<uint32_t>(vertices.size());
        allocation.indicesCount =
            static_cast<uint32_t>(indices.size());
        allocation.instanceCount = activeInstanceCount;
        appendInstanceMeshPayload(
            allocation,
            WebgpuInstanceMeshSceneRenderSetComponents::vertices,
            "WebgpuInstanceMeshVertices",
            vertices.data(),
            vertices.size() * sizeof(vertices[0u]),
            1u);
        appendInstanceMeshPayload(
            allocation,
            WebgpuInstanceMeshSceneRenderSetComponents::indices,
            "WebgpuInstanceMeshIndices",
            indices.data(),
            indices.size() * sizeof(indices[0u]),
            1u);
        appendInstanceMeshPayload(
            allocation,
            WebgpuInstanceMeshSceneRenderSetComponents::objects,
            "WebgpuInstanceMeshObject",
            &objectData,
            sizeof(objectData),
            1u);
        appendInstanceMeshPayload(
            allocation,
            WebgpuInstanceMeshSceneRenderSetComponents::instances,
            "WebgpuInstanceMeshInstances",
            instances.data(),
            instances.size() * sizeof(instances[0u]),
            activeInstanceCount);
        appendInstanceMeshPayload(
            allocation,
            WebgpuInstanceMeshSceneRenderSetComponents::materials,
            "WebgpuInstanceMeshMaterial",
            &materialData,
            sizeof(materialData),
            1u);
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebgpuInstanceMeshRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuInstanceMeshRuntimeAdapter::afterFrame(
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
            prepareInstanceMeshOutput(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU instance mesh RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"source\": \"gvm-three-r185\",\n"
            << "  \"caseId\": \"webgpu_instance_mesh\",\n"
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
            << ",\n  \"format\": \"rgba8unorm\",\n  \"inputReplay\": ";
        if (options.scenarioId == "reduced-count")
        {
            metadata
                << "{\"sha256\":\"2cf7798c5b002acad6e1dcf96913b6b1830fb35400439e8368627b07133e6b05\","
                << "\"caseId\":\"webgpu_instance_mesh\",\"scenarioId\":\"reduced-count\","
                << "\"captureFrame\":61,\"eventCount\":1,\"target\":\"canvas:not([class])\"}";
        }
        else
        {
            metadata << "null";
        }
        metadata
            << ",\n"
            << "  \"assetSha256\": \""
            << SuzanneAssetSha256
            << "\"\n}\n";
        writeInstanceMeshText(
            options.captureMetadataPath,
            metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"caseId\": \"webgpu_instance_mesh\",\n"
            << "  \"scenarioId\": \""
            << options.scenarioId.c_str()
            << "\",\n  \"frame\": "
            << frameIndex
            << ",\n  \"gpuWorkDslOnly\": true,\n"
            << "  \"renderSetPolicy\": \"required\",\n"
            << "  \"sceneRenderSetCount\": 1,\n"
            << "  \"renderSetType\": "
            << "\"WebgpuInstanceMeshSceneRenderSet\",\n"
            << "  \"renderableObjectCount\": 1,\n"
            << "  \"entityCount\": 1,\n"
            << "  \"instanceCounts\": ["
            << activeInstanceCount
            << "],\n  \"drawCommandCount\": 1,\n"
            << "  \"scenePassCount\": 1,\n"
            << "  \"vertexCount\": "
            << vertices.size()
            << ",\n  \"indexCount\": "
            << indices.size()
            << ",\n  \"sceneRoots\": [{\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"webgpu-instance-mesh-scene-set\","
            << "\"renderSetType\":\"WebgpuInstanceMeshSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"suzanne-grid\","
            << "\"instanceCount\":" << activeInstanceCount << "}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"main\","
            << "\"renderClass\":\"WebgpuInstanceMeshMainPass\","
            << "\"renderSetId\":\"webgpu-instance-mesh-scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n";
        writeInstanceMeshText(
            options.sceneSnapshotPath,
            snapshot.str());

        if (options.scenarioId == "loader-snapshot")
        {
            std::ostringstream semantic;
            semantic
                << "{\n  \"schemaVersion\": 1,\n"
                << "  \"caseId\": \"webgpu_instance_mesh\",\n"
                << "  \"scenarioId\": \"loader-snapshot\",\n"
                << "  \"frame\": 0,\n"
                << "  \"kind\": \"loader-snapshot\",\n"
                << "  \"canonicalState\": \"one-suzanne-buffergeometry-505-vertices-2901-indices\",\n"
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
            writeInstanceMeshText(
                options.semanticSnapshotPath,
                semantic.str());
        }
        captureWritten = true;
    }

    void WebgpuInstanceMeshRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        instances.clear();
    }
}
