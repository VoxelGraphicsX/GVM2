#include "WebgpuComputeGeometryRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t LeePerryVertexCount = 9279u;
        constexpr uint32_t LeePerryIndexCount = 53052u;

        /** Validates the three deterministic webgpu_compute_geometry scenarios. */
        void validateComputeGeometryScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial-assets" &&
                options.targetFrame == 0u;
            const bool loader =
                options.scenarioId == "loader-snapshot" &&
                options.targetFrame == 0u;
            const bool jelly =
                options.scenarioId == "jelly-input" &&
                options.targetFrame == 60u;
            if (options.caseId != "webgpu_compute_geometry" ||
                (!initial && !loader && !jelly) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                (jelly != !options.inputReplayPath.empty()))
            {
                throw std::invalid_argument(
                    "WebGPU compute geometry requires one locked Manifest scenario.");
            }
        }

        /** Reads one bounded binary asset from the explicit asset root. */
        eastl::vector<uint8_t> readComputeGeometryAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open the locked Lee Perry Smith GLB.");
            }
            input.seekg(0, std::ios::end);
            const std::streamoff byteCount = input.tellg();
            input.seekg(0, std::ios::beg);
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "Lee Perry Smith GLB is empty.");
            }
            eastl::vector<uint8_t> bytes(
                static_cast<size_t>(byteCount));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the Lee Perry Smith GLB.");
            }
            return bytes;
        }

        /** Builds the WebGPU perspective projection used by the r185 camera. */
        glm::mat4 makeComputeGeometryProjection()
        {
            constexpr float NearDistance = 0.1f;
            constexpr float FarDistance = 10.0f;
            constexpr float Pi = 3.14159265358979323846f;
            const float top =
                NearDistance * std::tan(50.0f * Pi / 360.0f);
            const float right = top * (800.0f / 500.0f);
            glm::mat4 result(0.0f);
            result[0u][0u] = NearDistance / right;
            result[1u][1u] = -NearDistance / top;
            result[2u][2u] =
                -FarDistance / (FarDistance - NearDistance);
            result[2u][3u] = -1.0f;
            result[3u][2u] =
                -(FarDistance * NearDistance) /
                (FarDistance - NearDistance);
            return result;
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareComputeGeometryOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeComputeGeometryText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareComputeGeometryOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU compute geometry artifact.");
            }
        }
    }

    void WebgpuComputeGeometryRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateComputeGeometryScenario(options);
        device = inDevice;
        const std::filesystem::path assetPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" /
            "gltf" /
            "LeePerrySmith" /
            "LeePerrySmith.glb";
        const ThreeCompat::DecodedGlbMesh mesh =
            ThreeCompat::decodeFirstGlbMesh(
                readComputeGeometryAsset(assetPath));
        if (mesh.positions.size() !=
                size_t(LeePerryVertexCount) * 3u ||
            mesh.indices.size() != LeePerryIndexCount)
        {
            throw std::runtime_error(
                "Lee Perry Smith GLB counts diverged from the r185 lock.");
        }
        positions.resize(LeePerryVertexCount);
        normals.resize(LeePerryVertexCount);
        for (uint32_t index = 0u;
             index < LeePerryVertexCount;
             ++index)
        {
            positions[index] = glm::vec4(
                mesh.positions[index * 3u],
                mesh.positions[index * 3u + 1u],
                mesh.positions[index * 3u + 2u],
                1.0f);
            normals[index] = glm::vec4(
                mesh.normals[index * 3u],
                mesh.normals[index * 3u + 1u],
                mesh.normals[index * 3u + 2u],
                0.0f);
        }
        indices.assign(mesh.indices.begin(), mesh.indices.end());

        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(0.0f, 0.0f, 1.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        modelView =
            view * glm::scale(
                glm::mat4(1.0f),
                glm::vec3(0.1f));
        const glm::mat4 projection =
            makeComputeGeometryProjection();
        modelViewProjection = projection * modelView;
        physics = glm::vec4(
            0.4f,
            0.94f,
            0.25f,
            float(LeePerryVertexCount));
        pointerPosition = glm::vec4(0.0f);
    }

    void WebgpuComputeGeometryRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuComputeGeometryRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
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
            prepareComputeGeometryOutput(path);
            std::ofstream output(
                path,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGPU compute geometry RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"source\": \"gvm-three-r185\",\n"
            << "  \"caseId\": \"webgpu_compute_geometry\",\n"
            << "  \"scenarioId\": \"" << options.scenarioId.c_str()
            << "\",\n  \"pipeline\": \"" << options.pipeline.c_str()
            << "\",\n  \"backend\": \""
            << threeSampleBackendName(options.backend)
            << "\",\n  \"frame\": " << frameIndex
            << ",\n  \"randomSeed\": " << options.randomSeed
            << ",\n  \"width\": " << width
            << ",\n  \"height\": " << height
            << ",\n  \"rowStrideBytes\": " << uint64_t(width) * 4u
            << ",\n  \"byteCount\": " << byteCount
            << ",\n  \"format\": \"rgba8unorm\",\n  \"inputReplay\": ";
        if (options.scenarioId == "jelly-input")
        {
            metadata
                << "{\"sha256\":\"cca4ff0c76ea166fa4aa4f8ea2e39ec1812bad2299b4cef80efd008abafc116e\","
                << "\"caseId\":\"webgpu_compute_geometry\",\"scenarioId\":\"jelly-input\","
                << "\"captureFrame\":60,\"eventCount\":1,\"target\":\"canvas:not([class])\"}";
        }
        else
        {
            metadata << "null";
        }
        metadata
            << ",\n"
            << "  \"renderSetCount\": 0,\n"
            << "  \"entityCount\": 1,\n"
            << "  \"vertexCount\": 9279,\n"
            << "  \"indexCount\": 53052,\n"
            << "  \"computeDispatchThreads\": 9279\n}\n";
        writeComputeGeometryText(
            options.captureMetadataPath,
            metadata.str());
        writeComputeGeometryText(
            options.sceneSnapshotPath,
            std::string("{\n  \"caseId\": \"webgpu_compute_geometry\",\n"
            "  \"scenarioId\": \"") + options.scenarioId.c_str() + "\",\n"
            "  \"frame\": " + std::to_string(frameIndex) + ",\n"
            "  \"gpuWorkDslOnly\": true,\n"
            "  \"renderSetPolicy\": \"not-required\",\n"
            "  \"sceneRenderSetCount\": 0,\n"
            "  \"renderableObjectCount\": 1,\n"
            "  \"entityCount\": 1,\n"
            "  \"instanceCount\": 1,\n"
            "  \"vertexCount\": 9279,\n"
            "  \"indexCount\": 53052,\n"
            "  \"drawCommandCount\": 1,\n"
            "  \"scenePassCount\": 1\n}\n");
        writeComputeGeometryText(
            options.semanticSnapshotPath,
            "{\n  \"schemaVersion\": 1,\n"
            "  \"caseId\": \"webgpu_compute_geometry\",\n"
            "  \"scenarioId\": \"loader-snapshot\",\n"
            "  \"frame\": 0,\n"
            "  \"kind\": \"loader-snapshot\",\n"
            "  \"canonicalState\": \"aux-scene-one-mesh-9279-vertices-53052-indices\",\n"
            "  \"result\": {\n"
            "    \"renderableObjectCount\": 1,\n"
            "    \"sceneRootCount\": 1,\n"
            "    \"canonicalSceneSha256\": \"f37983c94876234bec7ad285ff90ead5a4b81c0c7c7a996a63289e439d808811\",\n"
            "    \"assetPath\": \"models/gltf/LeePerrySmith/LeePerrySmith.glb\",\n"
            "    \"assetSha256\": \"402b8a8ac9f03232e6d64b5962929703a069daf99d3c49ac8eb0e48bedc9c576\",\n"
            "    \"vertexCount\": 9279,\n"
            "    \"indexCount\": 53052\n"
            "  }\n}\n");
        captureWritten = true;
    }

    void WebgpuComputeGeometryRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        positions.clear();
        normals.clear();
        indices.clear();
    }
}
