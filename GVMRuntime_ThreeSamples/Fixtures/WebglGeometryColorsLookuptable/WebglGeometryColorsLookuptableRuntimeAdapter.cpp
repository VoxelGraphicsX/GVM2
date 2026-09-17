#include "WebglGeometryColorsLookuptableRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/gtc/matrix_transform.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
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
        constexpr const char *PressureSha256 =
            "07f6e031d4268c2c86c54622b276526843cd6c5f110365cd56a3662bb3aad096";
        constexpr const char *CanonicalSceneSha256 =
            "d43766428ae76e4b9a30d5051758fc2b6832542501360c0ec1ec2a9c0084a76b";
        constexpr const char *CoolToWarmReplaySha256 =
            "f7b46e66b3d7e39d7cfc6c1b7688e87b9d8e00c7e20f1455f3e43444800f79fa";
        constexpr uint32_t PressureVertexCount = 25110u;

        /** Reads one complete bounded pressure JSON asset. */
        eastl::vector<uint8_t> readLookuptableAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open pressure.json.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) > uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error("pressure.json has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read pressure.json.");
            return bytes;
        }

        /** Returns one lowercase SHA-256 identity for a locked byte payload. */
        eastl::string calculateLookuptableSha256(
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

        /** Converts one r185 sRGB numeric channel to linear working space. */
        float lookuptableSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Returns one raw color-map endpoint for rainbow or cool-to-warm. */
        glm::vec3 lookuptableMapColor(bool coolToWarm, uint32_t stop)
        {
            static constexpr float Rainbow[5u][3u] = {
                {0.0f, 0.0f, 255.0f}, {0.0f, 255.0f, 255.0f},
                {0.0f, 255.0f, 0.0f}, {255.0f, 255.0f, 0.0f},
                {255.0f, 0.0f, 0.0f}};
            static constexpr float CoolToWarm[5u][3u] = {
                {60.0f, 78.0f, 194.0f}, {155.0f, 188.0f, 255.0f},
                {220.0f, 220.0f, 220.0f}, {246.0f, 163.0f, 133.0f},
                {180.0f, 4.0f, 38.0f}};
            const float (*colors)[3u] = coolToWarm ? CoolToWarm : Rainbow;
            return glm::vec3(
                colors[stop][0u] / 255.0f,
                colors[stop][1u] / 255.0f,
                colors[stop][2u] / 255.0f);
        }

        /** Evaluates one raw five-stop color map at a normalized LUT coordinate. */
        glm::vec3 interpolateLookuptableMap(bool coolToWarm, float value)
        {
            static constexpr float Stops[5u] = {0.0f, 0.2f, 0.5f, 0.8f, 1.0f};
            uint32_t upper = 1u;
            while (upper < 4u && value > Stops[upper]) ++upper;
            const float fraction =
                (value - Stops[upper - 1u]) /
                (Stops[upper] - Stops[upper - 1u]);
            return glm::mix(
                lookuptableMapColor(coolToWarm, upper - 1u),
                lookuptableMapColor(coolToWarm, upper),
                fraction);
        }

        /** Builds the exact 33-entry Lut array including endpoint conversion quirks. */
        eastl::array<glm::vec3, 33u> buildLookuptableColors(bool coolToWarm)
        {
            eastl::array<glm::vec3, 33u> result = {};
            result[0u] = lookuptableMapColor(coolToWarm, 0u);
            result[32u] = lookuptableMapColor(coolToWarm, 4u);
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                result[0u][channel] = lookuptableSrgbToLinear(result[0u][channel]);
                result[32u][channel] = lookuptableSrgbToLinear(result[32u][channel]);
            }
            for (uint32_t index = 1u; index < 32u; ++index)
            {
                result[index] = interpolateLookuptableMap(
                    coolToWarm, float(index) / 32.0f);
            }
            return result;
        }

        /** Applies Lut.getColor and the example's explicit convertSRGBToLinear call. */
        glm::vec3 evaluateLookuptablePressure(
            float pressure,
            const eastl::array<glm::vec3, 33u> &lut)
        {
            const float normalized = std::clamp(pressure, 0.0f, 2000.0f) / 2000.0f;
            const uint32_t index = static_cast<uint32_t>(std::round(normalized * 32.0f));
            glm::vec3 color = lut[index];
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                color[channel] = lookuptableSrgbToLinear(color[channel]);
            }
            return color;
        }

        /** Creates the exact WebGL perspective matrix before DSL depth conversion. */
        glm::mat4 makeLookuptableProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 100.0;
            constexpr double Aspect = 800.0 / 500.0;
            const double top = NearDistance * std::tan(60.0 * 3.14159265358979323846 / 360.0);
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

        /** Creates parent directories for one optional evidence artifact. */
        void prepareLookuptableOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one deterministic UTF-8 evidence artifact when requested. */
        void writeLookuptableText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareLookuptableOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write LUT evidence.");
        }
    }

    void WebglGeometryColorsLookuptableRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            (options.scenarioId == "initial-loader" ||
             options.scenarioId == "canonical-loader") &&
            options.targetFrame == 0u;
        const bool coolToWarm =
            options.scenarioId == "cool-to-warm" &&
            options.targetFrame == 1u;
        if (options.caseId != "webgl_geometry_colors_lookuptable" ||
            (!initial && !coolToWarm) || options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            (coolToWarm != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "Lookup-table adapter requires one locked Manifest scenario.");
        }
        device = inDevice;
        const eastl::vector<uint8_t> bytes = readLookuptableAsset(
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "json" / "pressure.json");
        pressureSha256 = calculateLookuptableSha256(bytes);
        if (pressureSha256 != PressureSha256)
        {
            throw std::runtime_error("pressure.json differs from the r185 lock.");
        }
        const nlohmann::json document = nlohmann::json::parse(
            bytes.begin(), bytes.end(), nullptr, true, true);
        const nlohmann::json &attributes = document.at("data").at("attributes");
        const nlohmann::json &positions = attributes.at("position").at("array");
        const nlohmann::json &pressures = attributes.at("pressure").at("array");
        if (positions.size() != size_t(PressureVertexCount) * 3u ||
            pressures.size() != PressureVertexCount)
        {
            throw std::runtime_error("pressure.json topology differs from r185.");
        }
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        eastl::vector<glm::vec3> centeredPositions(PressureVertexCount);
        for (uint32_t vertex = 0u; vertex < PressureVertexCount; ++vertex)
        {
            glm::vec3 position(
                positions[vertex * 3u].get<float>(),
                positions[vertex * 3u + 1u].get<float>(),
                positions[vertex * 3u + 2u].get<float>());
            centeredPositions[vertex] = position;
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        const glm::vec3 center = (minimum + maximum) * 0.5f;
        for (glm::vec3 &position : centeredPositions) position -= center;
        const eastl::array<glm::vec3, 33u> lut =
            buildLookuptableColors(coolToWarm);
        vertices.resize(PressureVertexCount);
        for (uint32_t triangle = 0u; triangle < PressureVertexCount; triangle += 3u)
        {
            const glm::vec3 normal = glm::normalize(glm::cross(
                centeredPositions[triangle + 1u] - centeredPositions[triangle],
                centeredPositions[triangle + 2u] - centeredPositions[triangle]));
            for (uint32_t corner = 0u; corner < 3u; ++corner)
            {
                const uint32_t vertex = triangle + corner;
                const glm::vec3 color = evaluateLookuptablePressure(
                    pressures[vertex].get<float>(), lut);
                vertices[vertex] = {
                    float3(centeredPositions[vertex]),
                    float3(normal),
                    float3(color)};
            }
        }
        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(0.0f, 0.0f, 10.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        uniforms.modelView = view;
        uniforms.modelViewProjection = makeLookuptableProjection() * view;
        uniforms.lightAndMap = float4(
            3.0f,
            lookuptableSrgbToLinear(245.0f / 255.0f),
            0.0f,
            coolToWarm ? 1.0f : 0.0f);
    }

    void WebglGeometryColorsLookuptableRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryColorsLookuptableRuntimeAdapter::afterFrame(
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
            prepareLookuptableOutputPath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write LUT RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"source\":\"gvm-three-r185\",\n"
            << "  \"caseId\":\"webgl_geometry_colors_lookuptable\",\n"
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
            << "  \"pressureSha256\":\"" << pressureSha256.c_str() << "\"";
        if (options.scenarioId == "cool-to-warm")
        {
            metadata
                << ",\n  \"inputReplay\":{\"schemaVersion\":1,"
                << "\"caseId\":\"webgl_geometry_colors_lookuptable\","
                << "\"scenarioId\":\"cool-to-warm\",\"captureFrame\":1,"
                << "\"sha256\":\"" << CoolToWarmReplaySha256 << "\","
                << "\"target\":\".lil-gui select\",\"eventCount\":1}"
                << "\n";
        }
        else
        {
            metadata << "\n";
        }
        metadata << "}\n";
        writeLookuptableText(options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"webgl_geometry_colors_lookuptable\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"renderSetPolicy\":\"not-required\",\n"
            << "  \"sceneRenderSetCount\":0,\n"
            << "  \"renderableObjectCount\":1,\n"
            << "  \"instanceCount\":1,\n"
            << "  \"vertexCount\":25110,\n"
            << "  \"drawCommandCount\":1,\n"
            << "  \"scenePassCount\":1,\n"
            << "  \"screenPassCount\":2,\n"
            << "  \"scenePassSequence\":[],\n"
            << "  \"sampleCount\":1,\n"
            << "  \"msaaEnabled\":false\n}\n";
        writeLookuptableText(options.sceneSnapshotPath, scene.str());
        if (!options.semanticSnapshotPath.empty())
        {
            std::ostringstream semantic;
            semantic
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_geometry_colors_lookuptable\",\n"
                << "  \"scenarioId\":\"canonical-loader\",\n"
                << "  \"frame\":0,\n"
                << "  \"kind\":\"loader-snapshot\",\n"
                << "  \"canonicalState\":\"canonical-loaded-scene\",\n"
                << "  \"result\":{\"renderableObjectCount\":1,"
                << "\"sceneRootCount\":2,"
                << "\"canonicalSceneSha256\":\"" << CanonicalSceneSha256 << "\","
                << "\"assetCount\":1,\"vertexCount\":25110,"
                << "\"triangleCount\":8370,\"pressureSha256\":\""
                << PressureSha256 << "\"}\n}\n";
            writeLookuptableText(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglGeometryColorsLookuptableRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
    }
}
