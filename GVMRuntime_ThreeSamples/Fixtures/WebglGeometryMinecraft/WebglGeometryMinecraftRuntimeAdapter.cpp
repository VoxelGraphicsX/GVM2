#include "WebglGeometryMinecraftRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t WorldWidth = 128u;
        constexpr uint32_t WorldDepth = 128u;
        constexpr uint32_t HeightValueCount = WorldWidth * WorldDepth;
        constexpr uint32_t ExpectedFaceCount = 23031u;
        constexpr uint32_t ExpectedVertexCount = ExpectedFaceCount * 4u;
        constexpr uint32_t ExpectedIndexCount = ExpectedFaceCount * 6u;
        constexpr uint32_t AtlasWidth = 16u;
        constexpr uint32_t AtlasHeight = 32u;
        constexpr uint32_t AtlasMipCount = 6u;
        constexpr uint32_t RandomSeed = 0x12345678u;
        constexpr uint32_t ImportRandomDrawCount = 76u;
        constexpr uint32_t CameraSceneRandomDrawCount = 8u;
        constexpr uint32_t PlaneGeometryRandomDrawCount = 20u;
        constexpr uint32_t MergeGeometryRandomDrawCount = 4u;
        constexpr uint32_t MeshRandomDrawCount = 16u;
        constexpr uint32_t LightRandomDrawCount = 16u;
        constexpr uint32_t RendererRandomDrawCount = 36u;
        constexpr uint32_t FirstRenderRandomDrawCount = 8u;
        constexpr uint32_t ExpectedFinalRandomState = 124295903u;
        constexpr double FrameStepSeconds = 1.0 / 60.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr char AtlasAssetSha256[] =
            "9e7a2ed78c02db3d54c2082534745797dde51f4b2b6bdcfed139dfe0a4abb7ef";
        constexpr char FirstPersonReplaySha256[] =
            "40998e57ee419db255557a0e32e35bb07321a1dfe2fe8d54859cbf68325246b1";

        static_assert(sizeof(WebglGeometryMinecraftVertex) == 20u);
        static_assert(offsetof(WebglGeometryMinecraftVertex, position) == 0u);
        static_assert(offsetof(WebglGeometryMinecraftVertex, texCoord) == 12u);
        static_assert(sizeof(WebglGeometryMinecraftUniforms) == 144u);

        /** Identifies one of the five immutable PlaneGeometry face templates. */
        enum class MinecraftFace : uint32_t
        {
            PositiveY,
            PositiveX,
            NegativeX,
            PositiveZ,
            NegativeZ,
        };

        /** Creates parent directories for one explicitly requested capture path. */
        void prepareMinecraftOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Reads one required bounded input without implicit path fallback. */
        eastl::vector<uint8_t> readMinecraftBytes(
            const std::filesystem::path &inputPath,
            const char *label)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open " + std::string(label) + ": " +
                    inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) >
                    std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    std::string(label) + " has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read complete " + std::string(label) + ".");
            }
            return bytes;
        }

        /** Returns one lowercase SHA-256 digest for a bounded byte range. */
        eastl::string calculateMinecraftSha256(
            const void *bytes,
            size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error(
                    "webgl_geometry_minecraft SHA-256 input is too large.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(
                bytes,
                static_cast<CC_LONG>(byteCount),
                digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Resolves the one explicit canonical replay without environment controls. */
        std::filesystem::path resolveMinecraftReplayPath(
            const ThreeSampleHostOptions &options)
        {
            const std::filesystem::path requested(
                options.inputReplayPath.c_str());
            if (requested.is_absolute() &&
                std::filesystem::is_regular_file(requested))
            {
                return requested;
            }
            if (!requested.empty() &&
                std::filesystem::is_regular_file(requested))
            {
                return std::filesystem::absolute(requested);
            }
            if (!options.assetRoot.empty())
            {
                const std::filesystem::path assetPath =
                    std::filesystem::path(options.assetRoot.c_str()) /
                    requested;
                if (std::filesystem::is_regular_file(assetPath))
                {
                    return assetPath;
                }
            }
            throw std::invalid_argument(
                "webgl_geometry_minecraft could not resolve its explicit "
                "--input-replay.");
        }

        /** Advances one deterministic random stream by an exact draw count. */
        void advanceMinecraftRandom(
            ThreeCompat::DeterministicRandom &random,
            uint32_t drawCount)
        {
            for (uint32_t draw = 0u; draw < drawCount; ++draw)
            {
                (void)random.nextUint32();
            }
        }

        /** Returns the fixed duplicated Perlin permutation entry at one index. */
        uint32_t minecraftPermutation(uint32_t index)
        {
            static constexpr eastl::array<uint32_t, 256u> Permutation = {
                151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53,
                194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99,
                37, 240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75,
                0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32,
                57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136,
                171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166,
                77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133,
                230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143,
                54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132,
                187, 208, 89, 18, 169, 200, 196, 135, 130, 116, 188,
                159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217,
                226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255,
                82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182,
                189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2,
                44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172,
                9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224,
                232, 178, 185, 112, 104, 218, 246, 97, 228, 251, 34,
                242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81,
                51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31,
                181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50,
                45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114,
                67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61,
                156, 180,
            };
            return Permutation[index & 255u];
        }

        /** Evaluates Three r185 ImprovedNoise's quintic interpolation weight. */
        double minecraftFade(double value)
        {
            return value * value * value *
                   (value * (value * 6.0 - 15.0) + 10.0);
        }

        /** Evaluates Three r185 ImprovedNoise's selected lattice gradient. */
        double minecraftGradient(
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

        /** Returns JavaScript MathUtils.lerp with the same operation ordering. */
        double minecraftLerp(double x, double y, double t)
        {
            return (1.0 - t) * x + t * y;
        }

        /** Evaluates one exact JavaScript-double ImprovedNoise sample. */
        double minecraftImprovedNoise(double x, double y, double z)
        {
            const double floorX = std::floor(x);
            const double floorY = std::floor(y);
            const double floorZ = std::floor(z);
            const uint32_t X = static_cast<uint32_t>(
                static_cast<int64_t>(floorX) & 255ll);
            const uint32_t Y = static_cast<uint32_t>(
                static_cast<int64_t>(floorY) & 255ll);
            const uint32_t Z = static_cast<uint32_t>(
                static_cast<int64_t>(floorZ) & 255ll);
            x -= floorX;
            y -= floorY;
            z -= floorZ;
            const double xMinus1 = x - 1.0;
            const double yMinus1 = y - 1.0;
            const double zMinus1 = z - 1.0;
            const double u = minecraftFade(x);
            const double v = minecraftFade(y);
            const double w = minecraftFade(z);
            const uint32_t A = minecraftPermutation(X) + Y;
            const uint32_t AA = minecraftPermutation(A) + Z;
            const uint32_t AB = minecraftPermutation(A + 1u) + Z;
            const uint32_t B = minecraftPermutation(X + 1u) + Y;
            const uint32_t BA = minecraftPermutation(B) + Z;
            const uint32_t BB = minecraftPermutation(B + 1u) + Z;
            return minecraftLerp(
                minecraftLerp(
                    minecraftLerp(
                        minecraftGradient(
                            minecraftPermutation(AA), x, y, z),
                        minecraftGradient(
                            minecraftPermutation(BA), xMinus1, y, z),
                        u),
                    minecraftLerp(
                        minecraftGradient(
                            minecraftPermutation(AB), x, yMinus1, z),
                        minecraftGradient(
                            minecraftPermutation(BB),
                            xMinus1,
                            yMinus1,
                            z),
                        u),
                    v),
                minecraftLerp(
                    minecraftLerp(
                        minecraftGradient(
                            minecraftPermutation(AA + 1u),
                            x,
                            y,
                            zMinus1),
                        minecraftGradient(
                            minecraftPermutation(BA + 1u),
                            xMinus1,
                            y,
                            zMinus1),
                        u),
                    minecraftLerp(
                        minecraftGradient(
                            minecraftPermutation(AB + 1u),
                            x,
                            yMinus1,
                            zMinus1),
                        minecraftGradient(
                            minecraftPermutation(BB + 1u),
                            xMinus1,
                            yMinus1,
                            zMinus1),
                        u),
                    v),
                w);
        }

        /** Builds the four-octave r185 height array from the single locked RNG draw. */
        eastl::vector<double> buildMinecraftHeightData(double noiseZ)
        {
            eastl::vector<double> data(HeightValueCount, 0.0);
            double quality = 2.0;
            for (uint32_t octave = 0u; octave < 4u; ++octave)
            {
                for (uint32_t index = 0u; index < HeightValueCount; ++index)
                {
                    const uint32_t x = index % WorldWidth;
                    const uint32_t y = index / WorldWidth;
                    data[index] += minecraftImprovedNoise(
                                       double(x) / quality,
                                       double(y) / quality,
                                       noiseZ) *
                                   quality;
                }
                quality *= 4.0;
            }
            return data;
        }

        /** Reproduces JavaScript array indexing followed by ToInt32 truncation. */
        int32_t minecraftHeightAt(
            const eastl::vector<double> &data,
            int32_t x,
            int32_t z)
        {
            const int64_t linearIndex =
                int64_t(x) + int64_t(z) * int64_t(WorldWidth);
            if (linearIndex < 0 ||
                linearIndex >= static_cast<int64_t>(data.size()))
            {
                return 0;
            }
            return static_cast<int32_t>(
                data[static_cast<size_t>(linearIndex)] * 0.15);
        }

        /** Appends one transformed r185 PlaneGeometry face in merge order. */
        void appendMinecraftFace(
            MinecraftFace face,
            float translationX,
            float translationY,
            float translationZ,
            eastl::vector<WebglGeometryMinecraftVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            static constexpr eastl::array<float, 4u> BaseX = {
                -50.0f, 50.0f, -50.0f, 50.0f};
            static constexpr eastl::array<float, 4u> BaseY = {
                50.0f, 50.0f, -50.0f, -50.0f};
            static constexpr eastl::array<float, 4u> BaseU = {
                0.0f, 1.0f, 0.0f, 1.0f};
            static constexpr eastl::array<uint32_t, 6u> PlaneIndices = {
                0u, 2u, 1u, 2u, 3u, 1u};
            const uint32_t firstVertex =
                static_cast<uint32_t>(vertices.size());
            for (uint32_t vertexIndex = 0u; vertexIndex < 4u; ++vertexIndex)
            {
                const float x = BaseX[vertexIndex];
                const float y = BaseY[vertexIndex];
                glm::vec3 position(0.0f);
                float v = vertexIndex < 2u ? 1.0f : 0.0f;
                if (face == MinecraftFace::PositiveY)
                {
                    position = glm::vec3(x, 50.0f, -y);
                    if (vertexIndex >= 2u)
                    {
                        v = 0.5f;
                    }
                }
                else if (face == MinecraftFace::PositiveX)
                {
                    position = glm::vec3(50.0f, y, -x);
                    if (vertexIndex < 2u)
                    {
                        v = 0.5f;
                    }
                }
                else if (face == MinecraftFace::NegativeX)
                {
                    position = glm::vec3(-50.0f, y, x);
                    if (vertexIndex < 2u)
                    {
                        v = 0.5f;
                    }
                }
                else if (face == MinecraftFace::PositiveZ)
                {
                    position = glm::vec3(x, y, 50.0f);
                    if (vertexIndex < 2u)
                    {
                        v = 0.5f;
                    }
                }
                else
                {
                    position = glm::vec3(-x, y, -50.0f);
                    if (vertexIndex < 2u)
                    {
                        v = 0.5f;
                    }
                }
                position += glm::vec3(
                    translationX,
                    translationY,
                    translationZ);
                vertices.push_back({
                    .position = float3(position.x, position.y, position.z),
                    .texCoord = float2(BaseU[vertexIndex], v),
                });
            }
            for (const uint32_t index : PlaneIndices)
            {
                indices.push_back(firstVertex + index);
            }
        }

        /** Merges every visible terrain face with the upstream boundary predicates. */
        uint32_t buildMinecraftGeometry(
            const eastl::vector<double> &data,
            eastl::vector<WebglGeometryMinecraftVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            vertices.reserve(ExpectedVertexCount);
            indices.reserve(ExpectedIndexCount);
            uint32_t faceCount = 0u;
            for (int32_t z = 0; z < static_cast<int32_t>(WorldDepth); ++z)
            {
                for (int32_t x = 0; x < static_cast<int32_t>(WorldWidth); ++x)
                {
                    const int32_t h = minecraftHeightAt(data, x, z);
                    const int32_t px = minecraftHeightAt(data, x + 1, z);
                    const int32_t nx = minecraftHeightAt(data, x - 1, z);
                    const int32_t pz = minecraftHeightAt(data, x, z + 1);
                    const int32_t nz = minecraftHeightAt(data, x, z - 1);
                    const float tx = float(x * 100 - 6400);
                    const float ty = float(h * 100);
                    const float tz = float(z * 100 - 6400);
                    appendMinecraftFace(
                        MinecraftFace::PositiveY,
                        tx,
                        ty,
                        tz,
                        vertices,
                        indices);
                    ++faceCount;
                    if ((px != h && px != h + 1) || x == 0)
                    {
                        appendMinecraftFace(
                            MinecraftFace::PositiveX,
                            tx,
                            ty,
                            tz,
                            vertices,
                            indices);
                        ++faceCount;
                    }
                    if ((nx != h && nx != h + 1) ||
                        x == static_cast<int32_t>(WorldWidth) - 1)
                    {
                        appendMinecraftFace(
                            MinecraftFace::NegativeX,
                            tx,
                            ty,
                            tz,
                            vertices,
                            indices);
                        ++faceCount;
                    }
                    if ((pz != h && pz != h + 1) ||
                        z == static_cast<int32_t>(WorldDepth) - 1)
                    {
                        appendMinecraftFace(
                            MinecraftFace::PositiveZ,
                            tx,
                            ty,
                            tz,
                            vertices,
                            indices);
                        ++faceCount;
                    }
                    if ((nz != h && nz != h + 1) || z == 0)
                    {
                        appendMinecraftFace(
                            MinecraftFace::NegativeZ,
                            tx,
                            ty,
                            tz,
                            vertices,
                            indices);
                        ++faceCount;
                    }
                }
            }
            if (faceCount != ExpectedFaceCount ||
                vertices.size() != ExpectedVertexCount ||
                indices.size() != ExpectedIndexCount)
            {
                throw std::runtime_error(
                    "Minecraft terrain merge diverged from the r185 face counts.");
            }
            return faceCount;
        }

        /** Builds Three's OpenGL perspective matrix before shader clip conversion. */
        glm::mat4 makeMinecraftProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 60.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 20000.0;
            constexpr double Pi = 3.14159265358979323846;
            const double top =
                NearDistance *
                std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                double(width) / double(height) * projectionHeight;
            const double projectionDepth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(
                2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(
                2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(
                -(FarDistance + NearDistance) / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -2.0 * FarDistance * NearDistance / projectionDepth);
            return projection;
        }

        /** Returns the byte count for one tightly packed RGBA8 readback. */
        uint64_t minecraftRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_geometry_minecraft RGBA8 byte count overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns the deterministic virtual Timer value after one rendered frame index. */
        double minecraftVirtualTimeMilliseconds(uint32_t frameIndex)
        {
            double virtualTimeMilliseconds = 0.0;
            for (uint32_t frame = 0u; frame < frameIndex; ++frame)
            {
                virtualTimeMilliseconds += FrameStepMilliseconds;
            }
            return virtualTimeMilliseconds;
        }

        /** Returns the virtual Timer delta after sequential floating-point accumulation. */
        double minecraftTimerDeltaSeconds(uint32_t frameIndex)
        {
            if (frameIndex == 0u)
            {
                return 0.0;
            }
            return (minecraftVirtualTimeMilliseconds(frameIndex) -
                    minecraftVirtualTimeMilliseconds(frameIndex - 1u)) /
                   1000.0;
        }
    } // namespace

    void WebglGeometryMinecraftRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_geometry_minecraft")
        {
            throw std::invalid_argument(
                "Minecraft adapter requires case-id webgl_geometry_minecraft.");
        }
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 60u;
        const bool firstPerson =
            options.scenarioId == "first-person-input" &&
            options.targetFrame == 61u;
        if (!initial && !animated && !firstPerson)
        {
            throw std::invalid_argument(
                "webgl_geometry_minecraft requires initial/frame 0, "
                "animated/frame 60, or first-person-input/frame 61.");
        }
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument(
                "webgl_geometry_minecraft requires the locked 800x500 extent.");
        }
        if (options.randomSeed != RandomSeed)
        {
            throw std::invalid_argument(
                "webgl_geometry_minecraft requires seed 0x12345678.");
        }
        if (options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "webgl_geometry_minecraft requires explicit --asset-root.");
        }
        if (!firstPerson && !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Non-interactive Minecraft scenarios must not consume replay input.");
        }
        if (firstPerson && options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "first-person-input requires the canonical --input-replay.");
        }

        device = inDevice;
        scenarioState = MinecraftScenarioState{};
        scenarioState.usesInputReplay = firstPerson;
        if (firstPerson)
        {
            const eastl::vector<uint8_t> replayBytes = readMinecraftBytes(
                resolveMinecraftReplayPath(options),
                "Minecraft FirstPersonControls replay");
            scenarioState.replaySha256 = calculateMinecraftSha256(
                replayBytes.data(),
                replayBytes.size());
            if (scenarioState.replaySha256 != FirstPersonReplaySha256)
            {
                throw std::invalid_argument(
                    "Minecraft replay SHA-256 differs from the locked keyboard sequence.");
            }
            scenarioState.replayTarget = "#container > canvas";
            scenarioState.replayEventCount = 4u;
            scenarioState.replayLastEventFrame = 31u;
        }

        ThreeCompat::DeterministicRandom random(RandomSeed);
        advanceMinecraftRandom(random, ImportRandomDrawCount);
        const double noiseZ = double(random.nextFloat()) * 100.0;
        heightData = buildMinecraftHeightData(noiseZ);
        scenarioState.cameraPosition.y =
            double(minecraftHeightAt(heightData, 64, 64) * 100 + 100);
        terrainFaceCount = buildMinecraftGeometry(
            heightData,
            vertices,
            indices);

        advanceMinecraftRandom(
            random,
            CameraSceneRandomDrawCount +
                PlaneGeometryRandomDrawCount +
                terrainFaceCount * 4u);
        advanceMinecraftRandom(random, MergeGeometryRandomDrawCount);
        advanceMinecraftRandom(random, MeshRandomDrawCount);
        advanceMinecraftRandom(random, LightRandomDrawCount);
        advanceMinecraftRandom(random, RendererRandomDrawCount);
        advanceMinecraftRandom(random, FirstRenderRandomDrawCount);
        finalRandomState = random.getState();
        if (finalRandomState != ExpectedFinalRandomState)
        {
            throw std::runtime_error(
                "Minecraft RNG segmentation diverged from the true r185 probe.");
        }

        const std::filesystem::path atlasPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "minecraft" / "atlas.png";
        const eastl::vector<uint8_t> sourceAtlasBytes =
            readMinecraftBytes(atlasPath, "Three r185 Minecraft atlas");
        atlasSha256 = calculateMinecraftSha256(
            sourceAtlasBytes.data(),
            sourceAtlasBytes.size());
        if (atlasSha256 != AtlasAssetSha256)
        {
            throw std::invalid_argument(
                "textures/minecraft/atlas.png differs from the pinned r185 asset.");
        }
        const RgbaImageData atlas = decodePngRgba8(atlasPath);
        if (atlas.width != AtlasWidth || atlas.height != AtlasHeight)
        {
            throw std::runtime_error(
                "Minecraft atlas must decode to the locked 16x32 extent.");
        }
        const eastl::vector<RgbaImageData> mipChain =
            buildSrgbMipChain(atlas);
        if (mipChain.size() != AtlasMipCount)
        {
            throw std::runtime_error(
                "Minecraft atlas did not produce the complete six-level mip chain.");
        }
        atlasBytes.clear();
        atlasMipOffsets.clear();
        for (const RgbaImageData &mip : mipChain)
        {
            atlasMipOffsets.push_back(atlasBytes.size());
            atlasBytes.insert(
                atlasBytes.end(),
                mip.pixels.begin(),
                mip.pixels.end());
        }

        eastl::vector<uint8_t> geometryBytes;
        geometryBytes.resize(
            vertices.size() * sizeof(WebglGeometryMinecraftVertex) +
            indices.size() * sizeof(uint32_t));
        std::memcpy(
            geometryBytes.data(),
            vertices.data(),
            vertices.size() * sizeof(WebglGeometryMinecraftVertex));
        std::memcpy(
            geometryBytes.data() +
                vertices.size() * sizeof(WebglGeometryMinecraftVertex),
            indices.data(),
            indices.size() * sizeof(uint32_t));
        geometrySha256 = calculateMinecraftSha256(
            geometryBytes.data(),
            geometryBytes.size());
        projectionMatrix = makeMinecraftProjection(options.width, options.height);
    }

    WebglGeometryMinecraftUniforms
    WebglGeometryMinecraftRuntimeAdapter::makeUniforms() const
    {
        WebglGeometryMinecraftUniforms uniforms;
        uniforms.projectionMatrix = projectionMatrix;
        const glm::mat4 view = glm::translate(
            glm::mat4(1.0f),
            -glm::vec3(scenarioState.cameraPosition));
        uniforms.viewMatrix = view;
        return uniforms;
    }

    void WebglGeometryMinecraftRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        if (frameIndex != processedFrameCount)
        {
            throw std::runtime_error(
                "Minecraft host frames must advance sequentially from frame zero.");
        }
        if (frameIndex > options.targetFrame || !frameUploader)
        {
            throw std::runtime_error(
                "Minecraft frame update exceeded its locked scenario contract.");
        }
        if (scenarioState.usesInputReplay)
        {
            if (frameIndex == 1u)
            {
                scenarioState.moveForward = true;
                scenarioState.moveRight = true;
            }
            if (frameIndex == 31u)
            {
                scenarioState.moveForward = false;
                scenarioState.moveRight = false;
            }
        }
        const glm::dvec3 targetVelocity(
            scenarioState.moveRight ? 1000.0 : 0.0,
            0.0,
            scenarioState.moveForward ? -1000.0 : 0.0);
        scenarioState.velocity +=
            (targetVelocity - scenarioState.velocity) * 0.1;
        const double deltaSeconds =
            frameIndex == 0u ? 0.0 : FrameStepSeconds;
        scenarioState.cameraPosition +=
            scenarioState.velocity * deltaSeconds;
        frameUploader(makeUniforms());
        ++processedFrameCount;
    }

    void WebglGeometryMinecraftRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = minecraftRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Minecraft capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_geometry_minecraft could not access its graphics queue.");
        }
        graphicsQueue
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(
            options,
            frameIndex,
            width,
            height,
            byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglGeometryMinecraftRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        frameUploader = nullptr;
    }

    void WebglGeometryMinecraftRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureRgbaPath.c_str());
        prepareMinecraftOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_geometry_minecraft RGBA output.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_geometry_minecraft RGBA output.");
        }
    }

    void WebglGeometryMinecraftRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureMetadataPath.c_str());
        prepareMinecraftOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_geometry_minecraft metadata output.");
        }
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_geometry_minecraft\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str()
               << "\",\n"
               << "  \"backend\": \""
               << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"randomState\": " << finalRandomState << ",\n"
               << "  \"randomDrawCount\": 92309,\n"
               << "  \"warmupRenderCount\": 2,\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"virtualTimeMs\": "
               << minecraftVirtualTimeMilliseconds(frameIndex) << ",\n"
               << "  \"timerDeltaSeconds\": "
               << minecraftTimerDeltaSeconds(frameIndex) << ",\n"
               << "  \"cameraPosition\": ["
               << scenarioState.cameraPosition.x << ", "
               << scenarioState.cameraPosition.y << ", "
               << scenarioState.cameraPosition.z << "],\n"
               << "  \"cameraQuaternion\": [0, 0, 0, 1],\n"
               << "  \"controlsVelocity\": ["
               << scenarioState.velocity.x << ", "
               << scenarioState.velocity.y << ", "
               << scenarioState.velocity.z << "],\n"
               << "  \"atlasSha256\": \"" << atlasSha256.c_str()
               << "\",\n"
               << "  \"atlasWidth\": 16,\n"
               << "  \"atlasHeight\": 32,\n"
               << "  \"atlasColorSpace\": \"srgb\",\n"
               << "  \"atlasMagFilter\": \"nearest\",\n"
               << "  \"atlasMinFilter\": \"linear-mipmap-linear\",\n"
               << "  \"atlasMipCount\": 6,\n"
               << "  \"geometrySha256\": \""
               << geometrySha256.c_str() << "\",\n"
               << "  \"terrainFaceCount\": " << terrainFaceCount << ",\n"
               << "  \"inputReplay\": ";
        if (scenarioState.usesInputReplay)
        {
            output << "{\"schemaVersion\":1,"
                   << "\"caseId\":\"webgl_geometry_minecraft\","
                   << "\"scenarioId\":\"first-person-input\","
                   << "\"captureFrame\":61,"
                   << "\"sha256\":\""
                   << scenarioState.replaySha256.c_str() << "\","
                   << "\"target\":\""
                   << scenarioState.replayTarget.c_str() << "\","
                   << "\"eventCount\":"
                   << scenarioState.replayEventCount << ","
                   << "\"lastEventFrame\":"
                   << scenarioState.replayLastEventFrame << "},\n";
        }
        else
        {
            output << "null,\n";
        }
        output << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u
               << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }

    void WebglGeometryMinecraftRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.sceneSnapshotPath.c_str());
        prepareMinecraftOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_geometry_minecraft snapshot output.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_geometry_minecraft\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"upstreamCommit\": "
                  "\"2431a09f46f34c560bc8e44b33be0e567723d5b9\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsInstancing\": false,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 1,\n"
               << "  \"terrainFaceCount\": " << terrainFaceCount << ",\n"
               << "  \"vertexCount\": " << vertices.size() << ",\n"
               << "  \"indexCount\": " << indices.size() << ",\n"
               << "  \"triangleCount\": " << indices.size() / 3u << ",\n"
               << "  \"indexed\": true,\n"
               << "  \"textureCount\": 1,\n"
               << "  \"textureMipCount\": 6,\n"
               << "  \"warmupRenderCount\": 2,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"scenePassSequence\": "
                  "[{\"sceneRoot\":\"scene\","
                  "\"scenePass\":\"main-atlas-lambert\"}],\n"
               << "  \"screenPasses\": "
                  "[],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
