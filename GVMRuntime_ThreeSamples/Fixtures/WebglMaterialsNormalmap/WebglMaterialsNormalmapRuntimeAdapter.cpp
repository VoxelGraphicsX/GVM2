#include "WebglMaterialsNormalmapRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/gtc/matrix_inverse.hpp>
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
        constexpr const char *GlbSha256 =
            "402b8a8ac9f03232e6d64b5962929703a069daf99d3c49ac8eb0e48bedc9c576";
        constexpr const char *DiffuseSha256 =
            "e976d73b31407f8d0967412bf468019ed26a5d5a32cf5811aabff7e816458a65";
        constexpr const char *SpecularSha256 =
            "cbb96b60e355b804d9b6c7338372d13ec8cc7adf664f50a00a3d32c66afd293a";
        constexpr const char *NormalSha256 =
            "36925e51ad9b324b94e8faf4692da1b4132809f2762bb8d5bd549ffd215d4ca6";
        constexpr const char *DisableOrbitReplaySha256 =
            "eb70653f053296d3dca0337ffad679e7a94f775ee1ccef6eafe790a2c73f3731";
        constexpr double Pi = 3.14159265358979323846;

        /** Reads one complete bounded asset or replay input. */
        eastl::vector<uint8_t> readNormalmapAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open a pinned normal-map input.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 || uint64_t(byteCount) > uint64_t(std::numeric_limits<CC_LONG>::max()))
                throw std::runtime_error("A pinned normal-map input has an invalid size.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read a complete normal-map input.");
            return bytes;
        }

        /** Returns one lowercase SHA-256 identity. */
        eastl::string calculateNormalmapSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char Digits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(Digits[value >> 4u]);
                result.push_back(Digits[value & 15u]);
            }
            return result;
        }

        /** Packs decoded mip levels into generated texture upload arrays. */
        eastl::vector<eastl::vector<uint8_t>> packNormalmapMips(
            const eastl::vector<RgbaImageData> &images)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(images.size());
            for (const RgbaImageData &image : images) result.push_back(image.pixels);
            return result;
        }

        /** Builds Three's OpenGL perspective projection before DSL depth conversion. */
        glm::mat4 makeNormalmapProjection()
        {
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 100.0;
            const double top = NearDistance * std::tan(27.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = height * (800.0 / 500.0);
            glm::mat4 result(0.0f);
            result[0][0] = float(2.0 * NearDistance / width);
            result[1][1] = float(2.0 * NearDistance / height);
            result[2][2] = float(-(FarDistance + NearDistance) / (FarDistance - NearDistance));
            result[2][3] = -1.0f;
            result[3][2] = float(-2.0 * FarDistance * NearDistance / (FarDistance - NearDistance));
            return result;
        }

        /** Reproduces the canonical sixty-by-minus-thirty OrbitControls drag. */
        glm::vec3 makeNormalmapCameraPosition(bool orbit)
        {
            if (!orbit) return glm::vec3(0.0f, 0.0f, 12.0f);
            constexpr double DampingFactor = 0.05;
            constexpr double UpdateCount = 3.0;
            const double appliedDrag =
                1.0 - std::pow(1.0 - DampingFactor, UpdateCount);
            double theta = -2.0 * Pi * 60.0 / 500.0 * appliedDrag;
            double phi = Pi * 0.5 -
                2.0 * Pi * -30.0 / 500.0 * appliedDrag;
            return glm::vec3(
                float(12.0 * std::sin(phi) * std::sin(theta)),
                float(12.0 * std::cos(phi)),
                float(12.0 * std::sin(phi) * std::cos(theta)));
        }

        /** Converts one authored sRGB material channel to linear space. */
        float normalmapSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Expands indexed triangles and computes the derivative-equivalent tangent frame. */
        eastl::vector<WebglMaterialsNormalmapVertex> buildNormalmapVertices(
            const ThreeCompat::DecodedGlbMesh &mesh)
        {
            eastl::vector<WebglMaterialsNormalmapVertex> result;
            result.reserve(mesh.indices.size());
            for (size_t triangle = 0u; triangle < mesh.indices.size(); triangle += 3u)
            {
                const uint32_t i0 = mesh.indices[triangle];
                const uint32_t i1 = mesh.indices[triangle + 1u];
                const uint32_t i2 = mesh.indices[triangle + 2u];
                const glm::vec3 p0(
                    mesh.positions[i0 * 3u],
                    mesh.positions[i0 * 3u + 1u],
                    mesh.positions[i0 * 3u + 2u]);
                const glm::vec3 p1(
                    mesh.positions[i1 * 3u],
                    mesh.positions[i1 * 3u + 1u],
                    mesh.positions[i1 * 3u + 2u]);
                const glm::vec3 p2(
                    mesh.positions[i2 * 3u],
                    mesh.positions[i2 * 3u + 1u],
                    mesh.positions[i2 * 3u + 2u]);
                const glm::vec2 uv0(
                    mesh.textureCoordinates[i0 * 2u],
                    mesh.textureCoordinates[i0 * 2u + 1u]);
                const glm::vec2 uv1(
                    mesh.textureCoordinates[i1 * 2u],
                    mesh.textureCoordinates[i1 * 2u + 1u]);
                const glm::vec2 uv2(
                    mesh.textureCoordinates[i2 * 2u],
                    mesh.textureCoordinates[i2 * 2u + 1u]);
                const glm::vec3 edge1 = p1 - p0;
                const glm::vec3 edge2 = p2 - p0;
                const glm::vec2 delta1 = uv1 - uv0;
                const glm::vec2 delta2 = uv2 - uv0;
                const float determinant = delta1.x * delta2.y - delta1.y * delta2.x;
                glm::vec3 tangent(1.0f, 0.0f, 0.0f);
                glm::vec3 bitangent(0.0f, 1.0f, 0.0f);
                if (std::abs(determinant) > 1.0e-12f)
                {
                    tangent = (edge1 * delta2.y - edge2 * delta1.y) / determinant;
                    bitangent = (edge2 * delta1.x - edge1 * delta2.x) / determinant;
                }
                const uint32_t triangleIndices[] = {i0, i1, i2};
                for (const uint32_t index : triangleIndices)
                {
                    result.push_back({
                        .position = float3(mesh.positions[index * 3u], mesh.positions[index * 3u + 1u], mesh.positions[index * 3u + 2u]),
                        .normal = float3(mesh.normals[index * 3u], mesh.normals[index * 3u + 1u], mesh.normals[index * 3u + 2u]),
                        .textureCoordinate = float2(mesh.textureCoordinates[index * 2u], mesh.textureCoordinates[index * 2u + 1u]),
                        .tangent = float3(tangent.x, tangent.y, tangent.z),
                        .bitangent = float3(bitangent.x, bitangent.y, bitangent.z),
                    });
                }
            }
            return result;
        }

        /** Creates parent directories for one requested output artifact. */
        void prepareNormalmapOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeNormalmapText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareNormalmapOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write normal-map evidence.");
        }
    } // namespace

    void WebglMaterialsNormalmapRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial-loader" && options.targetFrame == 0u;
        const bool canonical = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
        const bool disabled = options.scenarioId == "normal-disabled-orbit" && options.targetFrame == 1u;
        if (options.caseId != "webgl_materials_normalmap" ||
            (!initial && !canonical && !disabled) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty() ||
            (disabled != !options.inputReplayPath.empty()))
            throw std::invalid_argument("Normal-map adapter requires one locked Manifest scenario.");
        device = inDevice;
        const std::filesystem::path root =
            std::filesystem::path(options.assetRoot.c_str()) / "models" / "gltf" / "LeePerrySmith";
        const eastl::vector<uint8_t> glbBytes = readNormalmapAsset(root / "LeePerrySmith.glb");
        const eastl::vector<uint8_t> diffuseBytes = readNormalmapAsset(root / "Map-COL.jpg");
        const eastl::vector<uint8_t> specularBytes = readNormalmapAsset(root / "Map-SPEC.jpg");
        const eastl::vector<uint8_t> normalBytes = readNormalmapAsset(root / "Infinite-Level_02_Tangent_SmoothUV.jpg");
        if (calculateNormalmapSha256(glbBytes) != GlbSha256 ||
            calculateNormalmapSha256(diffuseBytes) != DiffuseSha256 ||
            calculateNormalmapSha256(specularBytes) != SpecularSha256 ||
            calculateNormalmapSha256(normalBytes) != NormalSha256)
            throw std::runtime_error("One normal-map asset differs from the r185 lock.");
        if (disabled)
        {
            replaySha256 = calculateNormalmapSha256(readNormalmapAsset(
                std::filesystem::path(options.inputReplayPath.c_str())));
            if (replaySha256 != DisableOrbitReplaySha256)
                throw std::runtime_error("The normal-map input replay differs from the lock.");
        }
        const ThreeCompat::DecodedGlbMesh mesh = ThreeCompat::decodeFirstGlbMesh(glbBytes);
        if (mesh.positions.size() != 9279u * 3u ||
            mesh.normals.size() != 9279u * 3u ||
            mesh.textureCoordinates.size() != 9279u * 2u ||
            mesh.indices.size() != 53052u)
            throw std::runtime_error("Lee Perry Smith geometry differs from the r185 lock.");
        vertices = buildNormalmapVertices(mesh);
        indices.resize(vertices.size());
        for (uint32_t index = 0u; index < uint32_t(indices.size()); ++index) indices[index] = index;
        diffuse = decodeJpegRgba8(diffuseBytes);
        specular = decodeJpegRgba8(specularBytes);
        normal = decodeJpegRgba8(normalBytes);
        diffuseMips = packNormalmapMips(buildSrgbMipChain(diffuse));
        specularMips = packNormalmapMips(buildSrgbMipChain(specular));
        normalMips = packNormalmapMips(buildUnormMipChain(normal));

        const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f));
        const glm::mat4 view = glm::lookAtRH(
            makeNormalmapCameraPosition(disabled), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        uniforms.modelView = view * model;
        uniforms.modelViewProjection = makeNormalmapProjection() * uniforms.modelView;
        uniforms.normalTransform = glm::transpose(glm::inverse(uniforms.modelView));
        const glm::vec4 pointView = view * glm::vec4(0.0f, 0.0f, 6.0f, 1.0f);
        const glm::vec4 directionalView = view * glm::vec4(
            glm::normalize(glm::vec3(1.0f, -0.5f, -1.0f)), 0.0f);
        uniforms.pointLightPositionAndIntensity = glm::vec4(glm::vec3(pointView), 30.0f);
        uniforms.directionalLightAndIntensity = glm::vec4(glm::vec3(directionalView), 3.0f);
        const float material = normalmapSrgbToLinear(239.0f / 255.0f);
        uniforms.materialAndNormalState = glm::vec4(material, material, material, disabled ? 0.0f : 1.0f);
        uniforms.viewport = glm::vec4(800.0f, 500.0f, 1.0f, 0.0f);
    }

    void WebglMaterialsNormalmapRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer; (void)options; (void)frameIndex;
    }

    void WebglMaterialsNormalmapRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const bool disabled =
            options.scenarioId == "normal-disabled-orbit";
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareNormalmapOutput(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write normal-map RGBA output.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
                 << "  \"caseId\":\"webgl_materials_normalmap\",\n  \"scenarioId\":\""
                 << options.scenarioId.c_str() << "\",\n  \"pipeline\":\"" << options.pipeline.c_str()
                 << "\",\n  \"backend\":\"" << threeSampleBackendName(options.backend)
                 << "\",\n  \"frame\":" << frameIndex << ",\n  \"randomSeed\":" << options.randomSeed
                 << ",\n  \"width\":" << width << ",\n  \"height\":" << height
                 << ",\n  \"rowStrideBytes\":" << uint64_t(width) * 4u
                 << ",\n  \"byteCount\":" << byteCount
                 << ",\n  \"format\":\"rgba8unorm\",\n  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
                 << "  \"inputReplay\":";
        if (disabled)
        {
            metadata
                << "{\"sha256\":\"" << DisableOrbitReplaySha256
                << "\",\"caseId\":\"webgl_materials_normalmap\","
                << "\"scenarioId\":\"normal-disabled-orbit\","
                << "\"captureFrame\":1,\"eventCount\":3,"
                << "\"target\":\"body > div:nth-of-type(2) > canvas\"},\n";
        }
        else
        {
            metadata << "null,\n";
        }
        metadata
                 << "  \"renderSetCount\":0,\n  \"entityCount\":1,\n  \"instanceCount\":1,\n"
                 << "  \"vertexCount\":9279,\n  \"indexCount\":53052,\n  \"scenePassCount\":1,\n  \"screenPassCount\":4\n}\n";
        writeNormalmapText(options.captureMetadataPath, metadata.str());
        writeNormalmapText(options.sceneSnapshotPath,
            std::string("{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_materials_normalmap\",\n  \"scenarioId\":\"") +
            options.scenarioId.c_str() + "\",\n  \"frame\":" + std::to_string(frameIndex) +
            ",\n  \"gpuWorkDslOnly\":true,\n  \"renderSetPolicy\":\"not-required\",\n"
            "  \"sceneRenderSetCount\":0,\n  \"renderableObjectCount\":1,\n  \"instanceCount\":1,\n"
            "  \"vertexCount\":9279,\n  \"indexCount\":53052,\n  \"drawCommandCount\":1,\n"
            "  \"scenePassCount\":1,\n  \"screenPassCount\":4,\n  \"sampleCount\":1,\n  \"msaaEnabled\":false\n}\n");
        writeNormalmapText(options.semanticSnapshotPath,
            "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_materials_normalmap\",\n"
            "  \"scenarioId\":\"canonical-loader\",\n  \"frame\":0,\n  \"kind\":\"loader-snapshot\",\n"
            "  \"canonicalState\":\"canonical-loaded-scene\",\n  \"result\":{\n"
            "    \"renderableObjectCount\":1,\n    \"sceneRootCount\":1,\n"
            "    \"canonicalSceneSha256\":\"ec62143f5c977a2907b785fdf4df92a6bd9c15a6ca41541f55b095eb02c4f81f\",\n"
            "    \"assetCount\":4,\n    \"vertexCount\":9279,\n    \"indexCount\":53052,\n"
            "    \"glbSha256\":\"402b8a8ac9f03232e6d64b5962929703a069daf99d3c49ac8eb0e48bedc9c576\"\n  }\n}\n");
        captureWritten = true;
    }

    void WebglMaterialsNormalmapRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer; (void)options;
        vertices.clear(); indices.clear();
        diffuseMips.clear(); specularMips.clear(); normalMips.clear();
        diffuse.pixels.clear(); specular.pixels.clear(); normal.pixels.clear();
    }
} // namespace GVM::ThreeSamples
