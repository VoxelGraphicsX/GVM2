#include "WebgpuTslRagingSeaRuntimeAdapter.hpp"

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
        constexpr const char *SettingsReplaySha256 =
            "dfb5e31190bc546c5a791eca84b82b28e2a4134afc704c012135aa1c8c2d80eb";
        constexpr const char *OrbitReplaySha256 =
            "635c193cfb81451ca92b36dd0af901b9866bddec31b05b89c97ed60ca6201d55";
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t GridSegments = 256u;

        /** Stores the four DFG texels required from one immutable LUT row. */
        struct RagingSeaDfgSelectedRow
        {
            uint32_t row;
            uint32_t column1;
            uint32_t column2;
            uint32_t column6;
            uint32_t column7;
        };

        /** Reads one complete bounded replay file. */
        eastl::vector<uint8_t> readRagingSeaReplay(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error("Could not open the pinned raging-sea replay.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) >
                    uint64_t(std::numeric_limits<CC_LONG>::max()))
                throw std::runtime_error("The raging-sea replay has an invalid size.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
                throw std::runtime_error("Could not read the complete raging-sea replay.");
            return bytes;
        }

        /** Returns one lowercase SHA-256 identity. */
        eastl::string calculateRagingSeaSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
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

        /** Creates parent directories for one requested evidence artifact. */
        void prepareRagingSeaOutput(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeRagingSeaText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareRagingSeaOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error("Could not write raging-sea evidence.");
        }

        /** Converts one authored sRGB channel into linear working space. */
        float ragingSeaSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Converts one authored hexadecimal color into a linear vector. */
        glm::vec3 makeRagingSeaLinearColor(
            uint32_t red,
            uint32_t green,
            uint32_t blue)
        {
            return glm::vec3(
                ragingSeaSrgbToLinear(float(red) / 255.0f),
                ragingSeaSrgbToLinear(float(green) / 255.0f),
                ragingSeaSrgbToLinear(float(blue) / 255.0f));
        }

        /** Builds Three's OpenGL perspective matrix before DSL depth conversion. */
        glm::mat4 makeRagingSeaProjection()
        {
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 10.0;
            const double top = NearDistance * std::tan(50.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = height * 1.6;
            glm::mat4 result(0.0f);
            result[0][0] = float(2.0 * NearDistance / width);
            result[1][1] = float(2.0 * NearDistance / height);
            result[2][2] = float(
                -(FarDistance + NearDistance) /
                (FarDistance - NearDistance));
            result[2][3] = -1.0f;
            result[3][2] = float(
                -2.0 * FarDistance * NearDistance /
                (FarDistance - NearDistance));
            return result;
        }

        /** Reproduces the damped canonical OrbitControls drag at frame 122. */
        glm::vec3 makeRagingSeaCameraPosition(bool orbit)
        {
            const glm::vec3 target(0.0f, -0.25f, 0.0f);
            const glm::vec3 initial(1.25f, 1.25f, 1.25f);
            if (!orbit) return initial;
            const glm::vec3 offset = initial - target;
            const double radius = glm::length(offset);
            double theta = std::atan2(double(offset.x), double(offset.z));
            double phi = std::acos(double(offset.y) / radius);
            constexpr double DampingFactor = 0.05;
            constexpr double UpdateCount = 123.0;
            const double applied =
                1.0 - std::pow(1.0 - DampingFactor, UpdateCount);
            theta -= 2.0 * Pi * 55.0 / 500.0 * applied;
            phi += 2.0 * Pi * 30.0 / 500.0 * applied;
            return target + glm::vec3(
                float(radius * std::sin(phi) * std::sin(theta)),
                float(radius * std::cos(phi)),
                float(radius * std::sin(phi) * std::cos(theta)));
        }

        /** Generates Three's exact rotated 256-by-256 PlaneGeometry topology. */
        void buildRagingSeaPlane(
            eastl::vector<WebgpuTslRagingSeaVertex> &vertices,
            eastl::vector<uint> &indices)
        {
            constexpr uint32_t GridWidth = GridSegments + 1u;
            vertices.reserve(GridWidth * GridWidth);
            for (uint32_t y = 0u; y <= GridSegments; ++y)
            {
                for (uint32_t x = 0u; x <= GridSegments; ++x)
                {
                    const float positionX =
                        float(x) * (2.0f / float(GridSegments)) - 1.0f;
                    const float planeY =
                        1.0f - float(y) * (2.0f / float(GridSegments));
                    vertices.push_back({
                        .position = float3(positionX, 0.0f, -planeY),
                    });
                }
            }
            indices.reserve(GridSegments * GridSegments * 6u);
            for (uint32_t y = 0u; y < GridSegments; ++y)
            {
                for (uint32_t x = 0u; x < GridSegments; ++x)
                {
                    const uint32_t a = x + GridWidth * y;
                    const uint32_t b = x + GridWidth * (y + 1u);
                    const uint32_t c = x + 1u + GridWidth * (y + 1u);
                    const uint32_t d = x + 1u + GridWidth * y;
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);
                    indices.push_back(b);
                    indices.push_back(c);
                    indices.push_back(d);
                }
            }
        }

        /** Builds the exact DFG texels sampled by the two locked roughness states. */
        eastl::vector<uint> buildRagingSeaDfgLut()
        {
            constexpr RagingSeaDfgSelectedRow Rows[] = {
                {0u, 0x3a4d314cu, 0x391c33d2u, 0x34103979u, 0x325239f8u},
                {1u, 0x38ce364au, 0x385e3699u, 0x32c4396eu, 0x313439deu},
                {2u, 0x36c93897u, 0x367538a3u, 0x31863997u, 0x303839e2u},
                {3u, 0x34a339acu, 0x348039aeu, 0x306339e0u, 0x2eb539fcu},
                {4u, 0x321f3a76u, 0x32043a73u, 0x2eb63a34u, 0x2d313a26u},
                {5u, 0x2fca3b06u, 0x2fb83b00u, 0x2cec3a85u, 0x2bc53a5eu},
                {6u, 0x2cbb3b68u, 0x2cbb3b62u, 0x2ae33acfu, 0x29983a92u},
                {7u, 0x296f3ba8u, 0x297b3ba3u, 0x28953b0eu, 0x27b73ac2u},
                {8u, 0x25d33bd1u, 0x25f03bcdu, 0x25c43b3eu, 0x250f3aecu},
                {9u, 0x21b73be9u, 0x21e53be5u, 0x22cd3b62u, 0x22473b0fu},
                {10u, 0x1cfb3bf6u, 0x1d383bf3u, 0x1f793b7du, 0x1f4c3b2cu},
                {11u, 0x17593bfcu, 0x17e73bf9u, 0x1b843b91u, 0x1bd23b43u},
                {12u, 0x10393bffu, 0x10c83bfcu, 0x16c53b9fu, 0x179a3b54u},
                {13u, 0x066a3c00u, 0x081c3bfeu, 0x114d3ba9u, 0x127c3b63u},
                {14u, 0x00893c00u, 0x011d3bfeu, 0x0acd3bb1u, 0x0c973b6fu},
                {15u, 0x00013c00u, 0x00153bffu, 0x031c3bb7u, 0x047c3b79u},
            };
            eastl::vector<uint> pixels(256u, 0u);
            for (const RagingSeaDfgSelectedRow &row : Rows)
            {
                const size_t offset = size_t(row.row) * 16u;
                pixels[offset + 1u] = row.column1;
                pixels[offset + 2u] = row.column2;
                pixels[offset + 6u] = row.column6;
                pixels[offset + 7u] = row.column7;
            }
            return pixels;
        }
    } // namespace

    void WebgpuTslRagingSeaRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 120u;
        const bool settings =
            options.scenarioId == "nondefault-settings" &&
            options.targetFrame == 121u;
        const bool orbit =
            options.scenarioId == "orbit" && options.targetFrame == 122u;
        if (options.caseId != "webgpu_tsl_raging_sea" ||
            (!initial && !animated && !settings && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            ((settings || orbit) != !options.inputReplayPath.empty()))
            throw std::invalid_argument(
                "Raging-sea adapter requires one locked Manifest scenario.");
        device = inDevice;
        if (settings || orbit)
        {
            replaySha256 = calculateRagingSeaSha256(readRagingSeaReplay(
                std::filesystem::path(options.inputReplayPath.c_str())));
            const char *expected = settings
                ? SettingsReplaySha256
                : OrbitReplaySha256;
            if (replaySha256 != expected)
                throw std::runtime_error(
                    "The raging-sea input replay differs from the lock.");
        }
        buildRagingSeaPlane(vertices, indices);
        dfgLutPackedPixels = buildRagingSeaDfgLut();
        if (vertices.size() != 66049u || indices.size() != 393216u)
            throw std::runtime_error(
                "The generated raging-sea PlaneGeometry contract drifted.");

        const glm::vec3 camera = makeRagingSeaCameraPosition(orbit);
        const glm::vec3 target(0.0f, -0.25f, 0.0f);
        const glm::mat4 view = glm::lookAtRH(
            camera, target, glm::vec3(0.0f, 1.0f, 0.0f));
        uniforms.modelView = view;
        uniforms.modelViewProjection = makeRagingSeaProjection() * view;
        uniforms.cameraPositionAndTime = glm::vec4(
            camera,
            float(options.targetFrame) / 60.0f);
        const glm::vec4 lightView = view * glm::vec4(
            glm::normalize(glm::vec3(-4.0f, 2.0f, 0.0f)), 0.0f);
        uniforms.lightDirectionAndIntensity =
            glm::vec4(glm::vec3(lightView), 3.0f);
        const glm::vec3 baseColor = makeRagingSeaLinearColor(39u, 20u, 66u);
        uniforms.baseColorAndRoughness = glm::vec4(
            baseColor, settings ? 0.42f : 0.15f);
        const glm::vec3 emissiveColor = settings
            ? makeRagingSeaLinearColor(0u, 213u, 255u)
            : makeRagingSeaLinearColor(255u, 10u, 129u);
        uniforms.emissiveColorAndLow = glm::vec4(
            emissiveColor, settings ? -0.4f : -0.25f);
        uniforms.emissiveHighPowerShiftIterations = glm::vec4(
            settings ? 0.35f : 0.2f,
            settings ? 5.0f : 7.0f,
            0.01f,
            settings ? 5.0f : 3.0f);
        uniforms.largeFrequencySpeedMultiplier = glm::vec4(
            3.0f,
            1.0f,
            settings ? 2.1f : 1.25f,
            settings ? 0.23f : 0.15f);
        uniforms.smallFrequencySpeedMultiplier = glm::vec4(
            settings ? 3.7f : 2.0f,
            settings ? 0.55f : 0.3f,
            0.18f,
            0.0f);
        uniforms.viewportAndInspector = glm::vec4(
            800.0f, 500.0f, 1.0f, 0.0f);
    }

    void WebgpuTslRagingSeaRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuTslRagingSeaRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const bool settings = options.scenarioId == "nondefault-settings";
        const bool orbit = options.scenarioId == "orbit";
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareRagingSeaOutput(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error("Could not write raging-sea RGBA output.");
        }

        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\":1,\n  \"source\":\"gvm-three-r185\",\n"
            << "  \"caseId\":\"webgpu_tsl_raging_sea\",\n"
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
            << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
            << "  \"inputReplay\":";
        if (settings || orbit)
        {
            metadata
                << "{\"sha256\":\""
                << (settings ? SettingsReplaySha256 : OrbitReplaySha256)
                << "\",\"caseId\":\"webgpu_tsl_raging_sea\","
                << "\"scenarioId\":\"" << options.scenarioId.c_str() << "\","
                << "\"captureFrame\":" << frameIndex << ","
                << "\"eventCount\":" << (settings ? 1u : 3u) << ","
                << "\"target\":\"body > canvas\"},\n";
        }
        else
        {
            metadata << "null,\n";
        }
        metadata
            << "  \"renderSetCount\":0,\n"
            << "  \"entityCount\":1,\n"
            << "  \"instanceCount\":1,\n"
            << "  \"vertexCount\":66049,\n"
            << "  \"indexCount\":393216,\n"
            << "  \"scenePassCount\":1,\n"
            << "  \"screenPassCount\":1\n}\n";
        writeRagingSeaText(options.captureMetadataPath, metadata.str());

        std::ostringstream snapshot;
        snapshot
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"webgpu_tsl_raging_sea\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"renderSetPolicy\":\"not-required\",\n"
            << "  \"sceneRenderSetCount\":0,\n"
            << "  \"renderableObjectCount\":1,\n"
            << "  \"entityCount\":1,\n"
            << "  \"instanceCount\":1,\n"
            << "  \"instanceCounts\":[1],\n"
            << "  \"componentSchema\":[],\n"
            << "  \"drawCommandCount\":1,\n"
            << "  \"scenePassCount\":1,\n"
            << "  \"screenPassCount\":1,\n"
            << "  \"sampleCount\":1,\n"
            << "  \"msaaEnabled\":false,\n"
            << "  \"directDrawFallback\":false,\n"
            << "  \"explicitOrdinaryDraw\":true,\n"
            << "  \"scenePasses\":[{\"name\":\"main-raging-sea\","
            << "\"renderClass\":\"WebgpuTslRagingSeaMainPass\","
            << "\"sceneRoot\":\"scene\",\"renderSetBindingCount\":0,"
            << "\"usesStandaloneGeometry\":true,"
            << "\"usesExplicitDrawCount\":true}],\n"
            << "  \"screenPasses\":[\"single-sample-downsample\"],\n"
            << "  \"scenePassSequence\":["
            << "{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"main-raging-sea\"}]\n}\n";
        writeRagingSeaText(options.sceneSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebgpuTslRagingSeaRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        dfgLutPackedPixels.clear();
    }
} // namespace GVM::ThreeSamples
