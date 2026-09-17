#include "WebglGeometryTerrainRuntimeAdapter.hpp"

#include <EASTL/array.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t WorldWidth = 256u;
        constexpr uint32_t WorldDepth = 256u;
        constexpr double Pi = 3.14159265358979323846264338327950288;

        /** Returns one duplicated r185 ImprovedNoise permutation entry. */
        uint32_t terrainPermutation(uint32_t index)
        {
            static constexpr eastl::array<uint32_t, 256u> values = {
                151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
                140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
                247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
                57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
                74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
                60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
                65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,
                196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,
                217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,
                206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,
                248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,129,
                22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,
                246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
                81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
                184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
                222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
            };
            return values[index & 255u];
        }

        /** Evaluates the ImprovedNoise quintic interpolation weight. */
        double terrainFade(double value)
        {
            return value * value * value *
                   (value * (value * 6.0 - 15.0) + 10.0);
        }

        /** Evaluates one selected ImprovedNoise lattice gradient. */
        double terrainGradient(
            uint32_t hash,
            double x,
            double y,
            double z)
        {
            const uint32_t h = hash & 15u;
            const double u = h < 8u ? x : y;
            const double v = h < 4u
                ? y
                : ((h == 12u || h == 14u) ? x : z);
            return ((h & 1u) == 0u ? u : -u) +
                   ((h & 2u) == 0u ? v : -v);
        }

        /** Reproduces JavaScript MathUtils.lerp operation ordering. */
        double terrainLerp(double x, double y, double t)
        {
            return (1.0 - t) * x + t * y;
        }

        /** Evaluates one exact JavaScript-double ImprovedNoise sample. */
        double terrainImprovedNoise(double x, double y, double z)
        {
            const double floorX = std::floor(x);
            const double floorY = std::floor(y);
            const double floorZ = std::floor(z);
            const uint32_t X =
                uint32_t(int64_t(floorX) & 255ll);
            const uint32_t Y =
                uint32_t(int64_t(floorY) & 255ll);
            const uint32_t Z =
                uint32_t(int64_t(floorZ) & 255ll);
            x -= floorX;
            y -= floorY;
            z -= floorZ;
            const double u = terrainFade(x);
            const double v = terrainFade(y);
            const double w = terrainFade(z);
            const uint32_t A = terrainPermutation(X) + Y;
            const uint32_t AA = terrainPermutation(A) + Z;
            const uint32_t AB = terrainPermutation(A + 1u) + Z;
            const uint32_t B = terrainPermutation(X + 1u) + Y;
            const uint32_t BA = terrainPermutation(B) + Z;
            const uint32_t BB = terrainPermutation(B + 1u) + Z;
            return terrainLerp(
                terrainLerp(
                    terrainLerp(
                        terrainGradient(terrainPermutation(AA), x, y, z),
                        terrainGradient(terrainPermutation(BA), x - 1.0, y, z),
                        u),
                    terrainLerp(
                        terrainGradient(terrainPermutation(AB), x, y - 1.0, z),
                        terrainGradient(terrainPermutation(BB), x - 1.0, y - 1.0, z),
                        u),
                    v),
                terrainLerp(
                    terrainLerp(
                        terrainGradient(terrainPermutation(AA + 1u), x, y, z - 1.0),
                        terrainGradient(terrainPermutation(BA + 1u), x - 1.0, y, z - 1.0),
                        u),
                    terrainLerp(
                        terrainGradient(terrainPermutation(AB + 1u), x, y - 1.0, z - 1.0),
                        terrainGradient(terrainPermutation(BB + 1u), x - 1.0, y - 1.0, z - 1.0),
                        u),
                    v),
                w);
        }

        /** Builds the source's four-octave Uint8 heightfield exactly. */
        eastl::vector<uint32_t> buildTerrainHeights()
        {
            eastl::vector<uint32_t> result(
                WorldWidth * WorldDepth,
                0u);
            const double noiseZ =
                (std::sin(Pi / 4.0) * 10000.0 -
                 std::floor(std::sin(Pi / 4.0) * 10000.0)) *
                100.0;
            double quality = 1.0;
            for (uint32_t octave = 0u; octave < 4u; ++octave)
            {
                for (uint32_t index = 0u;
                     index < uint32_t(result.size());
                     ++index)
                {
                    const uint32_t x = index % WorldWidth;
                    const uint32_t y = index / WorldWidth;
                    const double contribution =
                        std::abs(
                            terrainImprovedNoise(
                                double(x) / quality,
                                double(y) / quality,
                                noiseZ) *
                            quality *
                            1.75);
                    result[index] =
                        uint32_t(
                            uint8_t(
                                double(result[index]) +
                                contribution));
                }
                quality *= 5.0;
            }
            return result;
        }

        /** Creates parent directories for one requested terrain artifact. */
        void prepareTerrainOutput(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }
    }

    void WebglGeometryTerrainRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" &&
            options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" &&
            options.targetFrame == 60u;
        const bool input =
            options.scenarioId == "first-person-input" &&
            options.targetFrame == 61u;
        if (options.caseId != "webgl_geometry_terrain" ||
            (!initial && !animated && !input) ||
            options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            (input != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "Terrain requires one locked Manifest scenario.");
        }
        device = inDevice;
        heights = buildTerrainHeights();
        grain.resize(1024u * 1024u);
        for (uint32_t index = 0u;
             index < uint32_t(grain.size());
             ++index)
        {
            const double seed =
                Pi / 4.0 + 1.0 + double(index);
            const double sineValue =
                std::sin(seed) * 10000.0;
            const double randomValue =
                sineValue - std::floor(sineValue);
            grain[index] =
                uint32_t(std::floor(randomValue * 5.0));
        }
        positions.reserve(WorldWidth * WorldDepth);
        textureCoordinates.reserve(WorldWidth * WorldDepth);
        for (uint32_t y = 0u; y < WorldDepth; ++y)
        {
            for (uint32_t x = 0u; x < WorldWidth; ++x)
            {
                const uint32_t index = y * WorldWidth + x;
                const float u = float(x) / float(WorldWidth - 1u);
                const float v = float(y) / float(WorldDepth - 1u);
                positions.push_back(glm::vec4(
                    (u - 0.5f) * 7500.0f,
                    float(heights[index]) * 10.0f,
                    (v - 0.5f) * 7500.0f,
                    1.0f));
                textureCoordinates.push_back(
                    glm::vec4(u, v, 0.0f, 0.0f));
            }
        }
        indices.reserve(
            (WorldWidth - 1u) *
            (WorldDepth - 1u) *
            6u);
        for (uint32_t y = 0u; y < WorldDepth - 1u; ++y)
        {
            for (uint32_t x = 0u; x < WorldWidth - 1u; ++x)
            {
                const uint32_t a = y * WorldWidth + x;
                const uint32_t b = (y + 1u) * WorldWidth + x;
                const uint32_t c = (y + 1u) * WorldWidth + x + 1u;
                const uint32_t d = y * WorldWidth + x + 1u;
                const uint32_t cell[6u] = {a, b, d, b, c, d};
                indices.insert(indices.end(), cell, cell + 6u);
            }
        }
        glm::vec3 cameraPosition(100.0f, 800.0f, -800.0f);
        glm::vec3 cameraTarget(-100.0f, 810.0f, -800.0f);
        if (input)
        {
            const glm::vec3 forward =
                glm::normalize(cameraTarget - cameraPosition);
            constexpr float dampedForwardDistance =
                131.5577505127159f;
            cameraPosition +=
                forward * dampedForwardDistance;
            cameraTarget +=
                forward * dampedForwardDistance;
        }
        modelView = glm::lookAtRH(
            cameraPosition,
            cameraTarget,
            glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection =
            glm::perspectiveRH_ZO(
                float(60.0 * Pi / 180.0),
                800.0f / 500.0f,
                1.0f,
                10000.0f);
        projection[1u][1u] *= -1.0f;
        modelViewProjection = projection * modelView;
    }

    void WebglGeometryTerrainRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryTerrainRuntimeAdapter::afterFrame(
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
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Terrain capture is too large.");
        }
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
            prepareTerrainOutput(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
        }
        if (!options.captureMetadataPath.empty())
        {
            prepareTerrainOutput(options.captureMetadataPath);
            std::ofstream output(
                options.captureMetadataPath.c_str(),
                std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_geometry_terrain\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\",\n"
                << "  \"inputReplay\":";
            if (options.scenarioId == "first-person-input")
            {
                output
                    << "{\"sha256\":\"af9698615a9a2b85d150cd85369b2219caae242c1c0e1c4885f13e2b0ad948b5\","
                    << "\"caseId\":\"webgl_geometry_terrain\","
                    << "\"scenarioId\":\"first-person-input\","
                    << "\"captureFrame\":61,\"eventCount\":2,"
                    << "\"target\":\"#container > canvas\"}";
            }
            else
            {
                output << "null";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareTerrainOutput(options.sceneSnapshotPath);
            std::ofstream output(
                options.sceneSnapshotPath.c_str(),
                std::ios::trunc);
            output
                << "{\n  \"caseId\":\"webgl_geometry_terrain\","
                << "\n  \"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\n  \"frame\":" << frameIndex
                << ",\n  \"implementationLevel\":\"semantic-complete\","
                << "\n  \"gpuWorkDslOnly\":true,"
                << "\n  \"renderSetPolicy\":\"not-required\","
                << "\n  \"sceneRenderSetCount\":0,"
                << "\n  \"renderableObjectCount\":1,"
                << "\n  \"instanceCount\":1,"
                << "\n  \"vertexCount\":65536,"
                << "\n  \"indexCount\":390150,"
                << "\n  \"scenePassCount\":1,"
                << "\n  \"computeDispatchThreads\":[1024,1024,1],"
                << "\n  \"generatedTexture\":\"rgba8unorm-1024x1024\","
                << "\n  \"drawCommandCount\":1\n}\n";
        }
        captureWritten = true;
    }

    void WebglGeometryTerrainRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        positions.clear();
        textureCoordinates.clear();
        indices.clear();
        heights.clear();
        grain.clear();
    }
}
