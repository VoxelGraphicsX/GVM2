#include "WebglMorphtargetsHorseRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *HorseGlbSha256 =
            "bebaa4a60ba373317e25bf20f049f26ad0f5c86d4731ab67d46eb8c93c920947";
        constexpr uint32_t HorseVertexCount = 796u;
        constexpr uint32_t HorseIndexCount = 2952u;
        constexpr uint32_t HorseTargetCount = 15u;
        constexpr uint32_t HorseJsonOffset = 20u;
        constexpr uint32_t HorsePositionOffset = 0u;
        constexpr uint32_t HorseColorOffset = 9552u;
        constexpr uint32_t HorseFirstMorphOffset = 25472u;
        constexpr uint32_t HorseMorphByteCount = 9552u;
        constexpr uint32_t HorseIndexOffset = 168752u;
        constexpr double Pi = 3.14159265358979323846;

        /** Reads one complete bounded Horse GLB. */
        eastl::vector<uint8_t> readHorseGlb(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error("Could not open the pinned Horse GLB.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) > uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error("Pinned Horse GLB has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete Horse GLB.");
            }
            return bytes;
        }

        /** Returns one lowercase SHA-256 identity for the locked GLB. */
        eastl::string calculateHorseSha256(const eastl::vector<uint8_t> &bytes)
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

        /** Reads one little-endian uint32 value from a validated byte range. */
        uint32_t readHorseUint32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset + sizeof(uint32_t) > bytes.size())
            {
                throw std::runtime_error("Horse GLB uint32 access is out of bounds.");
            }
            uint32_t value = 0u;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        /** Reads one little-endian float value from a validated BIN range. */
        float readHorseFloat(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset + sizeof(float) > bytes.size())
            {
                throw std::runtime_error("Horse GLB float access is out of bounds.");
            }
            float value = 0.0f;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        /** Reads one little-endian uint16 index from a validated BIN range. */
        uint16_t readHorseUint16(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset + sizeof(uint16_t) > bytes.size())
            {
                throw std::runtime_error("Horse GLB uint16 access is out of bounds.");
            }
            uint16_t value = 0u;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        /** Creates Three's fixed perspective matrix before DSL depth conversion. */
        glm::mat4 makeHorseProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10000.0;
            constexpr double Aspect = 800.0 / 500.0;
            const double top = NearDistance * std::tan(50.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = height * Aspect;
            glm::mat4 result(0.0f);
            result[0u][0u] = float(2.0 * NearDistance / width);
            result[1u][1u] = float(2.0 * NearDistance / height);
            result[2u][2u] = float(-(FarDistance + NearDistance) /
                                    (FarDistance - NearDistance));
            result[2u][3u] = -1.0f;
            result[3u][2u] = float(-2.0 * FarDistance * NearDistance /
                                    (FarDistance - NearDistance));
            return result;
        }

        /** Converts one authored sRGB light component to linear working space. */
        float horseSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Creates parent directories for one optional Horse evidence artifact. */
        void prepareHorseOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic UTF-8 Horse evidence artifact. */
        void writeHorseText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareHorseOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error("Could not write one Horse evidence artifact.");
            }
        }
    } // namespace

    void WebglMorphtargetsHorseRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            (options.scenarioId == "loader-snapshot" ||
             options.scenarioId == "initial-loader") &&
            options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 60u;
        if (options.caseId != "webgl_morphtargets_horse" ||
            (!initial && !animated) || options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() || !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Horse adapter requires one locked Manifest scenario.");
        }
        device = inDevice;
        const eastl::vector<uint8_t> bytes = readHorseGlb(
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "gltf" / "Horse.glb");
        glbSha256 = calculateHorseSha256(bytes);
        if (glbSha256 != HorseGlbSha256 ||
            readHorseUint32(bytes, 0u) != 0x46546c67u ||
            readHorseUint32(bytes, 4u) != 2u)
        {
            throw std::runtime_error("Horse GLB differs from the r185 lock.");
        }
        const uint32_t jsonByteCount = readHorseUint32(bytes, 12u);
        const size_t binStart = HorseJsonOffset + size_t(jsonByteCount) + 8u;
        if (binStart + HorseIndexOffset + HorseIndexCount * sizeof(uint16_t) >
            bytes.size())
        {
            throw std::runtime_error("Horse GLB BIN layout is incomplete.");
        }

        vertices.resize(HorseVertexCount);
        for (uint32_t vertex = 0u; vertex < HorseVertexCount; ++vertex)
        {
            const size_t position =
                binStart + HorsePositionOffset + size_t(vertex) * 12u;
            const size_t color =
                binStart + HorseColorOffset + size_t(vertex) * 12u;
            vertices[vertex] = {
                float3(readHorseFloat(bytes, position),
                       readHorseFloat(bytes, position + 4u),
                       readHorseFloat(bytes, position + 8u)),
                float3(readHorseFloat(bytes, color),
                       readHorseFloat(bytes, color + 4u),
                       readHorseFloat(bytes, color + 8u)),
            };
        }
        morphPositions.resize(HorseVertexCount * HorseTargetCount);
        for (uint32_t target = 0u; target < HorseTargetCount; ++target)
        {
            for (uint32_t vertex = 0u; vertex < HorseVertexCount; ++vertex)
            {
                const size_t position =
                    binStart + HorseFirstMorphOffset +
                    size_t(target) * HorseMorphByteCount +
                    size_t(vertex) * 12u;
                morphPositions[target * HorseVertexCount + vertex] = float4(
                    readHorseFloat(bytes, position),
                    readHorseFloat(bytes, position + 4u),
                    readHorseFloat(bytes, position + 8u),
                    0.0f);
            }
        }
        indices.resize(HorseIndexCount);
        for (uint32_t index = 0u; index < HorseIndexCount; ++index)
        {
            indices[index] = readHorseUint16(
                bytes,
                binStart + HorseIndexOffset + size_t(index) * 2u);
        }

        const double thetaDegrees = animated ? 6.1 : 0.1;
        const glm::vec3 cameraPosition(
            float(600.0 * std::sin(thetaDegrees * Pi / 180.0)),
            300.0f,
            float(600.0 * std::cos(thetaDegrees * Pi / 180.0)));
        const glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(1.5f));
        const glm::mat4 view = glm::lookAtRH(
            cameraPosition,
            glm::vec3(0.0f, 150.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        uniforms.modelView = view * model;
        uniforms.modelViewProjection = makeHorseProjection() * uniforms.modelView;
        uniforms.morphWeights0 = float4(1.0f, 0.0f, 0.0f, 0.0f);
        uniforms.morphWeights1 = float4(0.0f);
        uniforms.morphWeights2 = float4(0.0f);
        uniforms.morphWeights3 = float4(0.0f);
        const glm::vec3 light0 = glm::normalize(glm::vec3(view * glm::vec4(
            glm::normalize(glm::vec3(1.0f)), 0.0f)));
        const glm::vec3 light1 = glm::normalize(glm::vec3(view * glm::vec4(
            glm::normalize(glm::vec3(-1.0f)), 0.0f)));
        uniforms.lightDirectionIntensity0 = float4(light0, 5.0f);
        uniforms.lightDirectionIntensity1 = float4(light1, 5.0f);
        uniforms.lightColor0 = float4(
            horseSrgbToLinear(239.0f / 255.0f),
            horseSrgbToLinear(239.0f / 255.0f),
            1.0f,
            0.0f);
        uniforms.lightColor1 = float4(
            1.0f,
            horseSrgbToLinear(239.0f / 255.0f),
            horseSrgbToLinear(239.0f / 255.0f),
            0.0f);
    }

    void WebglMorphtargetsHorseRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMorphtargetsHorseRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareHorseOutputPath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error("Could not write the Horse RGBA capture.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"source\":\"gvm-three-r185\",\n"
            << "  \"caseId\":\"webgl_morphtargets_horse\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
            << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"randomSeed\":" << options.randomSeed << ",\n"
            << "  \"width\":" << width << ",\n"
            << "  \"height\":" << height << ",\n"
            << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
            << "  \"byteCount\":" << byteCount << ",\n"
            << "  \"format\":\"rgba8unorm\",\n"
            << "  \"sampleCount\":1,\n"
            << "  \"msaaEnabled\":false,\n"
            << "  \"glbSha256\":\"" << glbSha256.c_str() << "\"\n}\n";
        writeHorseText(options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"webgl_morphtargets_horse\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"renderSetPolicy\":\"not-required\",\n"
            << "  \"sceneRenderSetCount\":0,\n"
            << "  \"renderableObjectCount\":1,\n"
            << "  \"entityCount\":1,\n"
            << "  \"instanceCount\":1,\n"
            << "  \"vertexCount\":796,\n"
            << "  \"indexCount\":2952,\n"
            << "  \"drawCommandCount\":1,\n"
            << "  \"scenePassCount\":1,\n"
            << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"main-standard-morph\"}],\n"
            << "  \"sampleCount\":1,\n"
            << "  \"msaaEnabled\":false\n}\n";
        writeHorseText(options.sceneSnapshotPath, scene.str());
        if (!options.semanticSnapshotPath.empty())
        {
            std::ostringstream semantic;
            semantic
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_morphtargets_horse\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"kind\":\"loader-snapshot\",\n"
                << "  \"canonicalState\":\"horse-one-primitive-796-vertices-2952-indices-15-targets-16-key-clip\",\n"
                << "  \"result\":{\"renderableObjectCount\":1,"
                << "\"sceneRootCount\":1,\"assetCount\":1,"
                << "\"canonicalSceneSha256\":"
                << "\"ce0bf15118e9f0a6e1a40b02745178e7943a71375d2679a84b792b48a904bbb4\","
                << "\"vertexCount\":796,\"indexCount\":2952,"
                << "\"morphTargetCount\":15,\"animationKeyCount\":16,"
                << "\"glbSha256\":\"" << HorseGlbSha256 << "\"}\n}\n";
            writeHorseText(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglMorphtargetsHorseRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        morphPositions.clear();
        indices.clear();
        vertices.clear();
    }
} // namespace GVM::ThreeSamples
