#include "WebglPostprocessingTransitionRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <CommonCrypto/CommonDigest.h>
#include <compression.h>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneSetAHandle =
            ExportedRenderSet::sceneSetA;
        constexpr GVM::Core::RenderSetHandle SceneSetBHandle =
            ExportedRenderSet::sceneSetB;
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t RenderableCount = 2u;
        constexpr uint32_t InstanceCount = 500u;
        constexpr uint32_t TransitionTextureExtent = 512u;
        constexpr const char *TransitionTextureSha256 =
            "074683a5a4a5240146db2a3ca5eddba98ed7da0cbe2917b36a89b389d8abb3fa";

        static_assert(sizeof(WebglPostprocessingTransitionVertex) == 32u);
        static_assert(sizeof(WebglPostprocessingTransitionHostObjectData) == 192u);
        static_assert(sizeof(WebglPostprocessingTransitionHostInstanceData) == 80u);
        static_assert(sizeof(WebglPostprocessingTransitionHostMaterialData) == 32u);

        /** Creates parent directories for one requested evidence path. */
        void prepareWebglPostprocessingTransitionOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeWebglPostprocessingTransitionText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebglPostprocessingTransitionOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGL postprocessing evidence.");
        }

        /** Reads one bounded transition asset into immutable bytes. */
        eastl::vector<uint8_t> readWebglPostprocessingTransitionAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open transition6.png: " + path.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) >
                                std::numeric_limits<size_t>::max())
            {
                throw std::runtime_error(
                    "transition6.png has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete transition6.png asset.");
            }
            return bytes;
        }

        /** Reads one network-order uint32 from a validated PNG byte offset. */
        uint32_t readWebglPostprocessingTransitionBigEndianUint32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error(
                    "transition6.png has a truncated uint32 field.");
            }
            return (uint32_t(bytes[offset]) << 24u) |
                   (uint32_t(bytes[offset + 1u]) << 16u) |
                   (uint32_t(bytes[offset + 2u]) << 8u) |
                   uint32_t(bytes[offset + 3u]);
        }

        /** Returns PNG's Paeth predictor for one filtered byte. */
        uint8_t predictWebglPostprocessingTransitionPngPaeth(
            uint8_t left,
            uint8_t above,
            uint8_t upperLeft)
        {
            const int leftValue = int(left);
            const int aboveValue = int(above);
            const int upperLeftValue = int(upperLeft);
            const int prediction = leftValue + aboveValue - upperLeftValue;
            const int leftDistance = std::abs(prediction - leftValue);
            const int aboveDistance = std::abs(prediction - aboveValue);
            const int upperLeftDistance = std::abs(prediction - upperLeftValue);
            if (leftDistance <= aboveDistance && leftDistance <= upperLeftDistance)
                return left;
            if (aboveDistance <= upperLeftDistance)
                return above;
            return upperLeft;
        }

        /** Decodes the pinned noninterlaced RGB8 transition PNG to flipped RGBA8. */
        eastl::vector<uint8_t> decodeWebglPostprocessingTransitionPng(
            const eastl::vector<uint8_t> &pngBytes)
        {
            constexpr eastl::array<uint8_t, 8u> PngSignature = {
                0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
            if (pngBytes.size() < PngSignature.size() ||
                !eastl::equal(
                    PngSignature.begin(), PngSignature.end(), pngBytes.begin()))
            {
                throw std::runtime_error(
                    "transition6.png has an invalid PNG signature.");
            }
            eastl::vector<uint8_t> compressed;
            uint32_t width = 0u;
            uint32_t height = 0u;
            bool ihdrSeen = false;
            bool iendSeen = false;
            size_t offset = PngSignature.size();
            while (offset < pngBytes.size())
            {
                if (pngBytes.size() - offset < 12u)
                {
                    throw std::runtime_error(
                        "transition6.png has a truncated chunk header.");
                }
                const uint32_t payloadSize =
                    readWebglPostprocessingTransitionBigEndianUint32(
                        pngBytes, offset);
                const size_t payloadOffset = offset + 8u;
                if (payloadSize > pngBytes.size() - payloadOffset - 4u)
                {
                    throw std::runtime_error(
                        "transition6.png has a truncated chunk payload.");
                }
                const char type0 = char(pngBytes[offset + 4u]);
                const char type1 = char(pngBytes[offset + 5u]);
                const char type2 = char(pngBytes[offset + 6u]);
                const char type3 = char(pngBytes[offset + 7u]);
                if (type0 == 'I' && type1 == 'H' && type2 == 'D' && type3 == 'R')
                {
                    if (ihdrSeen || payloadSize != 13u)
                    {
                        throw std::runtime_error(
                            "transition6.png has a duplicate or invalid IHDR.");
                    }
                    width = readWebglPostprocessingTransitionBigEndianUint32(
                        pngBytes, payloadOffset);
                    height = readWebglPostprocessingTransitionBigEndianUint32(
                        pngBytes, payloadOffset + 4u);
                    if (width != TransitionTextureExtent ||
                        height != TransitionTextureExtent ||
                        pngBytes[payloadOffset + 8u] != 8u ||
                        pngBytes[payloadOffset + 9u] != 2u ||
                        pngBytes[payloadOffset + 10u] != 0u ||
                        pngBytes[payloadOffset + 11u] != 0u ||
                        pngBytes[payloadOffset + 12u] != 0u)
                    {
                        throw std::runtime_error(
                            "transition6.png must remain 512x512 noninterlaced RGB8.");
                    }
                    ihdrSeen = true;
                }
                else if (type0 == 'I' && type1 == 'D' && type2 == 'A' && type3 == 'T')
                {
                    compressed.insert(
                        compressed.end(),
                        pngBytes.begin() + payloadOffset,
                        pngBytes.begin() + payloadOffset + payloadSize);
                }
                else if (type0 == 'I' && type1 == 'E' && type2 == 'N' && type3 == 'D')
                {
                    iendSeen = true;
                    break;
                }
                offset = payloadOffset + payloadSize + 4u;
            }
            if (!ihdrSeen || !iendSeen || compressed.size() <= 6u)
            {
                throw std::runtime_error(
                    "transition6.png lacks its required PNG chunks.");
            }
            const uint32_t zlibHeader =
                uint32_t(compressed[0u]) * 256u + uint32_t(compressed[1u]);
            if ((compressed[0u] & 0x0fu) != 8u || zlibHeader % 31u != 0u)
            {
                throw std::runtime_error(
                    "transition6.png has an unsupported zlib stream.");
            }
            const size_t rowByteCount = static_cast<size_t>(width) * 3u;
            const size_t filteredByteCount =
                static_cast<size_t>(height) * (rowByteCount + 1u);
            eastl::vector<uint8_t> filtered(filteredByteCount);
            const size_t decodedSize = compression_decode_buffer(
                filtered.data(), filtered.size(), compressed.data() + 2u,
                compressed.size() - 6u, nullptr, COMPRESSION_ZLIB);
            if (decodedSize != filteredByteCount)
            {
                throw std::runtime_error(
                    "Could not inflate the complete transition6.png RGB payload.");
            }
            eastl::vector<uint8_t> rgb(filteredByteCount - height);
            for (uint32_t y = 0u; y < height; ++y)
            {
                const size_t filteredRow = static_cast<size_t>(y) * (rowByteCount + 1u);
                const uint8_t filter = filtered[filteredRow];
                if (filter > 4u)
                {
                    throw std::runtime_error(
                        "transition6.png uses an unsupported PNG row filter.");
                }
                for (size_t x = 0u; x < rowByteCount; ++x)
                {
                    const uint8_t source = filtered[filteredRow + 1u + x];
                    const uint8_t left = x >= 3u
                        ? rgb[static_cast<size_t>(y) * rowByteCount + x - 3u]
                        : 0u;
                    const uint8_t above = y > 0u
                        ? rgb[(static_cast<size_t>(y) - 1u) * rowByteCount + x]
                        : 0u;
                    const uint8_t upperLeft = y > 0u && x >= 3u
                        ? rgb[(static_cast<size_t>(y) - 1u) * rowByteCount + x - 3u]
                        : 0u;
                    uint8_t predictor = 0u;
                    if (filter == 1u) predictor = left;
                    else if (filter == 2u) predictor = above;
                    else if (filter == 3u)
                        predictor = uint8_t((uint32_t(left) + uint32_t(above)) / 2u);
                    else if (filter == 4u)
                        predictor = predictWebglPostprocessingTransitionPngPaeth(
                            left, above, upperLeft);
                    rgb[static_cast<size_t>(y) * rowByteCount + x] =
                        uint8_t(uint32_t(source) + uint32_t(predictor));
                }
            }
            eastl::vector<uint8_t> rgba(
                static_cast<size_t>(width) * height * 4u);
            for (uint32_t destinationY = 0u; destinationY < height; ++destinationY)
            {
                const uint32_t sourceY = destinationY;
                for (uint32_t x = 0u; x < width; ++x)
                {
                    const size_t sourceOffset =
                        (static_cast<size_t>(sourceY) * width + x) * 3u;
                    const size_t destinationOffset =
                        (static_cast<size_t>(destinationY) * width + x) * 4u;
                    rgba[destinationOffset] = rgb[sourceOffset];
                    rgba[destinationOffset + 1u] = rgb[sourceOffset + 1u];
                    rgba[destinationOffset + 2u] = rgb[sourceOffset + 2u];
                    rgba[destinationOffset + 3u] = 255u;
                }
            }
            return rgba;
        }

        /** Verifies one pinned transition asset before it reaches the GPU. */
        void verifyWebglPostprocessingTransitionAsset(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    "transition6.png is too large for SHA-256 validation.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string actual;
            actual.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                actual.push_back(HexDigits[value >> 4u]);
                actual.push_back(HexDigits[value & 0x0fu]);
            }
            if (actual != TransitionTextureSha256)
            {
                throw std::runtime_error(
                    "transition6.png SHA-256 does not match the locked r185 asset.");
            }
        }

        /** Returns the canonical replay filename for one transition scenario. */
        const char *webglPostprocessingTransitionReplayFile(
            const eastl::string &scenarioId)
        {
            if (scenarioId == "mid-transition")
                return "webgl_postprocessing_transition_midpoint.json";
            if (scenarioId == "endpoint-scene-a")
                return "webgl_postprocessing_transition_endpoint_a.json";
            if (scenarioId == "animated-no-texture")
                return "webgl_postprocessing_transition_no_texture.json";
            return nullptr;
        }

        /** Returns the locked SHA-256 for one canonical transition replay. */
        const char *webglPostprocessingTransitionReplaySha256(
            const eastl::string &scenarioId)
        {
            if (scenarioId == "mid-transition")
                return "0c1b18d181ddea7285d393300aab1319b49ae950149ba53b687d6ee1e8d23baa";
            if (scenarioId == "endpoint-scene-a")
                return "862a965514a17cc2533108a7c377f9918d22d308cd2f3c72e6f8e7a31ff8eb97";
            if (scenarioId == "animated-no-texture")
                return "92af052c7d5fc307538489904ab70e753c654c03e0db67752f694615d04933ac";
            return nullptr;
        }

        /** Calculates a lowercase SHA-256 for a bounded replay JSON file. */
        std::string calculateWebglPostprocessingTransitionReplaySha256(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error(
                    "Could not open the WebGL postprocessing transition replay.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                static_cast<uint64_t>(byteCount) >
                    static_cast<uint64_t>(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error(
                    "The WebGL postprocessing transition replay has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                byteCount);
            if (!input)
                throw std::runtime_error(
                    "Could not read the WebGL postprocessing transition replay.");
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            std::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Resolves the transition texture from either accepted asset-root layout. */
        std::filesystem::path resolveWebglPostprocessingTransitionTexture(
            const eastl::string &assetRoot)
        {
            const std::filesystem::path root(assetRoot.c_str());
            const std::filesystem::path direct =
                root / "textures" / "transition" / "transition6.png";
            if (std::filesystem::exists(direct)) return direct;
            const std::filesystem::path nested =
                root / "examples" / "textures" / "transition" / "transition6.png";
            if (std::filesystem::exists(nested)) return nested;
            const std::filesystem::path lockedValidationAsset =
                root.parent_path() / "three-r185-status-validation" / "examples"
                / "textures" / "transition" / "transition6.png";
            if (std::filesystem::exists(lockedValidationAsset)) return lockedValidationAsset;
            throw std::runtime_error(
                "Could not resolve textures/transition/transition6.png from --asset-root.");
        }

        /** Advances the exact upper-24-bit xorshift32 reference stream. */
        double nextWebglPostprocessingTransitionRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return double(state >> 8u) / 16777216.0;
        }

        /** Builds Three's intrinsic XYZ Euler rotation in binary64. */
        glm::dmat4 makeWebglPostprocessingTransitionRotation(
            double rotationX,
            double rotationY,
            double rotationZ)
        {
            const double cx = std::cos(rotationX * 0.5);
            const double cy = std::cos(rotationY * 0.5);
            const double cz = std::cos(rotationZ * 0.5);
            const double sx = std::sin(rotationX * 0.5);
            const double sy = std::sin(rotationY * 0.5);
            const double sz = std::sin(rotationZ * 0.5);
            const double qx = sx * cy * cz + cx * sy * sz;
            const double qy = cx * sy * cz - sx * cy * sz;
            const double qz = cx * cy * sz + sx * sy * cz;
            const double qw = cx * cy * cz - sx * sy * sz;
            const double x2 = qx + qx;
            const double y2 = qy + qy;
            const double z2 = qz + qz;
            const double xx = qx * x2;
            const double xy = qx * y2;
            const double xz = qx * z2;
            const double yy = qy * y2;
            const double yz = qy * z2;
            const double zz = qz * z2;
            const double wx = qw * x2;
            const double wy = qw * y2;
            const double wz = qw * z2;
            glm::dmat4 result(1.0);
            result[0u] = glm::dvec4(
                1.0 - yy - zz, xy + wz, xz - wy, 0.0);
            result[1u] = glm::dvec4(
                xy - wz, 1.0 - xx - zz, yz + wx, 0.0);
            result[2u] = glm::dvec4(
                xz + wy, yz - wx, 1.0 - xx - yy, 0.0);
            return result;
        }

        /** Builds the r185 50-degree perspective projection in binary64. */
        glm::dmat4 makeWebglPostprocessingTransitionProjection()
        {
            constexpr double Near = 0.1;
            constexpr double Far = 100.0;
            const double inverseTangent =
                1.0 / std::tan(50.0 * Pi / 360.0);
            glm::dmat4 result(0.0);
            result[0u][0u] = inverseTangent / 1.6;
            result[1u][1u] = inverseTangent;
            result[2u][2u] = (Far + Near) / (Near - Far);
            result[2u][3u] = -1.0;
            result[3u][2u] = 2.0 * Far * Near / (Near - Far);
            return result;
        }

        /** Appends one expanded triangle with its exact flat face normal. */
        void appendWebglPostprocessingTransitionTriangle(
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            eastl::vector<WebglPostprocessingTransitionVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const glm::dvec3 normal = glm::normalize(glm::cross(b - a, c - a));
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({glm::vec4(glm::vec3(a), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(b), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(c), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f)});
            indices.insert(indices.end(), {base, base + 1u, base + 2u});
        }

        /** Appends one exact one-segment BoxGeometry plane in r185 vertex order. */
        void appendWebglPostprocessingTransitionBoxPlane(
            const uint32_t (&corners)[4u],
            const glm::dvec3 &normal,
            const glm::dvec3 (&positions)[8u],
            eastl::vector<WebglPostprocessingTransitionVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            for (const uint32_t corner : corners)
            {
                vertices.push_back({
                    glm::vec4(glm::vec3(positions[corner]), 1.0f),
                    glm::vec4(glm::vec3(normal), 0.0f)});
            }
            indices.insert(indices.end(), {
                base, base + 2u, base + 1u,
                base + 2u, base + 3u, base + 1u});
        }

        /** Builds Three's exact default-segment BoxGeometry triangle topology. */
        void buildWebglPostprocessingTransitionBox(
            eastl::vector<WebglPostprocessingTransitionVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.clear();
            indices.clear();
            const double h = 1.0;
            const glm::dvec3 p[8u] = {
                {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
                {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
            // These corner orders and the diagonal (a,b,d)+(b,c,d) are the
            // exact output of BoxGeometry.buildPlane() for one segment.  A
            // different diagonal is geometrically coplanar but changes the
            // deterministic coverage of one flat-shaded face at the image
            // gate, so the upstream topology is retained verbatim.
            constexpr uint32_t px[4u] = {6u, 2u, 5u, 1u};
            constexpr uint32_t nx[4u] = {3u, 7u, 0u, 4u};
            constexpr uint32_t py[4u] = {3u, 2u, 7u, 6u};
            constexpr uint32_t ny[4u] = {4u, 5u, 0u, 1u};
            constexpr uint32_t pz[4u] = {4u, 5u, 7u, 6u};
            constexpr uint32_t nz[4u] = {1u, 0u, 2u, 3u};
            appendWebglPostprocessingTransitionBoxPlane(
                px, glm::dvec3(1.0, 0.0, 0.0), p, vertices, indices);
            appendWebglPostprocessingTransitionBoxPlane(
                nx, glm::dvec3(-1.0, 0.0, 0.0), p, vertices, indices);
            appendWebglPostprocessingTransitionBoxPlane(
                py, glm::dvec3(0.0, 1.0, 0.0), p, vertices, indices);
            appendWebglPostprocessingTransitionBoxPlane(
                ny, glm::dvec3(0.0, -1.0, 0.0), p, vertices, indices);
            appendWebglPostprocessingTransitionBoxPlane(
                pz, glm::dvec3(0.0, 0.0, 1.0), p, vertices, indices);
            appendWebglPostprocessingTransitionBoxPlane(
                nz, glm::dvec3(0.0, 0.0, -1.0), p, vertices, indices);
            if (vertices.size() != 24u || indices.size() != 36u)
                throw std::runtime_error(
                    "WebGL postprocessing BoxGeometry topology differs from r185.");
        }

        /** Builds a detail-one IcosahedronGeometry by normalized midpoint subdivision. */
        void buildWebglPostprocessingTransitionIcosahedron(
            eastl::vector<WebglPostprocessingTransitionVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const double phi = (1.0 + std::sqrt(5.0)) * 0.5;
            eastl::vector<glm::dvec3> base = {
                {-1.0, phi, 0.0}, {1.0, phi, 0.0}, {-1.0, -phi, 0.0},
                {1.0, -phi, 0.0}, {0.0, -1.0, phi}, {0.0, 1.0, phi},
                {0.0, -1.0, -phi}, {0.0, 1.0, -phi},
                {phi, 0.0, -1.0}, {phi, 0.0, 1.0},
                {-phi, 0.0, -1.0}, {-phi, 0.0, 1.0}};
            for (glm::dvec3 &point : base) point = glm::normalize(point);
            constexpr uint32_t faceIndices[20u][3u] = {
                {0u, 11u, 5u}, {0u, 5u, 1u}, {0u, 1u, 7u}, {0u, 7u, 10u},
                {0u, 10u, 11u}, {1u, 5u, 9u}, {5u, 11u, 4u},
                {11u, 10u, 2u}, {10u, 7u, 6u}, {7u, 1u, 8u},
                {3u, 9u, 4u}, {3u, 4u, 2u}, {3u, 2u, 6u},
                {3u, 6u, 8u}, {3u, 8u, 9u}, {4u, 9u, 5u},
                {2u, 4u, 11u}, {6u, 2u, 10u}, {8u, 6u, 7u},
                {9u, 8u, 1u}};
            vertices.clear();
            indices.clear();
            for (const auto &face : faceIndices)
            {
                const glm::dvec3 &a = base[face[0u]];
                const glm::dvec3 &b = base[face[1u]];
                const glm::dvec3 &c = base[face[2u]];
                const glm::dvec3 ab = glm::normalize(a + b);
                const glm::dvec3 bc = glm::normalize(b + c);
                const glm::dvec3 ca = glm::normalize(c + a);
                // Match PolyhedronGeometry.subdivideFace() in r185: the
                // four triangles are emitted in this order and share the
                // same three normalized edge midpoints.
                appendWebglPostprocessingTransitionTriangle(
                    ab, ca, a, vertices, indices);
                appendWebglPostprocessingTransitionTriangle(
                    ab, bc, ca, vertices, indices);
                appendWebglPostprocessingTransitionTriangle(
                    b, bc, ab, vertices, indices);
                appendWebglPostprocessingTransitionTriangle(
                    bc, c, ca, vertices, indices);
            }
            if (vertices.size() != 240u || indices.size() != 240u)
                throw std::runtime_error(
                    "WebGL postprocessing IcosahedronGeometry topology differs from r185.");
        }

        /** Appends one typed component payload to a Scene entity allocation. */
        void appendWebglPostprocessingTransitionPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount = 1u)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }
    }

    void WebglPostprocessingTransitionRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool scenario0 = options.scenarioId == "initial-scene-b" && options.targetFrame == 0u;
        const bool scenario1 = options.scenarioId == "mid-transition" && options.targetFrame == 1u;
        const bool scenario2 = options.scenarioId == "endpoint-scene-a" && options.targetFrame == 1u;
        const bool scenario3 = options.scenarioId == "animated-no-texture" && options.targetFrame == 120u;
        const bool scenarioUsesReplay = scenario1 || scenario2 || scenario3;
        if (options.caseId != "webgl_postprocessing_transition" ||
            !(scenario0 || scenario1 || scenario2 || scenario3) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            (scenarioUsesReplay != !options.inputReplayPath.empty()) ||
            options.assetRoot.empty())
        {
            throw std::invalid_argument("webgl_postprocessing_transition requires its locked r185 scenario matrix.");
        }
        if (scenarioUsesReplay)
        {
            const char *expectedFile =
                webglPostprocessingTransitionReplayFile(options.scenarioId);
            const char *expectedSha256 =
                webglPostprocessingTransitionReplaySha256(options.scenarioId);
            const std::filesystem::path replayPath(options.inputReplayPath.c_str());
            if (expectedFile == nullptr || expectedSha256 == nullptr ||
                replayPath.filename() != expectedFile ||
                calculateWebglPostprocessingTransitionReplaySha256(replayPath) !=
                    expectedSha256)
            {
                throw std::invalid_argument(
                    "WebGL postprocessing transition replay does not match its locked r185 scenario.");
            }
        }
        device = inDevice;
        const std::filesystem::path transitionTexturePath =
            resolveWebglPostprocessingTransitionTexture(options.assetRoot);
        const eastl::vector<uint8_t> transitionTextureBytes =
            readWebglPostprocessingTransitionAsset(transitionTexturePath);
        verifyWebglPostprocessingTransitionAsset(transitionTextureBytes);
        transitionTexturePixels = decodeWebglPostprocessingTransitionPng(
            transitionTextureBytes);
        if (transitionTexturePixels.size() !=
            static_cast<size_t>(TransitionTextureExtent) *
                TransitionTextureExtent * 4u)
        {
            throw std::runtime_error(
                "Decoded transition6.png has an unexpected RGBA8 size.");
        }
        buildWebglPostprocessingTransitionBox(verticesA, indicesA);
        buildWebglPostprocessingTransitionIcosahedron(verticesB, indicesB);
        objects.resize(RenderableCount);
        instanceDataA.resize(InstanceCount);
        instanceDataB.resize(InstanceCount);
        uint32_t randomState = options.randomSeed;
        /* The r185 page consumes 124 seeded samples before the first
           InstancedMesh transform.  Forty additional UUID/resource samples
           occur between the Box and Icosahedron mesh construction calls. */
        for (uint32_t draw = 0u; draw < 124u; ++draw)
            (void)nextWebglPostprocessingTransitionRandom(randomState);
        // THREE.Timer accumulates one 60 Hz step for each callback after the
        // initial zero-delta callback in the deterministic reference clock.
        const double renderedTime = double(options.targetFrame) / 60.0;
        const glm::dmat4 sceneRotationA = makeWebglPostprocessingTransitionRotation(
            0.0, -0.4 * renderedTime, 0.0);
        const glm::dmat4 sceneRotationB = makeWebglPostprocessingTransitionRotation(
            0.0, 0.2 * renderedTime, 0.1 * renderedTime);
        glm::dmat4 view(1.0);
        view[3u][2u] = -20.0;
        const glm::dmat4 projection = makeWebglPostprocessingTransitionProjection();
        const glm::dmat4 viewA = view * sceneRotationA;
        const glm::dmat4 viewB = view * sceneRotationB;
        objects[0].modelView = glm::mat4(viewA);
        objects[0].modelViewProjection = glm::mat4(projection);
        objects[0].normalModelView = glm::mat4(
            glm::transpose(glm::inverse(viewA)));
        objects[1].modelView = glm::mat4(viewB);
        objects[1].modelViewProjection = glm::mat4(projection);
        objects[1].normalModelView = glm::mat4(
            glm::transpose(glm::inverse(viewB)));
        for (uint32_t index = 0u; index < InstanceCount; ++index)
        {
            const glm::dvec3 position(
                nextWebglPostprocessingTransitionRandom(randomState) * 100.0 - 50.0,
                nextWebglPostprocessingTransitionRandom(randomState) * 60.0 - 30.0,
                nextWebglPostprocessingTransitionRandom(randomState) * 80.0 - 40.0);
            const glm::dvec3 rotation(
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 * Pi,
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 * Pi,
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 * Pi);
            const double scaleX =
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 + 1.0;
            const double scaleY =
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 + 1.0;
            const double scaleZ =
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 + 1.0;
            const float color = static_cast<float>(
                0.1 + 0.9 * nextWebglPostprocessingTransitionRandom(randomState));
            glm::dmat4 model = makeWebglPostprocessingTransitionRotation(
                rotation.x, rotation.y, rotation.z);
            model[0u] *= scaleX;
            model[1u] *= scaleY;
            model[2u] *= scaleZ;
            model[3u] = glm::dvec4(position, 1.0);
            instanceDataA[index].model = glm::mat4(model);
            instanceDataA[index].color = glm::vec4(color);
        }
        for (uint32_t draw = 0u; draw < 40u; ++draw)
            (void)nextWebglPostprocessingTransitionRandom(randomState);
        for (uint32_t index = 0u; index < InstanceCount; ++index)
        {
            const glm::dvec3 position(
                nextWebglPostprocessingTransitionRandom(randomState) * 100.0 - 50.0,
                nextWebglPostprocessingTransitionRandom(randomState) * 60.0 - 30.0,
                nextWebglPostprocessingTransitionRandom(randomState) * 80.0 - 40.0);
            const glm::dvec3 rotation(
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 * Pi,
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 * Pi,
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 * Pi);
            const double scaleX =
                nextWebglPostprocessingTransitionRandom(randomState) * 2.0 + 1.0;
            const float color = static_cast<float>(
                0.1 + 0.9 * nextWebglPostprocessingTransitionRandom(randomState));
            glm::dmat4 icosaModel = makeWebglPostprocessingTransitionRotation(
                rotation.x, rotation.y, rotation.z);
            icosaModel[0u] *= scaleX;
            icosaModel[1u] *= scaleX;
            icosaModel[2u] *= scaleX;
            icosaModel[3u] = glm::dvec4(position, 1.0);
            instanceDataB[index].model = glm::mat4(icosaModel);
            instanceDataB[index].color = glm::vec4(color);
        }
        materialDataA.diffuseAndShininess = glm::vec4(0.0f, 0.0f, 1.0f, 30.0f);
        materialDataA.specular = glm::vec4(0.0056053917f);
        materialDataB.diffuseAndShininess = glm::vec4(1.0f, 0.0f, 0.0f, 30.0f);
        materialDataB.specular = glm::vec4(0.0056053917f);
        const auto encoderA = renderer.createRenderSetCommandEncoder(
            SceneSetAHandle);
        const auto encoderB = renderer.createRenderSetCommandEncoder(
            SceneSetBHandle);
        if (!encoderA || !encoderB)
            throw std::runtime_error(
                "Could not create the WebGL postprocessing Scene Set encoders.");
        GVM::Core::RenderSetAllocInfo allocationA;
        allocationA.verticesCount = static_cast<uint32_t>(verticesA.size());
        allocationA.indicesCount = static_cast<uint32_t>(indicesA.size());
        allocationA.instanceCount = InstanceCount;
        appendWebglPostprocessingTransitionPayload(
            allocationA, WebglPostprocessingTransitionSceneRenderSetComponents::vertices,
            "WebglPostprocessingTransitionBoxVertices", verticesA.data(),
            verticesA.size() * sizeof(verticesA[0u]));
        appendWebglPostprocessingTransitionPayload(
            allocationA, WebglPostprocessingTransitionSceneRenderSetComponents::indices,
            "WebglPostprocessingTransitionBoxIndices", indicesA.data(),
            indicesA.size() * sizeof(indicesA[0u]));
        appendWebglPostprocessingTransitionPayload(
            allocationA, WebglPostprocessingTransitionSceneRenderSetComponents::objects,
            "WebglPostprocessingTransitionBoxObject", &objects[0], sizeof(objects[0]));
        appendWebglPostprocessingTransitionPayload(
            allocationA, WebglPostprocessingTransitionSceneRenderSetComponents::instances,
            "WebglPostprocessingTransitionBoxInstances", instanceDataA.data(),
            instanceDataA.size() * sizeof(instanceDataA[0u]), InstanceCount);
        appendWebglPostprocessingTransitionPayload(
            allocationA, WebglPostprocessingTransitionSceneRenderSetComponents::materials,
            "WebglPostprocessingTransitionBoxMaterial", &materialDataA, sizeof(materialDataA));
        encoderA->allocEntity(allocationA);

        GVM::Core::RenderSetAllocInfo allocationB;
        allocationB.verticesCount = static_cast<uint32_t>(verticesB.size());
        allocationB.indicesCount = static_cast<uint32_t>(indicesB.size());
        allocationB.instanceCount = InstanceCount;
        appendWebglPostprocessingTransitionPayload(
            allocationB, WebglPostprocessingTransitionSceneRenderSetComponents::vertices,
            "WebglPostprocessingTransitionIcosahedronVertices", verticesB.data(),
            verticesB.size() * sizeof(verticesB[0u]));
        appendWebglPostprocessingTransitionPayload(
            allocationB, WebglPostprocessingTransitionSceneRenderSetComponents::indices,
            "WebglPostprocessingTransitionIcosahedronIndices", indicesB.data(),
            indicesB.size() * sizeof(indicesB[0u]));
        appendWebglPostprocessingTransitionPayload(
            allocationB, WebglPostprocessingTransitionSceneRenderSetComponents::objects,
            "WebglPostprocessingTransitionIcosahedronObject", &objects[1], sizeof(objects[1]));
        appendWebglPostprocessingTransitionPayload(
            allocationB, WebglPostprocessingTransitionSceneRenderSetComponents::instances,
            "WebglPostprocessingTransitionIcosahedronInstances", instanceDataB.data(),
            instanceDataB.size() * sizeof(instanceDataB[0u]), InstanceCount);
        appendWebglPostprocessingTransitionPayload(
            allocationB, WebglPostprocessingTransitionSceneRenderSetComponents::materials,
            "WebglPostprocessingTransitionIcosahedronMaterial", &materialDataB, sizeof(materialDataB));
        encoderB->allocEntity(allocationB);
        renderer.executeRenderSetCommand(SceneSetAHandle, encoderA);
        renderer.executeRenderSetCommand(SceneSetBHandle, encoderB);
    }

    void WebglPostprocessingTransitionRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPostprocessingTransitionRuntimeAdapter::afterFrame(
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
            prepareWebglPostprocessingTransitionOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGL postprocessing RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgl_postprocessing_transition\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\",\"sampleCount\":1"
            << ",\"samplePolicy\":{"
            << "\"mode\":\"single-sample\",\"msaaEnabled\":false,"
            << "\"simulateMsaa\":false},\"inputReplay\":";
        if (options.inputReplayPath.empty())
        {
            metadata << "null";
        }
        else
        {
            const std::string replaySha256 =
                calculateWebglPostprocessingTransitionReplaySha256(
                    std::filesystem::path(options.inputReplayPath.c_str()));
            metadata
                << "{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing_transition\","
                << "\"scenarioId\":\"" << options.scenarioId.c_str()
                << "\",\"captureFrame\":" << frameIndex
                << ",\"sha256\":\"" << replaySha256
                << "\",\"target\":\"body > canvas\",\"eventCount\":1,"
                << "\"lastEventFrame\":0}";
        }
        metadata << ",\"gpuWorkDslOnly\":true}\n";
        writeWebglPostprocessingTransitionText(
            options.captureMetadataPath, metadata.str());
        const auto componentSchema =
            "[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}]";
        const bool drawSceneA = options.scenarioId != "initial-scene-b";
        const bool drawSceneB = options.scenarioId != "endpoint-scene-a";
        const uint32_t scenePassCount =
            uint32_t(drawSceneA) + uint32_t(drawSceneB);
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing_transition\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":2,\"renderableObjectCount\":2,"
            << "\"entityCount\":2,\"instanceCount\":" << InstanceCount * RenderableCount
            << ",\"instanceCounts\":[" << InstanceCount << "," << InstanceCount << "],"
            << "\"vertexCount\":" << verticesA.size() + verticesB.size()
            << ",\"indexCount\":" << indicesA.size() + indicesB.size()
            << ",\"scenePassCount\":" << scenePassCount
            << ",\"screenPassCount\":3,"
            << "\"drawCommandCount\":" << scenePassCount
            << ",\"renderSetType\":"
            << "\"WebglPostprocessingTransitionSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"fxSceneA\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-a-set\",\"renderSetType\":"
            << "\"WebglPostprocessingTransitionSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"box-a\",\"instanceCount\":" << InstanceCount << "}],"
            << "\"componentSchema\":" << componentSchema << ","
            << "\"drawCommandCount\":" << uint32_t(drawSceneA)
            << ",\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"scene-a\",\"renderClass\":"
            << "\"WebglPostprocessingTransitionSceneAPass\",\"renderSetId\":\"scene-a-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":"
            << uint32_t(drawSceneA) << ","
            << "\"drawCommandCount\":" << uint32_t(drawSceneA)
            << ",\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]},{"
            << "\"id\":\"fxSceneB\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-b-set\",\"renderSetType\":"
            << "\"WebglPostprocessingTransitionSceneRenderSet\","
            << "\"renderableObjectCount\":1,\"entityCount\":1,"
            << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"icosahedron-b\",\"instanceCount\":" << InstanceCount << "}],"
            << "\"componentSchema\":" << componentSchema << ","
            << "\"drawCommandCount\":" << uint32_t(drawSceneB)
            << ",\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"scene-b\",\"renderClass\":"
            << "\"WebglPostprocessingTransitionSceneBPass\",\"renderSetId\":\"scene-b-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":"
            << uint32_t(drawSceneB) << ","
            << "\"drawCommandCount\":" << uint32_t(drawSceneB)
            << ",\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[";
        bool firstSequenceEntry = true;
        if (drawSceneA)
        {
            snapshot << "{\"sceneRoot\":\"fxSceneA\","
                     << "\"scenePass\":\"scene-a\",\"entityOrdinal\":0}";
            firstSequenceEntry = false;
        }
        if (drawSceneB)
        {
            if (!firstSequenceEntry) snapshot << ",";
            snapshot << "{\"sceneRoot\":\"fxSceneB\","
                     << "\"scenePass\":\"scene-b\",\"entityOrdinal\":0}";
        }
        snapshot << "]}\n";
        writeWebglPostprocessingTransitionText(
            options.sceneSnapshotPath, snapshot.str());
        writeWebglPostprocessingTransitionText(
            options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebglPostprocessingTransitionRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        verticesA.clear();
        indicesA.clear();
        verticesB.clear();
        indicesB.clear();
        objects.clear();
        instanceDataA.clear();
        instanceDataB.clear();
        transitionTexturePixels.clear();
        device = {};
    }
}
