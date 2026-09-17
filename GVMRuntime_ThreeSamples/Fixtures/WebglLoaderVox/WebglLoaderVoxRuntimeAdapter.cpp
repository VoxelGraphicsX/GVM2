#include "WebglLoaderVoxRuntimeAdapter.hpp"

#include "SampleAssetDecoders.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <EASTL/string.h>

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
        constexpr const char *VoxAssetSha256 =
            "acbe999679307a698b68fdec935a526a3b243006d20933e4ba22b72a0374ef90";
        constexpr double Pi = 3.14159265358979323846;

        /** Reads one bounded immutable VOX asset. */
        eastl::vector<uint8_t> readVoxAsset(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open pinned monu10.vox.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 || uint64_t(byteCount) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error("Pinned monu10.vox has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read complete monu10.vox.");
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded payload. */
        eastl::string calculateVoxSha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 15u]);
            }
            return result;
        }

        /** Builds Three's perspective matrix with generated-backend Y compensation. */
        glm::mat4 makeVoxProjection()
        {
            constexpr double FieldOfViewDegrees = 50.0;
            constexpr double Aspect = 800.0 / 500.0;
            constexpr double NearDistance = 0.01;
            constexpr double FarDistance = 10.0;
            const double top = NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = Aspect * height;
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] = float(2.0 * NearDistance / width);
            result[1u][1u] = float(-2.0 * NearDistance / height);
            result[2u][2u] = float(-FarDistance / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] = float(-FarDistance * NearDistance / depth);
            return result;
        }

        /** Stores one selected Orbit camera and fixed lighting state in generated DSL layout. */
        WebglLoaderVoxUniforms makeVoxUniforms(const glm::vec3 &cameraPosition)
        {
            const glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(0.0015f));
            const glm::mat4 view = glm::lookAtRH(
                cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 mvp = makeVoxProjection() * view * model;
            WebglLoaderVoxUniforms result;
            result.modelViewProjectionColumn0 = float4(mvp[0u][0u], mvp[0u][1u], mvp[0u][2u], mvp[0u][3u]);
            result.modelViewProjectionColumn1 = float4(mvp[1u][0u], mvp[1u][1u], mvp[1u][2u], mvp[1u][3u]);
            result.modelViewProjectionColumn2 = float4(mvp[2u][0u], mvp[2u][1u], mvp[2u][2u], mvp[2u][3u]);
            result.modelViewProjectionColumn3 = float4(mvp[3u][0u], mvp[3u][1u], mvp[3u][2u], mvp[3u][3u]);
            result.cameraPositionAndScale = float4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0015f);
            const glm::vec3 light0 = glm::normalize(glm::vec3(1.5f, 3.0f, 2.5f));
            const glm::vec3 light1 = glm::normalize(glm::vec3(-1.5f, -3.0f, -2.5f));
            result.lightDirection0 = float4(light0.x, light0.y, light0.z, 0.0f);
            result.lightDirection1 = float4(light1.x, light1.y, light1.z, 0.0f);
            return result;
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareVoxOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one bounded text artifact with deterministic truncation. */
        void writeVoxText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareVoxOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write VOX text artifact.");
        }
    }

    void WebglLoaderVoxRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
        const bool canonical = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
        const bool orbit = options.scenarioId == "orbit" && options.targetFrame == 1u;
        if (options.caseId != "webgl_loader_vox" || (!initial && !canonical && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
        {
            throw std::invalid_argument("VOX adapter requires one locked manifest scenario.");
        }
        if (orbit != !options.inputReplayPath.empty())
        {
            throw std::invalid_argument("Only the VOX orbit scenario accepts a replay.");
        }
        device = inDevice;
        const eastl::vector<uint8_t> bytes = readVoxAsset(
            std::filesystem::path(options.assetRoot.c_str()) / "models" / "vox" / "monu10.vox");
        if (calculateVoxSha256(bytes) != VoxAssetSha256)
        {
            throw std::runtime_error("monu10.vox differs from the r185 asset lock.");
        }
        const ThreeCompat::DecodedVoxModel model = ThreeCompat::decodeMagicaVoxel150(bytes);
        const ThreeCompat::DecodedVoxMesh mesh = ThreeCompat::buildVoxGreedyMesh(model);
        voxelCount = static_cast<uint32_t>(model.voxels.size());
        quadCount = mesh.quadCount;
        vertices.reserve(mesh.vertices.size());
        for (const ThreeCompat::VoxMeshVertex &source : mesh.vertices)
        {
            vertices.push_back({
                .position = float3(source.positionX, source.positionY, source.positionZ),
                .normal = float3(source.normalX, source.normalY, source.normalZ),
                .color = float3(source.colorR, source.colorG, source.colorB)});
        }
        indices.assign(mesh.indices.begin(), mesh.indices.end());
        const glm::vec3 cameraPosition = orbit ?
            glm::vec3(0.00809509545f, -0.0213729422f, 0.257590114f) :
            glm::vec3(0.175f, 0.075f, 0.175f);
        uniforms = makeVoxUniforms(cameraPosition);
    }

    void WebglLoaderVoxRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderVoxRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
            prepareVoxOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write VOX RGBA capture.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\": 1,\n  \"source\": \"gvm-three-r185\",\n"
                 << "  \"caseId\": \"webgl_loader_vox\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"randomSeed\": " << options.randomSeed << ",\n"
                 << "  \"frame\": " << frameIndex << ",\n"
                 << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                 << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\": " << byteCount << ",\n  \"format\": \"rgba8unorm\",\n"
                 << "  \"assetSha256\": \"" << VoxAssetSha256 << "\"";
        if (options.scenarioId == "orbit")
        {
            metadata << ",\n  \"inputReplay\": {\n"
                     << "    \"sha256\": "
                     << "\"e71d3164fd1d848b1f08cda40c6db7e53ea9425acfd3d962a9c852f7ab861c6d\",\n"
                     << "    \"caseId\": \"webgl_loader_vox\",\n"
                     << "    \"scenarioId\": \"orbit\",\n"
                     << "    \"captureFrame\": 1,\n"
                     << "    \"eventCount\": 3,\n"
                     << "    \"target\": \"body > canvas\"\n"
                     << "  }";
        }
        metadata << "\n}\n";
        writeVoxText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\": 1,\n  \"caseId\": \"webgl_loader_vox\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\": " << frameIndex << ",\n  \"gpuWorkDslOnly\": true,\n"
                 << "  \"renderSetPolicy\": \"not-required\",\n  \"sceneRenderSetCount\": 0,\n"
                 << "  \"renderableObjectCount\": 1,\n  \"scenePassCount\": 1,\n"
                 << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\","
                 << "\"scenePass\":\"standard-vertex-color\",\"entityOrdinal\":0}],\n"
                 << "  \"drawCommandCount\": 1,\n  \"instanceCount\": 1,\n"
                 << "  \"voxelCount\": " << voxelCount << ",\n"
                 << "  \"quadCount\": " << quadCount << ",\n"
                 << "  \"vertexCount\": " << vertices.size() << ",\n"
                 << "  \"indexCount\": " << indices.size() << "\n}\n";
        writeVoxText(options.sceneSnapshotPath, snapshot.str());
        if (options.scenarioId == "canonical-loader")
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\": 1,\n"
                     << "  \"caseId\": \"webgl_loader_vox\",\n"
                     << "  \"scenarioId\": \"canonical-loader\",\n"
                     << "  \"frame\": 0,\n"
                     << "  \"kind\": \"loader-snapshot\",\n"
                     << "  \"canonicalState\": "
                     << "\"one-greedy-mesh-150764-voxels-palette-colors\",\n"
                     << "  \"result\": {\n"
                     << "    \"renderableObjectCount\": 1,\n"
                     << "    \"sceneRootCount\": 1,\n"
                     << "    \"canonicalSceneSha256\": "
                     << "\"9a5061289e701e89ced41a7222f1fff6fd38427e200edbbe83c2ad919d99d17b\",\n"
                     << "    \"assetPath\": \"models/vox/monu10.vox\",\n"
                     << "    \"assetSha256\": \"" << VoxAssetSha256 << "\",\n"
                     << "    \"voxelCount\": " << voxelCount << ",\n"
                     << "    \"quadCount\": " << quadCount << ",\n"
                     << "    \"vertexCount\": " << vertices.size() << ",\n"
                     << "    \"indexCount\": " << indices.size() << "\n"
                     << "  }\n}\n";
            writeVoxText(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglLoaderVoxRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
}
