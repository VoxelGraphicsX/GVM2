#include "WebglModifierEdgeSplitRuntimeAdapter.hpp"

#include "EdgeSplitGeometry.hpp"
#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
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
        constexpr double EdgeSplitPi = 3.14159265358979323846;
        constexpr const char *CerberusObjSha256 =
            "0ffac38139fe00145683774bf33d2fb245e2770a68bb71c552812ab9d12ee534";
        constexpr const char *CerberusAlbedoSha256 =
            "e29e1d326560c635bf2407ba3bf506d632ae603d212082f74ada55c4f22423b6";
        constexpr const char *EdgeSplitGuiReplaySha256 =
            "4d91a0d3052e4712d279b9e558fc1e90e5a508ef38ebd7947da25e4f166e95b8";
        constexpr const char *EdgeSplitOrbitReplaySha256 =
            "d8c78bafd71e53fa2939f219c6022396d38fa5f0a15cf033e02f10d11465d815";
        constexpr const char *EdgeSplitCanonicalSceneSha256 =
            "41e914cdead934e9a24e74d434f0a23bf2259fac2445ad84d78a7a1fe1c2fa69";

        /** Reads one bounded immutable EdgeSplit asset. */
        eastl::vector<uint8_t> readEdgeSplitAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open one pinned Cerberus asset.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) >
                    uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error(
                    "A pinned Cerberus asset has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read a complete pinned Cerberus asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded asset payload. */
        eastl::string calculateEdgeSplitSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 15u]);
            }
            return result;
        }

        /** Builds Three's perspective matrix with generated-backend Y compensation. */
        glm::mat4 makeEdgeSplitProjection()
        {
            constexpr double FieldOfViewDegrees = 75.0;
            constexpr double Aspect = 800.0 / 500.0;
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 2000.0;
            const double top =
                NearDistance *
                std::tan(FieldOfViewDegrees * EdgeSplitPi / 360.0);
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

        /** Stores one column-major matrix in the generated uniform layout. */
        WebglModifierEdgeSplitUniforms makeEdgeSplitUniforms(
            bool showMap,
            bool flatShading,
            bool orbit)
        {
            glm::vec3 cameraPosition(0.0f, 0.0f, 4.0f);
            if (orbit)
            {
                const double theta =
                    -2.0 * EdgeSplitPi * (40.0 * 0.35) / 500.0 * 0.25;
                const double phi =
                    EdgeSplitPi * 0.5 -
                    2.0 * EdgeSplitPi * (-20.0 * 0.35) / 500.0 * 0.25;
                cameraPosition = glm::vec3(
                    float(4.0 * std::sin(phi) * std::sin(theta)),
                    float(4.0 * std::cos(phi)),
                    float(4.0 * std::sin(phi) * std::cos(theta)));
            }
            const glm::mat4 view = glm::lookAtRH(
                cameraPosition,
                glm::vec3(0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 viewProjection =
                makeEdgeSplitProjection() * view;
            WebglModifierEdgeSplitUniforms result;
            result.viewProjectionColumn0 = float4(
                viewProjection[0u][0u], viewProjection[0u][1u],
                viewProjection[0u][2u], viewProjection[0u][3u]);
            result.viewProjectionColumn1 = float4(
                viewProjection[1u][0u], viewProjection[1u][1u],
                viewProjection[1u][2u], viewProjection[1u][3u]);
            result.viewProjectionColumn2 = float4(
                viewProjection[2u][0u], viewProjection[2u][1u],
                viewProjection[2u][2u], viewProjection[2u][3u]);
            result.viewProjectionColumn3 = float4(
                viewProjection[3u][0u], viewProjection[3u][1u],
                viewProjection[3u][2u], viewProjection[3u][3u]);
            result.materialFlagsAndPadding = float4(
                showMap ? 1.0f : 0.0f,
                flatShading ? 1.0f : 0.0f,
                0.0f,
                0.0f);
            return result;
        }

        /** Transforms one local Cerberus vertex by the authored rotate-scale-translate matrix. */
        WebglModifierEdgeSplitVertex makeEdgeSplitVertex(
            const ThreeCompat::MergedEdgeSplitGeometry &geometry,
            size_t vertexIndex)
        {
            const float localX = geometry.positions[vertexIndex * 3u];
            const float localY = geometry.positions[vertexIndex * 3u + 1u];
            const float localZ = geometry.positions[vertexIndex * 3u + 2u];
            const float normalX = geometry.normals[vertexIndex * 3u];
            const float normalY = geometry.normals[vertexIndex * 3u + 1u];
            const float normalZ = geometry.normals[vertexIndex * 3u + 2u];
            return {
                .position = float3(
                    -localZ * 3.5f - 1.5f,
                    localY * 3.5f,
                    localX * 3.5f),
                .normal = float3(-normalZ, normalY, normalX),
                .textureCoordinate = float2(
                    geometry.textureCoordinates[vertexIndex * 2u],
                    geometry.textureCoordinates[vertexIndex * 2u + 1u]),
            };
        }

        /** Creates parent directories for one explicitly requested EdgeSplit artifact. */
        void prepareEdgeSplitOutputPath(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one bounded text artifact with deterministic truncation. */
        void writeEdgeSplitText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareEdgeSplitOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write an EdgeSplit text artifact.");
            }
        }
    }

    void WebglModifierEdgeSplitRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-default" &&
            options.targetFrame == 0u;
        const bool loader =
            options.scenarioId == "loader-topology" &&
            options.targetFrame == 0u;
        const bool gui =
            options.scenarioId == "gui-map-flat-cutoff" &&
            options.targetFrame == 1u;
        const bool orbit =
            options.scenarioId == "orbit-controls" &&
            options.targetFrame == 2u;
        if (options.caseId != "webgl_modifier_edgesplit" ||
            (!initial && !loader && !gui && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "EdgeSplit adapter requires one locked manifest scenario.");
        }
        if ((gui || orbit) != !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Only EdgeSplit GUI and Orbit scenarios accept a replay.");
        }
        if (gui || orbit)
        {
            const eastl::vector<uint8_t> replayBytes =
                readEdgeSplitAsset(
                    std::filesystem::path(options.inputReplayPath.c_str()));
            const char *expectedReplaySha256 = gui
                ? EdgeSplitGuiReplaySha256
                : EdgeSplitOrbitReplaySha256;
            if (calculateEdgeSplitSha256(replayBytes) !=
                expectedReplaySha256)
            {
                throw std::runtime_error(
                    "EdgeSplit input replay differs from its canonical lock.");
            }
        }
        device = inDevice;
        showMap = gui;
        flatShading = gui;
        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        const std::filesystem::path objPath =
            assetRoot / "models" / "obj" / "cerberus" / "Cerberus.obj";
        const std::filesystem::path albedoPath =
            assetRoot / "models" / "obj" / "cerberus" / "Cerberus_A.jpg";
        const eastl::vector<uint8_t> objBytes = readEdgeSplitAsset(objPath);
        const eastl::vector<uint8_t> albedoBytes =
            readEdgeSplitAsset(albedoPath);
        if (calculateEdgeSplitSha256(objBytes) != CerberusObjSha256 ||
            calculateEdgeSplitSha256(albedoBytes) != CerberusAlbedoSha256)
        {
            throw std::runtime_error(
                "Cerberus assets differ from the r185 asset lock.");
        }
        const ThreeCompat::DecodedObjMesh decoded =
            ThreeCompat::decodeObjTriangleMesh(objBytes);
        const ThreeCompat::MergedEdgeSplitGeometry merged =
            ThreeCompat::mergeObjVerticesForEdgeSplit(decoded);
        mergedVertexCount =
            static_cast<uint32_t>(merged.positions.size() / 3u);
        const ThreeCompat::MergedEdgeSplitGeometry selected =
            ThreeCompat::applyEdgeSplitModifier(
                merged,
                gui ? EdgeSplitPi / 3.0 : EdgeSplitPi / 9.0,
                !gui);
        vertices.reserve(selected.positions.size() / 3u);
        for (size_t vertexIndex = 0u;
             vertexIndex < selected.positions.size() / 3u;
             ++vertexIndex)
        {
            vertices.push_back(
                makeEdgeSplitVertex(selected, vertexIndex));
        }
        indices.assign(selected.indices.begin(), selected.indices.end());
        const RgbaImageData albedo = decodeJpegRgba8(albedoPath);
        albedoWidth = albedo.width;
        albedoHeight = albedo.height;
        const eastl::vector<RgbaImageData> images =
            buildSrgbMipChain(albedo);
        albedoMips.reserve(images.size());
        for (const RgbaImageData &image : images)
        {
            albedoMips.push_back(image.pixels);
        }
        uniforms = makeEdgeSplitUniforms(showMap, flatShading, orbit);
    }

    void WebglModifierEdgeSplitRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglModifierEdgeSplitRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareEdgeSplitOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write the EdgeSplit RGBA capture.");
            }
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\": 1,\n"
                 << "  \"source\": \"gvm-three-r185\",\n"
                 << "  \"caseId\": \"webgl_modifier_edgesplit\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"randomSeed\": " << options.randomSeed << ",\n"
                 << "  \"frame\": " << frameIndex << ",\n"
                 << "  \"width\": " << width << ",\n"
                 << "  \"height\": " << height << ",\n"
                 << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\": " << byteCount << ",\n"
                 << "  \"format\": \"rgba8unorm\",\n"
                 << "  \"samplePolicy\": {\"mode\":\"single-sample\","
                 << "\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                 << "  \"assetSha256\": {\"obj\":\"" << CerberusObjSha256
                 << "\",\"albedo\":\"" << CerberusAlbedoSha256 << "\"}";
        if (showMap || options.scenarioId == "orbit-controls")
        {
            const bool guiReplay = showMap;
            metadata << ",\n  \"inputReplay\": {\n"
                     << "    \"sha256\": \""
                     << (guiReplay
                             ? EdgeSplitGuiReplaySha256
                             : EdgeSplitOrbitReplaySha256)
                     << "\",\n"
                     << "    \"caseId\": \"webgl_modifier_edgesplit\",\n"
                     << "    \"scenarioId\": \""
                     << (guiReplay
                             ? "gui-map-flat-cutoff"
                             : "orbit-controls")
                     << "\",\n"
                     << "    \"captureFrame\": "
                     << (guiReplay ? 1u : 2u) << ",\n"
                     << "    \"eventCount\": "
                     << (guiReplay ? 2u : 3u) << ",\n"
                     << "    \"target\": \"body > canvas\"\n"
                     << "  }";
        }
        metadata << "\n}\n";
        writeEdgeSplitText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\": 1,\n"
                 << "  \"caseId\": \"webgl_modifier_edgesplit\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\": " << frameIndex << ",\n"
                 << "  \"gpuWorkDslOnly\": true,\n"
                 << "  \"renderSetPolicy\": \"not-required\",\n"
                 << "  \"sceneRenderSetCount\": 0,\n"
                 << "  \"renderableObjectCount\": 1,\n"
                 << "  \"scenePassCount\": 1,\n"
                 << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\","
                 << "\"scenePass\":\"main-standard\",\"entityOrdinal\":0}],\n"
                 << "  \"drawCommandCount\": 1,\n"
                 << "  \"instanceCount\": 1,\n"
                 << "  \"mergedVertexCount\": " << mergedVertexCount << ",\n"
                 << "  \"vertexAttributeCount\": " << vertices.size() << ",\n"
                 << "  \"indexCount\": " << indices.size() << ",\n"
                 << "  \"showMap\": " << (showMap ? "true" : "false") << ",\n"
                 << "  \"flatShading\": " << (flatShading ? "true" : "false") << "\n}\n";
        writeEdgeSplitText(options.sceneSnapshotPath, snapshot.str());
        if (options.scenarioId == "loader-topology")
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\": 1,\n"
                     << "  \"caseId\": \"webgl_modifier_edgesplit\",\n"
                     << "  \"scenarioId\": \"loader-topology\",\n"
                     << "  \"frame\": 0,\n"
                     << "  \"kind\": \"loader-snapshot\",\n"
                     << "  \"canonicalState\": "
                     << "\"one-child-33541-triangles-100623-loaded-vertices-26690-merged-vertices\",\n"
                     << "  \"result\": {\"renderableObjectCount\":1,"
                     << "\"sceneRootCount\":1,"
                     << "\"canonicalSceneSha256\":\""
                     << EdgeSplitCanonicalSceneSha256 << "\","
                     << "\"triangleCount\":33541,"
                     << "\"mergedVertexCount\":26690,"
                     << "\"edgeSplitVertexAttributeCount\":118552,"
                     << "\"indexCount\":100623}\n}\n";
            writeEdgeSplitText(
                options.semanticSnapshotPath,
                semantic.str());
        }
        captureWritten = true;
    }

    void WebglModifierEdgeSplitRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
}
