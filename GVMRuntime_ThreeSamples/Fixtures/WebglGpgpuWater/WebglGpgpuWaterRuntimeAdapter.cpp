#include "WebglGpgpuWaterRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t WaterTextureWidth = 128u;
        constexpr uint32_t WaterVertexCount = WaterTextureWidth * WaterTextureWidth;
        constexpr uint32_t WaterIndexCount = 127u * 127u * 6u;
        constexpr uint32_t WaterEntityCount = 14u;
        constexpr uint32_t DuckCount = 12u;
        constexpr uint32_t WaterRandomSeed = 0x12345678u;
        constexpr uint32_t PreSimplexRandomDrawCount = 84u;
        constexpr uint32_t SimplexRandomDrawCount = 256u;
        constexpr uint32_t PreDuckRandomDrawCount = 200u;
        constexpr uint32_t DuckCloneRandomDrawCount = 12u;
        constexpr uint32_t FirstRenderInternalRandomDrawCount = 152u;
        constexpr uint32_t TotalRandomDrawCount = 848u;
        constexpr uint32_t ExpectedFinalRandomState = 0x86bc9d95u;
        constexpr uint32_t ExpectedInteractiveRandomState = 246033143u;
        constexpr float WaterBounds = 6.0f;
        constexpr float WaterBoundsHalf = WaterBounds * 0.5f;
        constexpr double Pi = 3.14159265358979323846;
        constexpr char InitialHeightSha256[] =
            "089b8943b76c5d5433b45755c53b20e6202ce4975a8684f510c89c85db1cc676";
        constexpr char DuckMeshPackSha256[] =
            "c22f69cfe259aab38ec3f5dfa1c3c94f9dd0c4dc2c47bc49bde16df81dc5916a";
        constexpr char DuckTextureSha256[] =
            "d9b75fd8dc15a4b46d5a1b1f9cd9a31a1844fce9cdb24a9de6e61f27d5170124";
        constexpr char DuckSourceGlbSha256[] =
            "76c62e63a0aec09cd66f2e2c9452a6dcee428f2464dd2c6d845958a8f7f7cdd1";
        constexpr char DuckDecoderWasmSha256[] =
            "a680d927bed9cb864ddbd63521868891af2bfbe755092761b4837487618df8ac";
        constexpr char DuckDecoderWrapperSha256[] =
            "8bb2952d2ba7d67e1414f8df819410cb0434a666be53f671fff75f68843d76f6";
        constexpr char EnvironmentSourceSha256[] =
            "1cb809a131ff3cb7df94b639d5967fc6fc08ecd25b6c43930af3b5d99f1d8855";
        constexpr char PointerReplaySha256[] =
            "625efac0d15ec954b80b30de13739cac5bc5b4bef9cc314197208059860f2bb1";
        constexpr char PointerStateScriptSha256[] =
            "9d7e42b1dde39331e674a4d09e19e0bf69c5a0e72747bf3f694d4264e3cc4a93";
        constexpr char PointerReplayTarget[] =
            "body > div:nth-of-type(2) > canvas";
        constexpr uint32_t EnvironmentWidth = 1024u;
        constexpr uint32_t EnvironmentHeight = 512u;
        constexpr char DuckMeshPackMagic[] = "GVMWDUK1";
        constexpr uint32_t DuckMeshPackVersion = 1u;
        constexpr uint32_t DuckMeshPackHeaderSize = 256u;
        constexpr uint32_t DuckVertexCount = 2277u;
        constexpr uint32_t DuckIndexCount = 12636u;
        constexpr uint32_t DuckPackedVertexStride = 32u;
        constexpr uint8_t WhiteSceneTexture[] = {255u, 255u, 255u, 255u};
        constexpr uint32_t R185DfgLutPackedPixels[] = {
            0x3ad130b5u, 0x3a4d314cu, 0x391c33d2u, 0x382835efu, 0x36a637f3u, 0x353938d1u, 0x34103979u, 0x325239f8u,
            0x30f03a53u, 0x2fc93a94u, 0x2e353abfu, 0x2d053adau, 0x2c1f3ae8u, 0x2ae03aedu, 0x29d13aeau, 0x28ff3ae1u,
            0x38e43638u, 0x38ce364au, 0x385e3699u, 0x372c374eu, 0x35a43839u, 0x346238dcu, 0x32c4396eu, 0x313439deu,
            0x30033a2bu, 0x2e3a3a59u, 0x2ce13a6du, 0x2bba3a6eu, 0x2a333a5fu, 0x290a3a49u, 0x28263a2du, 0x26e83a0au,
            0x36d73894u, 0x36c93897u, 0x367538a3u, 0x35ac38bcu, 0x349c38eeu, 0x3332393eu, 0x31863997u, 0x303839e2u,
            0x2e753a13u, 0x2cf53a29u, 0x2bac3a2du, 0x29ff3a21u, 0x28bc3a04u, 0x279039dcu, 0x261a39adu, 0x24fa3978u,
            0x34a839acu, 0x34a339acu, 0x348039aeu, 0x342339aeu, 0x330e39b1u, 0x31a939c2u, 0x306339e0u, 0x2eb539fcu,
            0x2d1d3a0cu, 0x2bcf3a14u, 0x29ff3a07u, 0x28a339e9u, 0x273c39beu, 0x25b33989u, 0x2488394au, 0x23453907u,
            0x32233a77u, 0x321f3a76u, 0x32043a73u, 0x31b33a6au, 0x31143a58u, 0x303b3a45u, 0x2eb63a34u, 0x2d313a26u,
            0x2bef3a1eu, 0x2a0d3a0bu, 0x28a139ecu, 0x271b39c0u, 0x25803987u, 0x24493944u, 0x22bd38fau, 0x215538acu,
            0x2fca3b07u, 0x2fca3b06u, 0x2fb83b00u, 0x2f7c3af4u, 0x2eea3adbu, 0x2e003ab4u, 0x2cec3a85u, 0x2bc53a5eu,
            0x2a003a36u, 0x28993a0du, 0x270739dcu, 0x256239a0u, 0x2424395au, 0x2268390bu, 0x20fd38b7u, 0x1fd1385fu,
            0x2cb93b69u, 0x2cbb3b68u, 0x2cbb3b62u, 0x2cae3b56u, 0x2c783b3bu, 0x2c0a3b0du, 0x2ae33acfu, 0x29983a92u,
            0x28673a54u, 0x26d03a17u, 0x253c39d3u, 0x24023989u, 0x22263935u, 0x20bd38dcu, 0x1f54387du, 0x1db3381du,
            0x296b3ba9u, 0x296f3ba8u, 0x297b3ba3u, 0x29873b98u, 0x29763b7fu, 0x29273b4eu, 0x28953b0eu, 0x27b73ac2u,
            0x263b3a73u, 0x24e73a23u, 0x239b39d0u, 0x21d93976u, 0x207e3917u, 0x1ee738b2u, 0x1d53384bu, 0x1c1e37c7u,
            0x25cb3bd2u, 0x25d33bd1u, 0x25f03bcdu, 0x261f3bc2u, 0x26453badu, 0x262d3b7du, 0x25c43b3eu, 0x250f3aecu,
            0x243a3a93u, 0x22ce3a32u, 0x215b39d0u, 0x202a3969u, 0x1e6e38feu, 0x1cf1388fu, 0x1b9b381fu, 0x19dd3762u,
            0x21ab3be9u, 0x21b73be9u, 0x21e53be5u, 0x22413bddu, 0x22a73bc9u, 0x22ec3ba0u, 0x22cd3b62u, 0x22473b0fu,
            0x21753aaeu, 0x20883a44u, 0x1f4939d4u, 0x1dbe3960u, 0x1c7738e9u, 0x1ae83870u, 0x195337f1u, 0x181b3708u,
            0x1cea3bf6u, 0x1cfb3bf6u, 0x1d383bf3u, 0x1dbd3becu, 0x1e7c3bdau, 0x1f253bb7u, 0x1f793b7du, 0x1f4c3b2cu,
            0x1ea63ac6u, 0x1dbb3a55u, 0x1cbd39dau, 0x1b9d395au, 0x1a0038d8u, 0x18ac3855u, 0x173c37abu, 0x159836b7u,
            0x17363bfcu, 0x17593bfcu, 0x17e73bf9u, 0x18963bf4u, 0x19973be4u, 0x1aa83bc6u, 0x1b843b91u, 0x1bd23b43u,
            0x1b8a3adeu, 0x1acd3a65u, 0x19d339e2u, 0x18cd3957u, 0x17b338cau, 0x1613383eu, 0x14bf376du, 0x135e366fu,
            0x101b3bffu, 0x10393bffu, 0x10c83bfcu, 0x12263bf9u, 0x14283beau, 0x15843bcfu, 0x16c53b9fu, 0x179a3b54u,
            0x17ce3af0u, 0x17713a76u, 0x16a439eau, 0x15a73956u, 0x14a738bfu, 0x13793829u, 0x11ea3735u, 0x10a1362du,
            0x061b3c00u, 0x066a3c00u, 0x081c3bfeu, 0x0a4c3bfau, 0x0d163bedu, 0x0fb33bd5u, 0x114d3ba9u, 0x127c3b63u,
            0x132f3b01u, 0x13443a85u, 0x12d239f4u, 0x120d3957u, 0x112238b5u, 0x103c3817u, 0x0ed33703u, 0x0d6d35f0u,
            0x007a3c00u, 0x00893c00u, 0x011d3bfeu, 0x027c3bfbu, 0x04fa3bf0u, 0x08813bdau, 0x0acd3bb1u, 0x0c973b6fu,
            0x0d7b3b10u, 0x0df13a93u, 0x0def39feu, 0x0d8a3959u, 0x0ce938afu, 0x0c313808u, 0x0af036d5u, 0x09a335b9u,
            0x00003c00u, 0x00013c00u, 0x00153bffu, 0x00593bfbu, 0x00fd3bf2u, 0x01df3bddu, 0x031c3bb7u, 0x047c3b79u,
            0x05d43b1du, 0x06d53aa0u, 0x075a3a08u, 0x075e395du, 0x06f738aau, 0x064837f4u, 0x057636acu, 0x049f3586u,
        };
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        constexpr int32_t SimplexGradients[12u][3u] = {
            {1, 1, 0},
            {-1, 1, 0},
            {1, -1, 0},
            {-1, -1, 0},
            {1, 0, 1},
            {-1, 0, 1},
            {1, 0, -1},
            {-1, 0, -1},
            {0, 1, 1},
            {0, -1, 1},
            {0, 1, -1},
            {0, -1, -1},
        };

        static_assert(sizeof(float4) == 16u);
        static_assert(sizeof(WebglGpgpuWaterHeightUniforms) == 32u);
        static_assert(sizeof(WebglGpgpuWaterScreenUniforms) == 64u);
        static_assert(sizeof(GpgpuWaterHostFloat4) == 16u);
        static_assert(sizeof(GpgpuWaterHostUint4) == 16u);
        static_assert(sizeof(GpgpuWaterHostVertex) == 48u);
        static_assert(sizeof(GpgpuWaterHostObjectData) == 208u);
        static_assert(sizeof(GpgpuWaterHostInstanceData) == 16u);
        static_assert(sizeof(GpgpuWaterHostMaterialData) == 32u);
        static_assert(sizeof(GpgpuWaterHostRenderFlags) == 16u);
        static_assert(
            sizeof(R185DfgLutPackedPixels) /
                    sizeof(R185DfgLutPackedPixels[0u]) ==
                256u);
        static_assert(WaterEntityCount == 2u + DuckCount);
        static_assert(TotalRandomDrawCount ==
                      PreSimplexRandomDrawCount +
                          SimplexRandomDrawCount +
                          PreDuckRandomDrawCount +
                          11u * DuckCloneRandomDrawCount +
                          DuckCount * 2u +
                          FirstRenderInternalRandomDrawCount);

        /** Reproduces the exact r185 two-dimensional simplex permutation and noise function. */
        class GpgpuWaterSimplexNoise2D final
        {
        public:
            /** Consumes the same 256 JavaScript random values as the r185 constructor. */
            explicit GpgpuWaterSimplexNoise2D(
                ThreeCompat::DeterministicRandom &random)
            {
                eastl::array<uint32_t, 256u> base = {};
                for (uint32_t index = 0u; index < base.size(); ++index)
                {
                    base[index] = static_cast<uint32_t>(
                        std::floor(nextRandomUnit(random) * 256.0));
                }
                for (uint32_t index = 0u; index < permutation.size(); ++index)
                {
                    permutation[index] = base[index & 255u];
                }
            }

            /** Returns the exact double-precision 2D simplex value used by fillTexture. */
            double noise(double inputX, double inputY) const
            {
                constexpr double F2 = 0.36602540378443864676372317075294;
                constexpr double G2 = 0.21132486540518711774542560974902;
                const double skew = (inputX + inputY) * F2;
                const int32_t latticeX =
                    static_cast<int32_t>(std::floor(inputX + skew));
                const int32_t latticeY =
                    static_cast<int32_t>(std::floor(inputY + skew));
                const double unskew = double(latticeX + latticeY) * G2;
                const double x0 = inputX - (double(latticeX) - unskew);
                const double y0 = inputY - (double(latticeY) - unskew);
                const int32_t cornerX = x0 > y0 ? 1 : 0;
                const int32_t cornerY = x0 > y0 ? 0 : 1;
                const double x1 = x0 - double(cornerX) + G2;
                const double y1 = y0 - double(cornerY) + G2;
                const double x2 = x0 - 1.0 + 2.0 * G2;
                const double y2 = y0 - 1.0 + 2.0 * G2;
                const uint32_t wrappedX =
                    static_cast<uint32_t>(latticeX) & 255u;
                const uint32_t wrappedY =
                    static_cast<uint32_t>(latticeY) & 255u;
                const uint32_t gradient0 =
                    permutation[wrappedX + permutation[wrappedY]] % 12u;
                const uint32_t gradient1 =
                    permutation[
                        wrappedX + static_cast<uint32_t>(cornerX) +
                        permutation[
                            wrappedY + static_cast<uint32_t>(cornerY)]] %
                    12u;
                const uint32_t gradient2 =
                    permutation[
                        wrappedX + 1u + permutation[wrappedY + 1u]] %
                    12u;
                return 70.0 *
                       (cornerContribution(gradient0, x0, y0) +
                        cornerContribution(gradient1, x1, y1) +
                        cornerContribution(gradient2, x2, y2));
            }

        private:
            /** Returns one upper-24-bit xorshift value as a JavaScript unit double. */
            static double nextRandomUnit(
                ThreeCompat::DeterministicRandom &random)
            {
                return double(random.nextUint32() >> 8u) / 16777216.0;
            }

            /** Evaluates one attenuated simplex corner with the frozen gradient table. */
            static double cornerContribution(
                uint32_t gradientIndex,
                double x,
                double y)
            {
                double attenuation = 0.5 - x * x - y * y;
                if (attenuation < 0.0)
                {
                    return 0.0;
                }
                attenuation *= attenuation;
                const double gradientDot =
                    double(SimplexGradients[gradientIndex][0u]) * x +
                    double(SimplexGradients[gradientIndex][1u]) * y;
                return attenuation * attenuation * gradientDot;
            }

            eastl::array<uint32_t, 512u> permutation = {};
        };

        /** Creates parent directories for one explicitly requested output artifact. */
        void prepareWaterOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Returns one upper-24-bit xorshift value as the reference JavaScript unit double. */
        double nextWaterRandomUnit(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte range. */
        eastl::string calculateWaterSha256(const void *bytes, size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error(
                    "webgl_gpgpu_water SHA-256 input is too large.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes, static_cast<CC_LONG>(byteCount), digest.data());
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

        /** Returns the exact r185 quaternion rotating one normalized direction onto another. */
        glm::dquat makeWaterUnitVectorQuaternion(
            const glm::dvec3 &from,
            const glm::dvec3 &to)
        {
            double scalar = glm::dot(from, to) + 1.0;
            glm::dquat result;
            if (scalar < 1e-8)
            {
                scalar = 0.0;
                if (std::abs(from.x) > std::abs(from.z))
                {
                    result = glm::dquat(
                        scalar,
                        -from.y,
                        from.x,
                        0.0);
                }
                else
                {
                    result = glm::dquat(
                        scalar,
                        0.0,
                        -from.z,
                        from.y);
                }
            }
            else
            {
                const glm::dvec3 vector = glm::cross(from, to);
                result = glm::dquat(
                    scalar,
                    vector.x,
                    vector.y,
                    vector.z);
            }
            return glm::normalize(result);
        }

        /** Applies Three r185 Quaternion.slerp semantics without reducing state to float. */
        glm::dquat slerpWaterQuaternion(
            const glm::dquat &source,
            const glm::dquat &target,
            double interpolation)
        {
            glm::dquat adjustedTarget = target;
            double dotProduct = glm::dot(source, target);
            if (dotProduct < 0.0)
            {
                adjustedTarget = -target;
                dotProduct = -dotProduct;
            }
            double sourceWeight = 1.0 - interpolation;
            double targetWeight = interpolation;
            if (dotProduct < 0.9995)
            {
                const double theta = std::acos(dotProduct);
                const double sine = std::sin(theta);
                sourceWeight = std::sin(sourceWeight * theta) / sine;
                targetWeight = std::sin(targetWeight * theta) / sine;
                return source * sourceWeight + adjustedTarget * targetWeight;
            }
            return glm::normalize(
                source * sourceWeight + adjustedTarget * targetWeight);
        }

        /** Resolves the replayed canvas pointer to the fixed horizontal water plane. */
        glm::vec2 makeWaterPointerPosition(uint32_t frameIndex)
        {
            if (frameIndex < 10u || frameIndex >= 30u)
            {
                return glm::vec2(10000.0f);
            }
            if (frameIndex < 20u)
            {
                return glm::vec2(0.0f);
            }
            constexpr double CameraHeight = 2.0;
            constexpr double CameraForwardY = 0.44721359549995793928;
            const double horizontalSlope =
                std::tan(75.0 * Pi / 360.0) * 1.6 * 0.25;
            return glm::vec2(
                static_cast<float>(
                    horizontalSlope * CameraHeight / CameraForwardY),
                0.0f);
        }

        /** Reads one required immutable water asset into bounded binary storage. */
        eastl::vector<uint8_t> readWaterBinaryAsset(
            const std::filesystem::path &assetPath,
            const char *assetLabel)
        {
            std::ifstream input(assetPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                const eastl::string message =
                    eastl::string("Could not open ") + assetLabel +
                    " asset: " + assetPath.string().c_str();
                throw std::runtime_error(message.c_str());
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                static_cast<uint64_t>(byteCount) >
                    std::numeric_limits<size_t>::max())
            {
                const eastl::string message =
                    eastl::string(assetLabel) +
                    " asset has an invalid byte length.";
                throw std::runtime_error(message.c_str());
            }
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.seekg(0, std::ios::beg);
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input)
            {
                const eastl::string message =
                    eastl::string("Could not read complete ") + assetLabel +
                    " asset.";
                throw std::runtime_error(message.c_str());
            }
            return bytes;
        }

        /** Validates one replay or canonical-state file against its locked digest. */
        eastl::string validateWaterInputArtifact(
            const eastl::string &inputPath,
            const char *expectedSha256,
            const char *inputLabel)
        {
            if (inputPath.empty())
            {
                const eastl::string message =
                    eastl::string("webgl_gpgpu_water requires ") +
                    inputLabel + ".";
                throw std::invalid_argument(message.c_str());
            }
            const eastl::vector<uint8_t> bytes = readWaterBinaryAsset(
                std::filesystem::path(inputPath.c_str()),
                inputLabel);
            const eastl::string digest = calculateWaterSha256(
                bytes.data(),
                bytes.size());
            if (digest != expectedSha256)
            {
                const eastl::string message =
                    eastl::string("webgl_gpgpu_water ") + inputLabel +
                    " diverged from its clean-reference lock.";
                throw std::runtime_error(message.c_str());
            }
            return digest;
        }

        /** Returns one validated little-endian uint32 from the derived duck pack. */
        uint32_t readWaterPackUint32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < sizeof(uint32_t))
            {
                throw std::runtime_error(
                    "Derived duck pack uint32 field is out of range.");
            }
            return uint32_t(bytes[offset]) |
                   (uint32_t(bytes[offset + 1u]) << 8u) |
                   (uint32_t(bytes[offset + 2u]) << 16u) |
                   (uint32_t(bytes[offset + 3u]) << 24u);
        }

        /** Returns one validated little-endian float32 from the derived duck pack. */
        float readWaterPackFloat(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            const uint32_t bits = readWaterPackUint32(bytes, offset);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        /** Returns one lowercase hexadecimal SHA field embedded in the derived pack. */
        eastl::string readWaterPackSha256(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            constexpr size_t DigestByteCount = CC_SHA256_DIGEST_LENGTH;
            if (offset > bytes.size() || bytes.size() - offset < DigestByteCount)
            {
                throw std::runtime_error(
                    "Derived duck pack SHA-256 field is out of range.");
            }
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(DigestByteCount * 2u);
            for (size_t index = 0u; index < DigestByteCount; ++index)
            {
                const uint8_t value = bytes[offset + index];
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Reads one newline-delimited ASCII field from a locked Radiance file. */
        eastl::string readWaterHdrLine(
            const eastl::vector<uint8_t> &bytes,
            size_t &offset)
        {
            if (offset >= bytes.size())
            {
                throw std::runtime_error(
                    "Radiance environment ended before its header was complete.");
            }
            const size_t lineStart = offset;
            while (offset < bytes.size() && bytes[offset] != '\n')
            {
                ++offset;
            }
            if (offset >= bytes.size())
            {
                throw std::runtime_error(
                    "Radiance environment header has no terminating newline.");
            }
            size_t lineEnd = offset;
            ++offset;
            if (lineEnd > lineStart && bytes[lineEnd - 1u] == '\r')
            {
                --lineEnd;
            }
            return eastl::string(
                reinterpret_cast<const char *>(bytes.data() + lineStart),
                lineEnd - lineStart);
        }

        /** Decodes the exact r185 RGBE scanlines into top-down linear float texels. */
        GpgpuWaterEnvironmentAsset loadWaterEnvironmentAsset(
            const ThreeSampleHostOptions &options)
        {
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "webgl_gpgpu_water requires explicit --asset-root.");
            }
            const std::filesystem::path environmentPath =
                std::filesystem::path(options.assetRoot.c_str()) /
                "textures" / "equirectangular" /
                "blouberg_sunrise_2_1k.hdr";
            const eastl::vector<uint8_t> bytes =
                readWaterBinaryAsset(environmentPath, "Radiance environment");
            GpgpuWaterEnvironmentAsset asset;
            asset.sourceSha256 = calculateWaterSha256(
                bytes.data(), bytes.size());
            if (asset.sourceSha256 != EnvironmentSourceSha256)
            {
                throw std::runtime_error(
                    "Radiance environment SHA-256 diverged from the r185 lock.");
            }

            size_t offset = 0u;
            const eastl::string magic = readWaterHdrLine(bytes, offset);
            if (magic != "#?RADIANCE" && magic != "#?RGBE")
            {
                throw std::runtime_error(
                    "Radiance environment has an invalid program identifier.");
            }
            bool foundFormat = false;
            for (;;)
            {
                const eastl::string line = readWaterHdrLine(bytes, offset);
                if (line.empty())
                {
                    break;
                }
                if (line == "FORMAT=32-bit_rle_rgbe")
                {
                    foundFormat = true;
                }
            }
            if (!foundFormat)
            {
                throw std::runtime_error(
                    "Radiance environment does not use locked RGBE RLE storage.");
            }
            const eastl::string dimensions = readWaterHdrLine(bytes, offset);
            unsigned int decodedHeight = 0u;
            unsigned int decodedWidth = 0u;
            if (std::sscanf(
                    dimensions.c_str(),
                    "-Y %u +X %u",
                    &decodedHeight,
                    &decodedWidth) != 2 ||
                decodedWidth != EnvironmentWidth ||
                decodedHeight != EnvironmentHeight)
            {
                throw std::runtime_error(
                    "Radiance environment dimensions or orientation diverged from r185.");
            }
            asset.width = decodedWidth;
            asset.height = decodedHeight;
            asset.pixels.resize(
                size_t(asset.width) * size_t(asset.height));
            eastl::vector<uint8_t> scanline(size_t(asset.width) * 4u);
            for (uint32_t row = 0u; row < asset.height; ++row)
            {
                if (offset > bytes.size() || bytes.size() - offset < 4u ||
                    bytes[offset] != 2u || bytes[offset + 1u] != 2u ||
                    (bytes[offset + 2u] & 0x80u) != 0u ||
                    ((uint32_t(bytes[offset + 2u]) << 8u) |
                     uint32_t(bytes[offset + 3u])) != asset.width)
                {
                    throw std::runtime_error(
                        "Radiance environment has an invalid RGBE scanline prefix.");
                }
                offset += 4u;
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    uint32_t column = 0u;
                    while (column < asset.width)
                    {
                        if (offset >= bytes.size())
                        {
                            throw std::runtime_error(
                                "Radiance environment scanline ended unexpectedly.");
                        }
                        const uint32_t encodedCount = bytes[offset++];
                        const bool isRepeated = encodedCount > 128u;
                        const uint32_t count = isRepeated
                                                   ? encodedCount - 128u
                                                   : encodedCount;
                        if (count == 0u || count > asset.width - column)
                        {
                            throw std::runtime_error(
                                "Radiance environment scanline run is out of range.");
                        }
                        if (isRepeated)
                        {
                            if (offset >= bytes.size())
                            {
                                throw std::runtime_error(
                                    "Radiance environment repeated run has no value.");
                            }
                            const uint8_t value = bytes[offset++];
                            for (uint32_t runIndex = 0u;
                                 runIndex < count;
                                 ++runIndex)
                            {
                                scanline[size_t(channel) * asset.width +
                                         column + runIndex] = value;
                            }
                        }
                        else
                        {
                            if (offset > bytes.size() ||
                                bytes.size() - offset < count)
                            {
                                throw std::runtime_error(
                                    "Radiance environment literal run is truncated.");
                            }
                            std::memcpy(
                                scanline.data() +
                                    size_t(channel) * asset.width + column,
                                bytes.data() + offset,
                                count);
                            offset += count;
                        }
                        column += count;
                    }
                }
                for (uint32_t column = 0u; column < asset.width; ++column)
                {
                    const uint8_t exponent =
                        scanline[size_t(3u) * asset.width + column];
                    const float scale =
                        std::ldexp(1.0f, int(exponent) - 128) / 255.0f;
                    asset.pixels[size_t(row) * asset.width + column] = float4(
                        float(scanline[column]) * scale,
                        float(scanline[size_t(asset.width) + column]) * scale,
                        float(scanline[size_t(2u) * asset.width + column]) * scale,
                        1.0f);
                }
            }
            return asset;
        }

        /** Flattens a complete explicit sRGB mip chain into one TextureComponent payload. */
        void buildWaterDuckTexturePayload(
            const RgbaImageData &baseImage,
            GpgpuWaterDuckMeshAsset &asset)
        {
            const eastl::vector<RgbaImageData> mipChain =
                buildSrgbMipChain(baseImage);
            asset.textureBytes.clear();
            asset.textureMipOffsets.clear();
            uint64_t byteOffset = 0u;
            for (const RgbaImageData &mip : mipChain)
            {
                asset.textureMipOffsets.push_back(byteOffset);
                asset.textureBytes.insert(
                    asset.textureBytes.end(),
                    mip.pixels.begin(),
                    mip.pixels.end());
                byteOffset += mip.pixels.size();
            }
            asset.textureWidth = baseImage.width;
            asset.textureHeight = baseImage.height;
        }

        /** Loads and validates the canonical C++-readable duck primitive and texture assets. */
        GpgpuWaterDuckMeshAsset loadWaterDuckMeshAsset(
            const ThreeSampleHostOptions &options)
        {
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "webgl_gpgpu_water requires explicit --asset-root.");
            }
            const std::filesystem::path derivedRoot =
                std::filesystem::path(options.assetRoot.c_str()) /
                "derived" / "webgl_gpgpu_water";
            const std::filesystem::path meshPath =
                derivedRoot / "duck_mesh.bin";
            const std::filesystem::path texturePath = derivedRoot / "duck.png";
            const eastl::vector<uint8_t> meshBytes =
                readWaterBinaryAsset(meshPath, "derived duck mesh");
            GpgpuWaterDuckMeshAsset asset;
            asset.meshPackSha256 = calculateWaterSha256(
                meshBytes.data(), meshBytes.size());
            if (asset.meshPackSha256 != DuckMeshPackSha256)
            {
                throw std::runtime_error(
                    "Derived duck mesh pack SHA-256 diverged from the r185 lock.");
            }
            if (meshBytes.size() < DuckMeshPackHeaderSize ||
                std::memcmp(
                    meshBytes.data(),
                    DuckMeshPackMagic,
                    sizeof(DuckMeshPackMagic) - 1u) != 0 ||
                readWaterPackUint32(meshBytes, 8u) != DuckMeshPackVersion ||
                readWaterPackUint32(meshBytes, 12u) != DuckMeshPackHeaderSize ||
                readWaterPackUint32(meshBytes, 16u) != DuckVertexCount ||
                readWaterPackUint32(meshBytes, 20u) != DuckIndexCount ||
                readWaterPackUint32(meshBytes, 24u) != DuckPackedVertexStride ||
                readWaterPackUint32(meshBytes, 28u) != sizeof(uint32_t))
            {
                throw std::runtime_error(
                    "Derived duck mesh pack header diverged from the locked schema.");
            }
            if (readWaterPackSha256(meshBytes, 88u) != DuckSourceGlbSha256 ||
                readWaterPackSha256(meshBytes, 120u) != DuckDecoderWasmSha256 ||
                readWaterPackSha256(meshBytes, 152u) != DuckDecoderWrapperSha256)
            {
                throw std::runtime_error(
                    "Derived duck mesh pack source provenance diverged from r185.");
            }
            const uint32_t vertexOffset = readWaterPackUint32(meshBytes, 32u);
            const uint32_t indexOffset = readWaterPackUint32(meshBytes, 36u);
            const uint64_t vertexBytes =
                uint64_t(DuckVertexCount) * DuckPackedVertexStride;
            const uint64_t indexBytes = uint64_t(DuckIndexCount) * sizeof(uint32_t);
            if (vertexOffset < DuckMeshPackHeaderSize ||
                uint64_t(vertexOffset) + vertexBytes > meshBytes.size() ||
                indexOffset < uint64_t(vertexOffset) + vertexBytes ||
                uint64_t(indexOffset) + indexBytes != meshBytes.size())
            {
                throw std::runtime_error(
                    "Derived duck mesh pack data ranges are invalid.");
            }
            asset.vertices.reserve(DuckVertexCount);
            for (uint32_t vertexIndex = 0u;
                 vertexIndex < DuckVertexCount;
                 ++vertexIndex)
            {
                const size_t offset =
                    size_t(vertexOffset) +
                    size_t(vertexIndex) * DuckPackedVertexStride;
                asset.vertices.push_back({
                    .position = {
                        readWaterPackFloat(meshBytes, offset),
                        readWaterPackFloat(meshBytes, offset + 4u),
                        readWaterPackFloat(meshBytes, offset + 8u),
                        1.0f,
                    },
                    .normal = {
                        readWaterPackFloat(meshBytes, offset + 12u),
                        readWaterPackFloat(meshBytes, offset + 16u),
                        readWaterPackFloat(meshBytes, offset + 20u),
                        0.0f,
                    },
                    .uvAndReserved = {
                        readWaterPackFloat(meshBytes, offset + 24u),
                        readWaterPackFloat(meshBytes, offset + 28u),
                        0.0f,
                        0.0f,
                    },
                });
            }
            asset.indices.reserve(DuckIndexCount);
            for (uint32_t index = 0u; index < DuckIndexCount; ++index)
            {
                const uint32_t vertexIndex = readWaterPackUint32(
                    meshBytes,
                    size_t(indexOffset) + size_t(index) * sizeof(uint32_t));
                if (vertexIndex >= DuckVertexCount)
                {
                    throw std::runtime_error(
                        "Derived duck index exceeds the locked vertex range.");
                }
                asset.indices.push_back(vertexIndex);
            }
            asset.material = {
                .baseColor = {
                    readWaterPackFloat(meshBytes, 48u),
                    readWaterPackFloat(meshBytes, 52u),
                    readWaterPackFloat(meshBytes, 56u),
                    readWaterPackFloat(meshBytes, 60u),
                },
                .metalnessRoughnessOpacityAndReserved = {
                    readWaterPackFloat(meshBytes, 64u),
                    readWaterPackFloat(meshBytes, 68u),
                    1.0f,
                    0.0f,
                },
            };
            const eastl::vector<uint8_t> textureSourceBytes =
                readWaterBinaryAsset(texturePath, "derived duck texture");
            asset.textureSha256 = calculateWaterSha256(
                textureSourceBytes.data(), textureSourceBytes.size());
            if (asset.textureSha256 != DuckTextureSha256)
            {
                throw std::runtime_error(
                    "Derived duck texture SHA-256 diverged from the r185 lock.");
            }
            buildWaterDuckTexturePayload(decodePngRgba8(texturePath), asset);
            if (asset.textureWidth != 512u || asset.textureHeight != 512u)
            {
                throw std::runtime_error(
                    "Derived duck texture extent diverged from the r185 lock.");
            }
            return asset;
        }

        /** Converts one r185 hexadecimal sRGB channel into the linear working space. */
        float convertWaterSrgbChannelToLinear(uint32_t channel)
        {
            const double srgb = double(channel) / 255.0;
            return static_cast<float>(
                srgb < 0.04045
                    ? srgb * 0.0773993808
                    : std::pow(srgb * 0.9478672986 + 0.0521327014, 2.4));
        }

        /** Builds one bounded deterministic identifier containing a zero-based ordinal. */
        eastl::string makeWaterOrdinalIdentifier(
            const char *prefix,
            uint32_t ordinal)
        {
            char identifier[64u] = {};
            const int written = std::snprintf(
                identifier,
                sizeof(identifier),
                "%s%u",
                prefix,
                ordinal);
            if (written < 0 || static_cast<size_t>(written) >= sizeof(identifier))
            {
                throw std::runtime_error(
                    "webgl_gpgpu_water ordinal identifier overflowed its bound.");
            }
            return eastl::string(identifier);
        }

        /** Builds the current zero-to-one perspective projection for the fixed r185 camera. */
        glm::mat4 makeWaterProjection(uint32_t width, uint32_t height)
        {
            if (width == 0u || height == 0u)
            {
                throw std::invalid_argument(
                    "webgl_gpgpu_water capture dimensions must be positive.");
            }
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 1000.0;
            const double top =
                NearDistance * std::tan(75.0 * Pi / 360.0);
            const double projectionHeight = top * 2.0;
            const double projectionWidth =
                projectionHeight * double(width) / double(height);
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(
                2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(
                -2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(-FarDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -FarDistance * NearDistance / depth);
            return projection;
        }

        /** Fills one entity with the camera, identity instance, and derived normal matrix. */
        void initializeWaterEntityTransforms(
            GpgpuWaterEntityState &entity,
            const glm::mat4 &model,
            const glm::mat4 &viewProjection)
        {
            entity.objectData.model = model;
            entity.objectData.normalMatrix = glm::transpose(glm::inverse(model));
            entity.objectData.viewProjection = viewProjection;
            entity.objectData.cameraPositionAndTime = {0.0f, 2.0f, 4.0f, 0.0f};
            entity.instanceData.translation = {0.0f, 0.0f, 0.0f, 0.0f};
        }

        /** Builds Three r185 PlaneGeometry topology with a deliberately flat frame-zero surface. */
        void buildWaterGeometry(GpgpuWaterEntityState &entity)
        {
            entity.vertices.clear();
            entity.indices.clear();
            entity.vertices.reserve(WaterVertexCount);
            entity.indices.reserve(WaterIndexCount);
            for (uint32_t row = 0u; row < WaterTextureWidth; ++row)
            {
                const float sourceY =
                    WaterBoundsHalf -
                    float(row) * WaterBounds /
                        float(WaterTextureWidth - 1u);
                const float v =
                    1.0f - float(row) / float(WaterTextureWidth - 1u);
                for (uint32_t column = 0u; column < WaterTextureWidth; ++column)
                {
                    const float x =
                        -WaterBoundsHalf +
                        float(column) * WaterBounds /
                            float(WaterTextureWidth - 1u);
                    const float u =
                        float(column) / float(WaterTextureWidth - 1u);
                    entity.vertices.push_back({
                        .position = {x, 0.0f, -sourceY, 1.0f},
                        .normal = {0.0f, 1.0f, 0.0f, 0.0f},
                        .uvAndReserved = {u, v, 0.0f, 0.0f},
                    });
                }
            }
            for (uint32_t row = 0u; row + 1u < WaterTextureWidth; ++row)
            {
                for (uint32_t column = 0u;
                     column + 1u < WaterTextureWidth;
                     ++column)
                {
                    const uint32_t a =
                        column + WaterTextureWidth * row;
                    const uint32_t b =
                        column + WaterTextureWidth * (row + 1u);
                    const uint32_t c = b + 1u;
                    const uint32_t d = a + 1u;
                    entity.indices.push_back(a);
                    entity.indices.push_back(b);
                    entity.indices.push_back(d);
                    entity.indices.push_back(b);
                    entity.indices.push_back(c);
                    entity.indices.push_back(d);
                }
            }
        }

        /** Builds the exact 65-vertex, 288-index r185 pool-border TorusGeometry. */
        void buildWaterBorderGeometry(GpgpuWaterEntityState &entity)
        {
            constexpr uint32_t RadialSegments = 12u;
            constexpr uint32_t TubularSegments = 4u;
            constexpr float Radius = 4.2f;
            constexpr float Tube = 0.1f;
            const glm::mat4 rotation =
                glm::rotate(
                    glm::mat4(1.0f),
                    static_cast<float>(Pi * 0.25),
                    glm::vec3(0.0f, 1.0f, 0.0f)) *
                glm::rotate(
                    glm::mat4(1.0f),
                    static_cast<float>(Pi * 0.5),
                    glm::vec3(1.0f, 0.0f, 0.0f));
            entity.vertices.clear();
            entity.indices.clear();
            entity.vertices.reserve((RadialSegments + 1u) *
                                    (TubularSegments + 1u));
            entity.indices.reserve(RadialSegments * TubularSegments * 6u);
            for (uint32_t radial = 0u; radial <= RadialSegments; ++radial)
            {
                const double v =
                    double(radial) / double(RadialSegments) * Pi * 2.0;
                for (uint32_t tubular = 0u;
                     tubular <= TubularSegments;
                     ++tubular)
                {
                    const double u =
                        double(tubular) / double(TubularSegments) * Pi * 2.0;
                    const glm::vec3 sourcePosition(
                        static_cast<float>((Radius + Tube * std::cos(v)) *
                                           std::cos(u)),
                        static_cast<float>((Radius + Tube * std::cos(v)) *
                                           std::sin(u)),
                        static_cast<float>(Tube * std::sin(v)));
                    const glm::vec3 center(
                        static_cast<float>(Radius * std::cos(u)),
                        static_cast<float>(Radius * std::sin(u)),
                        0.0f);
                    const glm::vec3 sourceNormal =
                        glm::normalize(sourcePosition - center);
                    const glm::vec3 position =
                        glm::vec3(rotation * glm::vec4(sourcePosition, 1.0f));
                    const glm::vec3 normal =
                        glm::normalize(glm::vec3(
                            rotation * glm::vec4(sourceNormal, 0.0f)));
                    entity.vertices.push_back({
                        .position = {position.x, position.y, position.z, 1.0f},
                        .normal = {normal.x, normal.y, normal.z, 0.0f},
                        .uvAndReserved = {
                            float(tubular) / float(TubularSegments),
                            float(radial) / float(RadialSegments),
                            0.0f,
                            0.0f,
                        },
                    });
                }
            }
            for (uint32_t radial = 1u; radial <= RadialSegments; ++radial)
            {
                for (uint32_t tubular = 1u;
                     tubular <= TubularSegments;
                     ++tubular)
                {
                    const uint32_t a =
                        (TubularSegments + 1u) * radial + tubular - 1u;
                    const uint32_t b =
                        (TubularSegments + 1u) * (radial - 1u) +
                        tubular - 1u;
                    const uint32_t c =
                        (TubularSegments + 1u) * (radial - 1u) + tubular;
                    const uint32_t d =
                        (TubularSegments + 1u) * radial + tubular;
                    entity.indices.push_back(a);
                    entity.indices.push_back(b);
                    entity.indices.push_back(d);
                    entity.indices.push_back(b);
                    entity.indices.push_back(c);
                    entity.indices.push_back(d);
                }
            }
        }

        /** Copies the exact decoded duck primitive into one ordinary cloned Scene entity. */
        void assignWaterDuckGeometry(
            GpgpuWaterEntityState &entity,
            const GpgpuWaterDuckMeshAsset &asset)
        {
            entity.vertices = asset.vertices;
            entity.indices = asset.indices;
        }

        /** Builds the exact initial RGBA32Float payload and all deterministic duck positions. */
        void buildWaterDeterministicState(
            eastl::vector<float4> &heightValues,
            eastl::array<GpgpuWaterDuckState, DuckCount> &ducks,
            uint32_t &finalRandomState)
        {
            ThreeCompat::DeterministicRandom random(WaterRandomSeed);
            for (uint32_t drawIndex = 0u;
                 drawIndex < PreSimplexRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            const GpgpuWaterSimplexNoise2D simplex(random);
            heightValues.clear();
            heightValues.reserve(WaterVertexCount);
            for (uint32_t row = 0u; row < WaterTextureWidth; ++row)
            {
                for (uint32_t column = 0u;
                     column < WaterTextureWidth;
                     ++column)
                {
                    const double x =
                        double(column) * 128.0 / double(WaterTextureWidth);
                    const double y =
                        double(row) * 128.0 / double(WaterTextureWidth);
                    double amplitude = 0.1;
                    double frequency = 0.025;
                    double value = 0.0;
                    for (uint32_t octave = 0u; octave < 15u; ++octave)
                    {
                        value += amplitude * simplex.noise(
                                                 x * frequency,
                                                 y * frequency);
                        amplitude *= 0.53 + 0.025 * double(octave);
                        frequency *= 1.25;
                    }
                    const float floatValue = static_cast<float>(value);
                    heightValues.push_back(
                        float4(floatValue, floatValue, 0.0f, 1.0f));
                }
            }
            for (uint32_t drawIndex = 0u;
                 drawIndex < PreDuckRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            for (uint32_t duckIndex = 0u; duckIndex < DuckCount; ++duckIndex)
            {
                if (duckIndex + 1u < DuckCount)
                {
                    for (uint32_t drawIndex = 0u;
                         drawIndex < DuckCloneRandomDrawCount;
                         ++drawIndex)
                    {
                        (void)random.nextUint32();
                    }
                }
                ducks[duckIndex].position.x =
                    (nextWaterRandomUnit(random) - 0.5) * WaterBounds * 0.7;
                ducks[duckIndex].position.y = 0.0;
                ducks[duckIndex].position.z =
                    (nextWaterRandomUnit(random) - 0.5) * WaterBounds * 0.7;
                ducks[duckIndex].velocity = glm::dvec3(0.0);
                ducks[duckIndex].orientation =
                    glm::dquat(1.0, 0.0, 0.0, 0.0);
            }
            for (uint32_t drawIndex = 0u;
                 drawIndex < FirstRenderInternalRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            finalRandomState = random.getState();
            if (finalRandomState != ExpectedFinalRandomState)
            {
                throw std::runtime_error(
                    "webgl_gpgpu_water final RNG state diverged from r185.");
            }
        }

        /** Appends one typed payload to a RenderSet BufferComponent allocation. */
        void appendWaterBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const eastl::string &name,
            const void *value,
            uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }

        /** Appends one fixed entity texture slot using explicit source or one white texel. */
        void appendWaterTexturePayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            const GpgpuWaterEntityState &entity,
            const GpgpuWaterDuckMeshAsset &duckAsset)
        {
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle =
                WebglGpgpuWaterSceneRenderSetComponents::textures;
            if (entity.renderFlags.values.w != 0u)
            {
                textureComponent.textures.push_back({
                    .textureName = entity.storagePrefix + "BaseColorSrgb",
                    .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                    .width = duckAsset.textureWidth,
                    .height = duckAsset.textureHeight,
                    .data = duckAsset.textureBytes.data(),
                    .dataStorageBytes = duckAsset.textureBytes.size(),
                    .mipmapOffsetBytes = duckAsset.textureMipOffsets,
                });
            }
            else
            {
                textureComponent.textures.push_back({
                    .textureName = entity.storagePrefix + "WhiteBaseColor",
                    .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                    .width = 1u,
                    .height = 1u,
                    .data = WhiteSceneTexture,
                    .dataStorageBytes = sizeof(WhiteSceneTexture),
                    .mipmapOffsetBytes = {0u},
                });
            }
            allocation.textureInfos.push_back(eastl::move(textureComponent));
        }

        /** Allocates one ordinary non-instanced entity in the unique Scene RenderSet. */
        GVM::Core::RenderEntityIndex allocateWaterEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const GpgpuWaterEntityState &entity,
            const GpgpuWaterDuckMeshAsset &duckAsset)
        {
            if (entity.vertices.empty() || entity.indices.empty() ||
                entity.vertices.size() > std::numeric_limits<uint32_t>::max() ||
                entity.indices.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::out_of_range(
                    "webgl_gpgpu_water entity geometry has an invalid draw range.");
            }
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount =
                static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount =
                static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            appendWaterBufferPayload(
                allocation,
                WebglGpgpuWaterSceneRenderSetComponents::vertices,
                entity.storagePrefix + "Vertices",
                entity.vertices.data(),
                entity.vertices.size() * sizeof(GpgpuWaterHostVertex));
            appendWaterBufferPayload(
                allocation,
                WebglGpgpuWaterSceneRenderSetComponents::indices,
                entity.storagePrefix + "Indices",
                entity.indices.data(),
                entity.indices.size() * sizeof(uint32_t));
            appendWaterBufferPayload(
                allocation,
                WebglGpgpuWaterSceneRenderSetComponents::objects,
                entity.storagePrefix + "Object",
                &entity.objectData,
                sizeof(entity.objectData));
            appendWaterBufferPayload(
                allocation,
                WebglGpgpuWaterSceneRenderSetComponents::instances,
                entity.storagePrefix + "Instance",
                &entity.instanceData,
                sizeof(entity.instanceData));
            appendWaterBufferPayload(
                allocation,
                WebglGpgpuWaterSceneRenderSetComponents::materials,
                entity.storagePrefix + "Material",
                &entity.materialData,
                sizeof(entity.materialData));
            appendWaterBufferPayload(
                allocation,
                WebglGpgpuWaterSceneRenderSetComponents::renderFlags,
                entity.storagePrefix + "RenderFlags",
                &entity.renderFlags,
                sizeof(entity.renderFlags));
            appendWaterTexturePayload(allocation, entity, duckAsset);
            return encoder.allocEntity(allocation);
        }

        /** Returns the clamped nearest texel coordinate selected by normalized sampling. */
        uint32_t waterNearestCoordinate(float coordinate)
        {
            const float scaled = coordinate * float(WaterTextureWidth);
            const int32_t integerCoordinate =
                static_cast<int32_t>(std::floor(scaled));
            return static_cast<uint32_t>(
                std::clamp(
                    integerCoordinate,
                    0,
                    static_cast<int32_t>(WaterTextureWidth - 1u)));
        }

        /** Reads one clamped height channel from a tightly packed RGBA32Float image. */
        float readWaterHeight(
            const eastl::vector<float4> &heightValues,
            int32_t x,
            int32_t y)
        {
            const uint32_t clampedX = static_cast<uint32_t>(
                std::clamp(x, 0, static_cast<int32_t>(WaterTextureWidth - 1u)));
            const uint32_t clampedY = static_cast<uint32_t>(
                std::clamp(y, 0, static_cast<int32_t>(WaterTextureWidth - 1u)));
            return heightValues[clampedY * WaterTextureWidth + clampedX].x;
        }

        /** Computes the r185 height and two finite-difference normal channels at one UV. */
        glm::vec3 sampleWaterHeightAndNormal(
            const eastl::vector<float4> &heightValues,
            float u,
            float v)
        {
            const int32_t x = static_cast<int32_t>(waterNearestCoordinate(u));
            const int32_t y = static_cast<int32_t>(waterNearestCoordinate(v));
            const float height = readWaterHeight(heightValues, x, y);
            const float normalX =
                (readWaterHeight(heightValues, x - 1, y) -
                 readWaterHeight(heightValues, x + 1, y)) *
                float(WaterTextureWidth) / WaterBounds;
            const float normalY =
                (readWaterHeight(heightValues, x, y - 1) -
                 readWaterHeight(heightValues, x, y + 1)) *
                float(WaterTextureWidth) / WaterBounds;
            return glm::vec3(height, normalX, normalY);
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeWaterRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_gpgpu_water RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebglGpgpuWaterRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_gpgpu_water")
        {
            throw std::invalid_argument(
                "WebglGpgpuWater requires case-id webgl_gpgpu_water.");
        }
        const bool initialScenario =
            options.scenarioId == "initial-assets-loaded" &&
            options.targetFrame == 0u;
        const bool loaderScenario =
            options.scenarioId == "canonical-loader-snapshot" &&
            options.targetFrame == 0u;
        const bool pointerScenario =
            options.scenarioId == "pointer-viscosity-shadow" &&
            options.targetFrame == 120u;
        if (!initialScenario && !loaderScenario && !pointerScenario)
        {
            throw std::invalid_argument(
                "WebglGpgpuWater slice requires initial-assets-loaded/frame 0, "
                "canonical-loader-snapshot/frame 0, or "
                "pointer-viscosity-shadow/frame 120.");
        }
        if (options.frameCount != options.targetFrame + 1u)
        {
            throw std::invalid_argument(
                "WebglGpgpuWater callbacks must execute inclusively through the target frame.");
        }
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument(
                "WebglGpgpuWater comparison requires an 800x500 output.");
        }
        if (options.randomSeed != WaterRandomSeed)
        {
            throw std::invalid_argument(
                "WebglGpgpuWater requires random seed 0x12345678.");
        }
        interactiveScenario = pointerScenario;
        inputReplaySha256.clear();
        canonicalStateSha256.clear();
        if (interactiveScenario)
        {
            inputReplaySha256 = validateWaterInputArtifact(
                options.inputReplayPath,
                PointerReplaySha256,
                "pointer input replay");
            canonicalStateSha256 = validateWaterInputArtifact(
                options.canonicalStatePath,
                PointerStateScriptSha256,
                "pointer canonical-state script");
        }
        else if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Frame-zero WebglGpgpuWater scenarios must not consume input replay.");
        }

        device = inDevice;
        shadowEnabled = interactiveScenario;
        viscosity = interactiveScenario ? 0.97f : 0.93f;
        buildWaterDeterministicState(initialHeight, ducks, finalRandomState);
        initialHeightSha256 = calculateWaterSha256(
            initialHeight.data(),
            initialHeight.size() * sizeof(float4));
        if (initialHeightSha256 != InitialHeightSha256)
        {
            throw std::runtime_error(
                "webgl_gpgpu_water initial RGBA32Float payload diverged from r185.");
        }
        currentHeight = initialHeight;
        currentHeightSha256 = initialHeightSha256;
        duckMeshAsset = loadWaterDuckMeshAsset(options);
        environmentAsset = loadWaterEnvironmentAsset(options);
        dfgLutPackedPixels.assign(
            R185DfgLutPackedPixels,
            R185DfgLutPackedPixels + 256u);

        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(0.0f, 2.0f, 4.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 viewProjection =
            makeWaterProjection(options.width, options.height) * view;

        entities[0u].logicalId = "water";
        entities[0u].storagePrefix = "WebglGpgpuWaterSurface";
        buildWaterGeometry(entities[0u]);
        initializeWaterEntityTransforms(
            entities[0u],
            glm::mat4(1.0f),
            viewProjection);
        entities[0u].materialData = {
            .baseColor = {
                convertWaterSrgbChannelToLinear(0x9bu),
                convertWaterSrgbChannelToLinear(0xd2u),
                convertWaterSrgbChannelToLinear(0xecu),
                1.0f,
            },
            .metalnessRoughnessOpacityAndReserved = {0.9f, 0.0f, 0.8f, 0.0f},
        };
        entities[0u].renderFlags.values = {0u, 1u, 0u, 0u};
        entities[0u].renderFlags.values.z = shadowEnabled ? 1u : 0u;

        entities[1u].logicalId = "pool-border";
        entities[1u].storagePrefix = "WebglGpgpuWaterPoolBorder";
        buildWaterBorderGeometry(entities[1u]);
        initializeWaterEntityTransforms(
            entities[1u],
            glm::mat4(1.0f),
            viewProjection);
        entities[1u].materialData = {
            .baseColor = {
                convertWaterSrgbChannelToLinear(0x90u),
                convertWaterSrgbChannelToLinear(0x88u),
                convertWaterSrgbChannelToLinear(0x77u),
                1.0f,
            },
            .metalnessRoughnessOpacityAndReserved = {0.0f, 0.2f, 1.0f, 0.0f},
        };
        entities[1u].renderFlags.values = {1u, 1u, 0u, 0u};
        entities[1u].renderFlags.values.z = shadowEnabled ? 1u : 0u;

        for (uint32_t duckIndex = 0u; duckIndex < DuckCount; ++duckIndex)
        {
            GpgpuWaterEntityState &entity = entities[duckIndex + 2u];
            entity.logicalId =
                makeWaterOrdinalIdentifier("duck-", duckIndex);
            entity.storagePrefix =
                makeWaterOrdinalIdentifier("WebglGpgpuWaterDuck", duckIndex);
            assignWaterDuckGeometry(entity, duckMeshAsset);
            initializeWaterEntityTransforms(
                entity,
                glm::translate(
                    glm::mat4(1.0f),
                    glm::vec3(ducks[duckIndex].position)),
                viewProjection);
            entity.materialData = duckMeshAsset.material;
            entity.renderFlags.values = {1u, 1u, 0u, 1u};
            entity.renderFlags.values.z = shadowEnabled ? 1u : 0u;
        }
        currentWaterVertexSha256 = calculateWaterSha256(
            entities[0u].vertices.data(),
            entities[0u].vertices.size() * sizeof(GpgpuWaterHostVertex));
    }

    void WebglGpgpuWaterRuntimeAdapter::allocateSceneEntities(
        GVM::Core::AbstractRendererImpl &renderer)
    {
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "webgl_gpgpu_water could not create its Scene RenderSet encoder.");
        }
        for (GpgpuWaterEntityState &entity : entities)
        {
            entity.entityIndex =
                allocateWaterEntity(*encoder, entity, duckMeshAsset);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglGpgpuWaterRuntimeAdapter::updateSceneFromHeight(
        GVM::Core::AbstractRendererImpl &renderer,
        const eastl::vector<float4> &heightValues)
    {
        GpgpuWaterEntityState &water = entities[0u];
        for (GpgpuWaterHostVertex &vertex : water.vertices)
        {
            const glm::vec3 sampled = sampleWaterHeightAndNormal(
                heightValues,
                vertex.uvAndReserved.x,
                vertex.uvAndReserved.y);
            vertex.position.y = sampled.x;
            const glm::vec3 worldNormal =
                glm::normalize(glm::vec3(sampled.y, 1.0f, -sampled.z));
            vertex.normal = {
                worldNormal.x,
                worldNormal.y,
                worldNormal.z,
                0.0f,
            };
        }

        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "webgl_gpgpu_water could not create its dynamic update encoder.");
        }
        encoder->setBufferComponentData(
            water.entityIndex,
            WebglGpgpuWaterSceneRenderSetComponents::vertices,
            water.vertices.data(),
            water.vertices.size() * sizeof(GpgpuWaterHostVertex),
            0u,
            static_cast<uint32_t>(water.vertices.size()));

        for (uint32_t duckIndex = 0u; duckIndex < DuckCount; ++duckIndex)
        {
            GpgpuWaterDuckState &duck = ducks[duckIndex];
            const float u = static_cast<float>(
                duck.position.x / double(WaterBounds) + 0.5);
            const float v = static_cast<float>(
                0.5 - duck.position.z / double(WaterBounds));
            const glm::vec3 sampled =
                sampleWaterHeightAndNormal(heightValues, u, v);
            const glm::dvec3 startPosition = duck.position;
            duck.position.y = double(sampled.x);
            duck.velocity +=
                glm::dvec3(double(sampled.y), 0.0, -double(sampled.z)) *
                0.01;
            duck.velocity *= 0.998;
            duck.position += duck.velocity;
            constexpr double Limit = double(WaterBoundsHalf) - 0.2;
            constexpr double Decal = 0.001;
            if (duck.position.x < -Limit)
            {
                duck.position.x = -Limit + Decal;
                duck.velocity.x *= -0.3;
            }
            else if (duck.position.x > Limit)
            {
                duck.position.x = Limit - Decal;
                duck.velocity.x *= -0.3;
            }
            if (duck.position.z < -Limit)
            {
                duck.position.z = -Limit + Decal;
                duck.velocity.z *= -0.3;
            }
            else if (duck.position.z > Limit)
            {
                duck.position.z = Limit - Decal;
                duck.velocity.z *= -0.3;
            }

            const glm::dvec3 surfaceNormal = glm::normalize(glm::dvec3(
                double(sampled.y),
                1.0,
                -double(sampled.z)));
            glm::dvec3 movementDirection = startPosition - duck.position;
            movementDirection.y = 0.0;
            const double movementLength = glm::length(movementDirection);
            if (movementLength > 0.0)
            {
                movementDirection /= movementLength;
            }
            const glm::dquat horizontalOrientation =
                makeWaterUnitVectorQuaternion(
                    glm::dvec3(0.0, 0.0, -1.0),
                    movementDirection);
            const glm::dquat surfaceOrientation =
                makeWaterUnitVectorQuaternion(
                    glm::dvec3(0.0, 1.0, 0.0),
                    surfaceNormal);
            const glm::dquat targetOrientation =
                surfaceOrientation * horizontalOrientation;
            duck.orientation = slerpWaterQuaternion(
                duck.orientation,
                targetOrientation,
                0.017);

            GpgpuWaterEntityState &entity = entities[duckIndex + 2u];
            const glm::dmat4 model =
                glm::translate(glm::dmat4(1.0), duck.position) *
                glm::mat4_cast(duck.orientation);
            entity.objectData.model = glm::mat4(model);
            entity.objectData.normalMatrix = glm::transpose(
                glm::inverse(entity.objectData.model));
            encoder->setBufferComponentData(
                entity.entityIndex,
                WebglGpgpuWaterSceneRenderSetComponents::objects,
                &entity.objectData,
                sizeof(entity.objectData),
                0u,
                1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        currentWaterVertexSha256 = calculateWaterSha256(
            water.vertices.data(),
            water.vertices.size() * sizeof(GpgpuWaterHostVertex));
    }

    void WebglGpgpuWaterRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        if (frameIndex != processedFrameCount)
        {
            throw std::logic_error(
                "webgl_gpgpu_water callbacks must advance sequentially.");
        }
        ++callbackCounter;
        if (callbackCounter >= 2u)
        {
            if (!heightStepper || !heightTextureProvider || !heightTickProvider)
            {
                throw std::logic_error(
                    "webgl_gpgpu_water generated height callbacks are unavailable.");
            }
            WebglGpgpuWaterHeightUniforms uniforms;
            const glm::vec2 pointerPosition = interactiveScenario
                                                  ? makeWaterPointerPosition(
                                                        frameIndex)
                                                  : glm::vec2(10000.0f);
            uniforms.mousePositionSizeAndDepth =
                float4(
                    pointerPosition.x,
                    pointerPosition.y,
                    0.2f,
                    0.01f);
            uniforms.viscosityAndReserved =
                float4(viscosity, 0.0f, 0.0f, 0.0f);
            heightStepper(uniforms);
            callbackCounter = 0u;
            ++simulationTickCount;
            if (heightTickProvider() != simulationTickCount)
            {
                throw std::runtime_error(
                    "webgl_gpgpu_water generated and host tick counts diverged.");
            }
            currentHeight.resize(WaterVertexCount);
            device->graphicsQueue(0)
                ->readTexture(
                    heightTextureProvider(),
                    currentHeight.data(),
                    currentHeight.size() * sizeof(float4))
                ->submit();
            currentHeightSha256 = calculateWaterSha256(
                currentHeight.data(),
                currentHeight.size() * sizeof(float4));
            updateSceneFromHeight(renderer, currentHeight);
        }
        ++processedFrameCount;
    }

    void WebglGpgpuWaterRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computeWaterRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_gpgpu_water capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path heightPath(
                (options.captureMetadataPath + ".height.rgba32f").c_str());
            prepareWaterOutputPath(heightPath);
            std::ofstream output(
                heightPath,
                std::ios::binary | std::ios::out | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(currentHeight.data()),
                static_cast<std::streamsize>(
                    currentHeight.size() * sizeof(float4)));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write complete webgl_gpgpu_water height evidence.");
            }
        }
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeLoaderSemanticSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglGpgpuWaterRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        heightStepper = nullptr;
        heightTextureProvider = nullptr;
        heightTickProvider = nullptr;
    }

    void WebglGpgpuWaterRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareWaterOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_gpgpu_water RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_gpgpu_water RGBA capture.");
        }
    }

    void WebglGpgpuWaterRuntimeAdapter::writeCaptureMetadata(
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
        prepareWaterOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_gpgpu_water metadata output path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_gpgpu_water\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str()
               << "\",\n"
               << "  \"backend\": \""
               << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"implementationStage\": \"dsl-height-pmrem-vsm-pbr-aces\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"randomState\": "
               << (interactiveScenario
                       ? ExpectedInteractiveRandomState
                       : ExpectedFinalRandomState)
               << ",\n"
               << "  \"randomDrawCount\": " << TotalRandomDrawCount << ",\n"
               << "  \"finalRandomState\": " << finalRandomState << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"simulationFormat\": \"rgba32float-fragment-ping-pong\",\n"
               << "  \"simulationTickCount\": " << simulationTickCount
               << ",\n"
               << "  \"heightTextureIndex\": " << (simulationTickCount & 1u)
               << ",\n"
               << "  \"initialHeightSha256\": \""
               << initialHeightSha256.c_str() << "\",\n"
               << "  \"currentHeightSha256\": \""
               << currentHeightSha256.c_str() << "\",\n"
               << "  \"waterVertexSha256\": \""
               << currentWaterVertexSha256.c_str() << "\",\n"
               << "  \"duckMeshPackSha256\": \""
               << duckMeshAsset.meshPackSha256.c_str() << "\",\n"
               << "  \"duckSourceGlbSha256\": \""
               << DuckSourceGlbSha256 << "\",\n"
               << "  \"duckDecoderWasmSha256\": \""
               << DuckDecoderWasmSha256 << "\",\n"
               << "  \"duckDecoderWrapperSha256\": \""
               << DuckDecoderWrapperSha256 << "\",\n"
               << "  \"duckTextureSha256\": \""
               << duckMeshAsset.textureSha256.c_str() << "\",\n"
               << "  \"duckVertexCount\": " << duckMeshAsset.vertices.size()
               << ",\n"
               << "  \"duckIndexCount\": " << duckMeshAsset.indices.size()
               << ",\n"
               << "  \"duckTextureWidth\": " << duckMeshAsset.textureWidth
               << ",\n"
               << "  \"duckTextureHeight\": " << duckMeshAsset.textureHeight
               << ",\n"
               << "  \"environmentSourceSha256\": \""
               << environmentAsset.sourceSha256.c_str() << "\",\n"
               << "  \"environmentWidth\": " << environmentAsset.width
               << ",\n"
               << "  \"environmentHeight\": " << environmentAsset.height
               << ",\n"
               << "  \"toneMapping\": \"aces-filmic\",\n"
               << "  \"toneMappingExposure\": 0.5,\n"
               << "  \"visualSurfaceFlat\": "
               << (simulationTickCount == 0u ? "true" : "false") << ",\n"
               << "  \"viscosity\": " << viscosity << ",\n"
               << "  \"shadowEnabled\": "
               << (shadowEnabled ? "true" : "false") << ",\n"
               << "  \"wireframeEnabled\": false,\n"
               << "  \"canonicalStateSha256\": ";
        if (interactiveScenario)
        {
            output << "\"" << canonicalStateSha256.c_str() << "\",\n"
                   << "  \"inputReplay\": {\n"
                   << "    \"schemaVersion\": 1,\n"
                   << "    \"sha256\": \""
                   << inputReplaySha256.c_str() << "\",\n"
                   << "    \"caseId\": \"webgl_gpgpu_water\",\n"
                   << "    \"scenarioId\": \"pointer-viscosity-shadow\",\n"
                   << "    \"captureFrame\": 120,\n"
                   << "    \"eventCount\": 4,\n"
                   << "    \"lastEventFrame\": 30,\n"
                   << "    \"target\": \"" << PointerReplayTarget
                   << "\"\n"
                   << "  }\n";
        }
        else
        {
            output << "null,\n"
                   << "  \"inputReplay\": null\n";
        }
        output << "}\n";
    }

    void WebglGpgpuWaterRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareWaterOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_gpgpu_water structural snapshot path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_gpgpu_water\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"implementationStage\": \"dsl-height-pmrem-vsm-pbr-aces\",\n"
               << "  \"renderSetPolicy\": \"required\",\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"renderableObjectCount\": 14,\n"
               << "  \"entityCount\": 14,\n"
               << "  \"containsInstancing\": false,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"scenePassCount\": 5,\n"
               << "  \"defaultInvokedScenePassCount\": 5,\n"
               << "  \"drawCommandCount\": 5,\n"
               << "  \"screenPassCount\": "
               << (shadowEnabled ? 4 : 2) << ",\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"heightTextureRenderSetComponent\": false,\n"
               << "  \"shadowTextureRenderSetComponent\": false,\n"
               << "  \"staticMaterialTextureRenderSetComponent\": true,\n"
               << "  \"staticMaterialTextureEntityCount\": 14,\n"
               << "  \"duckMeshPackSha256\": \""
               << duckMeshAsset.meshPackSha256.c_str() << "\",\n"
               << "  \"duckTextureSha256\": \""
               << duckMeshAsset.textureSha256.c_str() << "\",\n"
               << "  \"environmentSourceSha256\": \""
               << environmentAsset.sourceSha256.c_str() << "\",\n"
               << "  \"sceneRoots\": [\n"
               << "    {\n"
               << "      \"id\": \"scene\",\n"
               << "      \"renderSetCount\": 1,\n"
               << "      \"renderSetId\": \"scene\",\n"
               << "      \"renderSetType\": \"WebglGpgpuWaterSceneRenderSet\",\n"
               << "      \"renderableObjectCount\": 14,\n"
               << "      \"entityCount\": 14,\n"
               << "      \"drawCommandCount\": 5,\n"
               << "      \"directDrawFallback\": false,\n"
               << "      \"entities\": [\n";
        for (uint32_t entityIndex = 0u; entityIndex < entities.size(); ++entityIndex)
        {
            const GpgpuWaterEntityState &entity = entities[entityIndex];
            output << "        {\"entityId\": " << entity.entityIndex
                   << ", \"logicalRenderableId\": \""
                   << entity.logicalId.c_str()
                   << "\", \"instanceCount\": 1, \"vertexCount\": "
                   << entity.vertices.size() << ", \"indexCount\": "
                   << entity.indices.size()
                   << ", \"materialTextureSlotCount\": 1}";
            output << (entityIndex + 1u < entities.size() ? ",\n" : "\n");
        }
        output << "      ],\n"
               << "      \"componentSchema\": [\n"
               << "        {\"name\": \"vertices\", \"kind\": \"buffer\", \"role\": \"vertex\"},\n"
               << "        {\"name\": \"indices\", \"kind\": \"buffer\", \"role\": \"index\"},\n"
               << "        {\"name\": \"objects\", \"kind\": \"buffer\", \"role\": \"object\"},\n"
               << "        {\"name\": \"instances\", \"kind\": \"buffer\", \"role\": \"instance\"},\n"
               << "        {\"name\": \"materials\", \"kind\": \"buffer\", \"role\": \"material\"},\n"
               << "        {\"name\": \"renderFlags\", \"kind\": \"buffer\", \"role\": \"phase-visibility-shadow-wireframe-texture\"},\n"
               << "        {\"name\": \"textures\", \"kind\": \"texture\", \"role\": \"fixed-per-entity-static-material-texture\"}\n"
               << "      ],\n"
               << "      \"scenePasses\": [\n"
               << "        {\"name\": \"directional-shadow-depth\", \"renderClass\": \"WebglGpgpuWaterShadowPass\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 1, \"drawCommandCount\": 1, \"phaseEnabled\": "
               << (shadowEnabled ? "true" : "false")
               << ", \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false},\n"
               << "        {\"name\": \"main-opaque-pbr\", \"renderClass\": \"WebglGpgpuWaterOpaquePass\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 1, \"drawCommandCount\": 1, \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false},\n"
               << "        {\"name\": \"main-transparent-water-back\", \"renderClass\": \"WebglGpgpuWaterBackPass\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 1, \"drawCommandCount\": 1, \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false},\n"
               << "        {\"name\": \"main-transparent-water-front\", \"renderClass\": \"WebglGpgpuWaterFrontPass\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 1, \"drawCommandCount\": 1, \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false},\n"
               << "        {\"name\": \"main-wireframe-phase\", \"renderClass\": \"WebglGpgpuWaterWireframePass\", \"renderSetId\": \"scene\", \"renderSetBindingCount\": 1, \"drawMode\": \"render-set-indexed-indirect\", \"invocationCount\": 1, \"drawCommandCount\": 1, \"phaseEnabled\": false, \"usesStandaloneGeometry\": false, \"usesExplicitDrawCount\": false}\n"
               << "      ]\n"
               << "    }\n"
               << "  ],\n"
               << "  \"screenPasses\": [\n";
        if (shadowEnabled)
        {
            output << "    {\"name\": \"vsm-shadow-filter-vertical\", \"renderClass\": \"WebglGpgpuWaterVsmVerticalPass\", \"usesRenderSet\": false, \"usesExplicitDrawCount\": true},\n"
                   << "    {\"name\": \"vsm-shadow-filter-horizontal\", \"renderClass\": \"WebglGpgpuWaterVsmHorizontalPass\", \"usesRenderSet\": false, \"usesExplicitDrawCount\": true},\n";
        }
        output << "    {\"name\": \"equirectangular-background\", \"renderClass\": \"WebglGpgpuWaterEnvironmentPass\", \"usesRenderSet\": false, \"usesExplicitDrawCount\": true},\n"
               << "    {\"name\": \"aces-tone-map\", \"renderClass\": \"WebglGpgpuWaterToneMapPass\", \"usesRenderSet\": false, \"usesExplicitDrawCount\": true}\n"
               << "  ],\n"
               << "  \"simulation\": {\"format\": \"rgba32float\", \"implementation\": \"dsl-fragment-ping-pong\", \"tickCount\": "
               << simulationTickCount
               << ", \"currentIndex\": " << (simulationTickCount & 1u)
               << ", \"initialSha256\": \""
               << initialHeightSha256.c_str()
               << "\", \"currentSha256\": \""
               << currentHeightSha256.c_str() << "\"}\n"
               << "}\n";
    }

    void WebglGpgpuWaterRuntimeAdapter::writeLoaderSemanticSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.semanticSnapshotPath.empty() ||
            options.scenarioId != "canonical-loader-snapshot")
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.semanticSnapshotPath.c_str());
        prepareWaterOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_gpgpu_water loader semantic output path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_gpgpu_water\",\n"
               << "  \"scenarioId\": \"canonical-loader-snapshot\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"kind\": \"loader-snapshot\",\n"
               << "  \"canonicalState\": \"duck-glb-one-scene-one-node-one-draco-primitive-one-embedded-texture-hdr-1024x512\",\n"
               << "  \"result\": {\n"
               << "    \"canonicalSceneSha256\": \""
               << duckMeshAsset.meshPackSha256.c_str() << "\",\n"
               << "    \"decoderWasmSha256\": \""
               << DuckDecoderWasmSha256 << "\",\n"
               << "    \"decoderWrapperSha256\": \""
               << DuckDecoderWrapperSha256 << "\",\n"
               << "    \"embeddedTextureCount\": 1,\n"
               << "    \"indexCount\": " << duckMeshAsset.indices.size()
               << ",\n"
               << "    \"nodeCount\": 1,\n"
               << "    \"primitiveCount\": 1,\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"sceneRootCount\": 1,\n"
               << "    \"sourceAssetSha256\": \""
               << DuckSourceGlbSha256 << "\",\n"
               << "    \"textureSha256\": \""
               << duckMeshAsset.textureSha256.c_str() << "\",\n"
               << "    \"vertexCount\": " << duckMeshAsset.vertices.size()
               << "\n"
               << "  }\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
