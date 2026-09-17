#include "WebglMaterialsNormalmapObjectSpaceRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

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
        constexpr const char *NefertitiGlbSha256 =
            "d8c00c742b59137695245eefeef0e217d6e33c64eeaeb1904ff39f6b31542639";
        constexpr const char *OrbitReplaySha256 =
            "e89c9dc87d36e4d951a14a6440d14d98c3c57fdcf129198297faf378a38d1208";
        constexpr const char *CanonicalSceneSha256 =
            "129c45249f176ec1c5028544b1b1e6d0df3e76f498ddbe8bf317dbe7bac3d5f0";
        constexpr double Pi = 3.14159265358979323846;

        /** Reads one complete bounded asset or replay payload. */
        eastl::vector<uint8_t> readNormalmapInput(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open a pinned object-space normal input.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) >
                    uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error(
                    "Object-space normal input has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read a complete object-space normal input.");
            }
            return bytes;
        }

        /** Returns one lowercase SHA-256 input identity. */
        eastl::string calculateNormalmapSha256(
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

        /** Converts one decoded image chain into standalone texture upload arrays. */
        eastl::vector<eastl::vector<uint8_t>> packNormalmapMips(
            const eastl::vector<RgbaImageData> &images)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(images.size());
            for (const RgbaImageData &image : images)
            {
                result.push_back(image.pixels);
            }
            return result;
        }

        /** Builds Three's symmetric OpenGL perspective matrix before DSL depth conversion. */
        glm::mat4 makeNormalmapProjection()
        {
            constexpr double FieldOfViewDegrees = 40.0;
            constexpr double Aspect = 800.0 / 500.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 1000.0;
            const double top =
                NearDistance *
                std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = Aspect * height;
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] = float(2.0 * NearDistance / width);
            result[1u][1u] = float(2.0 * NearDistance / height);
            result[2u][2u] =
                float(-(FarDistance + NearDistance) / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] =
                float(-2.0 * FarDistance * NearDistance / depth);
            return result;
        }

        /** Reproduces the one pointer drag consumed by OrbitControls. */
        glm::vec3 makeNormalmapCameraPosition(bool orbit)
        {
            if (!orbit)
            {
                return glm::vec3(-10.0f, 0.0f, 23.0f);
            }
            const double radius = std::sqrt(10.0 * 10.0 + 23.0 * 23.0);
            double theta = std::atan2(-10.0, 23.0);
            double phi = Pi * 0.5;
            theta -= 2.0 * Pi * 60.0 / 500.0;
            phi -= 2.0 * Pi * -30.0 / 500.0;
            return glm::vec3(
                float(radius * std::sin(phi) * std::sin(theta)),
                float(radius * std::cos(phi)),
                float(radius * std::sin(phi) * std::cos(theta)));
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareNormalmapOutputPath(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic UTF-8 evidence artifact. */
        void writeNormalmapText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareNormalmapOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write an object-space normal evidence artifact.");
            }
        }
    } // namespace

    void WebglMaterialsNormalmapObjectSpaceRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-loader" &&
            options.targetFrame == 0u;
        const bool canonical =
            options.scenarioId == "canonical-loader" &&
            options.targetFrame == 0u;
        const bool orbit =
            options.scenarioId == "orbit" &&
            options.targetFrame == 1u;
        if (options.caseId !=
                "webgl_materials_normalmap_object_space" ||
            (!initial && !canonical && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            (orbit != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "Object-space normal adapter requires one locked manifest scenario.");
        }
        device = inDevice;
        const eastl::vector<uint8_t> glbBytes = readNormalmapInput(
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "gltf" / "Nefertiti" / "Nefertiti.glb");
        if (calculateNormalmapSha256(glbBytes) != NefertitiGlbSha256)
        {
            throw std::runtime_error(
                "Nefertiti.glb differs from the r185 asset lock.");
        }
        if (orbit)
        {
            const eastl::vector<uint8_t> replayBytes =
                readNormalmapInput(
                    std::filesystem::path(
                        options.inputReplayPath.c_str()));
            if (calculateNormalmapSha256(replayBytes) != OrbitReplaySha256)
            {
                throw std::runtime_error(
                    "Object-space normal Orbit replay differs from its lock.");
            }
        }
        const ThreeCompat::DecodedGlbMesh mesh =
            ThreeCompat::decodeFirstGlbMesh(glbBytes);
        if (mesh.positions.size() != 11378u * 3u ||
            mesh.textureCoordinates.size() != 11378u * 2u ||
            mesh.indices.size() != 58446u)
        {
            throw std::runtime_error(
                "Nefertiti GLB geometry counts differ from r185.");
        }
        vertices.reserve(11378u);
        for (size_t index = 0u; index < 11378u; ++index)
        {
            vertices.push_back({
                .position = float3(
                    mesh.positions[index * 3u],
                    mesh.positions[index * 3u + 1u],
                    mesh.positions[index * 3u + 2u]),
                .textureCoordinate = float2(
                    mesh.textureCoordinates[index * 2u],
                    mesh.textureCoordinates[index * 2u + 1u]),
            });
        }
        indices.assign(mesh.indices.begin(), mesh.indices.end());
        baseColor = decodeJpegRgba8(
            ThreeCompat::extractGlbBufferView(glbBytes, 2u));
        objectNormal = decodeJpegRgba8(
            ThreeCompat::extractGlbBufferView(glbBytes, 3u));
        if (baseColor.width != 2048u || baseColor.height != 2048u ||
            objectNormal.width != 2048u || objectNormal.height != 2048u)
        {
            throw std::runtime_error(
                "Nefertiti embedded texture dimensions differ from r185.");
        }
        baseColorPixelsSha256 =
            calculateNormalmapSha256(baseColor.pixels);
        objectNormalPixelsSha256 =
            calculateNormalmapSha256(objectNormal.pixels);
        baseColorMips = packNormalmapMips(buildSrgbMipChain(baseColor));
        objectNormalMips = packNormalmapMips(buildUnormMipChain(objectNormal));
        if (baseColorMips.size() != objectNormalMips.size())
        {
            throw std::runtime_error(
                "Nefertiti material mip chains have inconsistent lengths.");
        }
        glm::vec3 minimum(
            mesh.positions[0u],
            mesh.positions[1u],
            mesh.positions[2u]);
        glm::vec3 maximum = minimum;
        for (size_t index = 1u; index < 11378u; ++index)
        {
            const glm::vec3 position(
                mesh.positions[index * 3u],
                mesh.positions[index * 3u + 1u],
                mesh.positions[index * 3u + 2u]);
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        const glm::vec3 center = (minimum + maximum) * 0.5f;
        const glm::mat4 model =
            glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)) *
            glm::translate(glm::mat4(1.0f), -center);
        const glm::vec3 cameraPosition =
            makeNormalmapCameraPosition(orbit);
        const glm::mat4 view = glm::lookAtRH(
            cameraPosition,
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        uniforms.modelView = view * model;
        uniforms.modelViewProjection =
            makeNormalmapProjection() * uniforms.modelView;
        uniforms.ambientPointRoughness =
            float4(0.6f, 4.5f, 0.5f, 0.0f);
        uniforms.sampleOffsetAndPadding = float4(0.0f);
    }

    void WebglMaterialsNormalmapObjectSpaceRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMaterialsNormalmapObjectSpaceRuntimeAdapter::afterFrame(
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
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareNormalmapOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write object-space normal RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"source\": \"gvm-three-r185\",\n"
            << "  \"caseId\": \"webgl_materials_normalmap_object_space\",\n"
            << "  \"scenarioId\": \"" << options.scenarioId.c_str()
            << "\",\n  \"pipeline\": \"" << options.pipeline.c_str()
            << "\",\n  \"backend\": \""
            << threeSampleBackendName(options.backend)
            << "\",\n  \"randomSeed\": " << options.randomSeed
            << ",\n  \"frame\": " << frameIndex
            << ",\n  \"width\": " << width
            << ",\n  \"height\": " << height
            << ",\n  \"rowStrideBytes\": " << uint64_t(width) * 4u
            << ",\n  \"byteCount\": " << byteCount
            << ",\n  \"format\": \"rgba8unorm\",\n"
            << "  \"assetSha256\": \"" << NefertitiGlbSha256 << "\",\n"
            << "  \"baseColorPixelsSha256\": \""
            << baseColorPixelsSha256.c_str() << "\",\n"
            << "  \"objectNormalPixelsSha256\": \""
            << objectNormalPixelsSha256.c_str() << "\",\n"
            << "  \"samplePolicy\": {\"mode\":\"single-sample\","
            << "\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
            << "  \"inputReplay\": ";
        if (options.scenarioId == "orbit")
        {
            metadata
                << "{\"sha256\":\"" << OrbitReplaySha256
                << "\",\"caseId\":\"webgl_materials_normalmap_object_space\","
                << "\"scenarioId\":\"orbit\",\"captureFrame\":1,"
                << "\"eventCount\":3,\"target\":\"body > canvas\"}";
        }
        else
        {
            metadata << "null";
        }
        metadata << "\n}\n";
        writeNormalmapText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot
            << "{\n  \"schemaVersion\": 1,\n"
            << "  \"caseId\": \"webgl_materials_normalmap_object_space\",\n"
            << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\": " << frameIndex << ",\n"
            << "  \"gpuWorkDslOnly\": true,\n"
            << "  \"renderSetPolicy\": \"not-required\",\n"
            << "  \"sceneRenderSetCount\": 0,\n"
            << "  \"renderableObjectCount\": 1,\n"
            << "  \"scenePassCount\": 2,\n"
            << "  \"scenePassSequence\": ["
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"back-faces\",\"entityOrdinal\":0},"
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"front-faces\",\"entityOrdinal\":0}],\n"
            << "  \"drawCommandCount\": 2,\n"
            << "  \"entityCount\": 1,\n"
            << "  \"instanceCount\": 1,\n"
            << "  \"vertexCount\": " << vertices.size() << ",\n"
            << "  \"indexCount\": " << indices.size() << ",\n"
            << "  \"textureWidth\": " << baseColor.width << ",\n"
            << "  \"textureHeight\": " << baseColor.height << ",\n"
            << "  \"mipCount\": " << baseColorMips.size() << "\n}\n";
        writeNormalmapText(options.sceneSnapshotPath, snapshot.str());
        if (options.scenarioId == "canonical-loader")
        {
            std::ostringstream semantic;
            semantic
                << "{\n  \"schemaVersion\": 1,\n"
                << "  \"caseId\": \"webgl_materials_normalmap_object_space\",\n"
                << "  \"scenarioId\": \"canonical-loader\",\n"
                << "  \"frame\": 0,\n"
                << "  \"kind\": \"loader-snapshot\",\n"
                << "  \"canonicalState\": \"canonical-loaded-scene\",\n"
                << "  \"result\": {\"renderableObjectCount\":1,"
                << "\"sceneRootCount\":1,\"canonicalSceneSha256\":\""
                << CanonicalSceneSha256 << "\",\"assetSha256\":\""
                << NefertitiGlbSha256 << "\",\"vertexCount\":"
                << vertices.size() << ",\"indexCount\":" << indices.size()
                << ",\"baseColorSize\":\"2048x2048\","
                << "\"normalMapSize\":\"2048x2048\","
                << "\"normalMapType\":\"object-space\","
                << "\"doubleSided\":true}\n}\n";
            writeNormalmapText(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglMaterialsNormalmapObjectSpaceRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        baseColor.pixels.clear();
        objectNormal.pixels.clear();
        baseColorMips.clear();
        objectNormalMips.clear();
    }
} // namespace GVM::ThreeSamples
