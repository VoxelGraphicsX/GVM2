#include "WebglCustomAttributesPoints3RuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <compression.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/unordered_map.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t Points3RandomSeed = 0x18500014u;
        constexpr uint32_t RandomCandidateCount = 100000u;
        constexpr uint32_t ShellPointCount = 78435u;
        constexpr uint32_t BoxGeometry1PointCount = 1052u;
        constexpr uint32_t BoxGeometry1CopyCount = 8u;
        constexpr uint32_t BoxGeometry2PointCount = 1252u;
        constexpr uint32_t BoxGeometry2CopyCount = 4u;
        constexpr uint32_t BoxEdgePointCount =
            BoxGeometry1PointCount * BoxGeometry1CopyCount +
            BoxGeometry2PointCount * BoxGeometry2CopyCount;
        constexpr uint32_t LogicalPointCount =
            ShellPointCount + BoxEdgePointCount;
        constexpr uint32_t ExpandedVertexCount = LogicalPointCount * 4u;
        constexpr uint32_t ExpandedIndexCount = LogicalPointCount * 6u;
        constexpr uint32_t BallTextureExtent = 64u;
        constexpr uint32_t BallTextureMipCount = 7u;
        constexpr uint32_t TotalRandomDrawCount = 300156u;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *BallAssetSha256 =
            "6dd1bf340dc56432cf44feeff5d79480f9dcd3397fd1872f16c98c0e56af7f3b";
        constexpr const char *FlippedBallBaseSha256 =
            "470558b2070eb9ddc6b1d76c3bd1f22d8104a0f0e56448a7bc1c13edd7073eb0";
        constexpr const char *PositionSha256 =
            "aecf4ba034095bb51cb8973811da2ebe1a86ae3a7e9cb646276c52620332c1f4";
        constexpr const char *ColorSha256 =
            "16188fb73af79ca5a4a4806476ed115b23f2915b1b8c84973b0411872c0b8eb3";
        constexpr const char *InitialSizeSha256 =
            "336b6cbb84741b37d2305b61b1d9a359120d25f2bd8a5cf86a3ed7f59e0a5de0";
        constexpr const char *AnimatedSizeSha256 =
            "510afd7c2d0f4ee12abd858fd10f67c673b0d17f4e4135d499164a4128681565";

        using Points3Matrix = eastl::array<double, 16u>;

        static_assert(sizeof(WebglCustomAttributesPoints3Vertex) == 36u);
        static_assert(offsetof(WebglCustomAttributesPoints3Vertex, position) == 0u);
        static_assert(offsetof(WebglCustomAttributesPoints3Vertex, customColor) == 12u);
        static_assert(offsetof(WebglCustomAttributesPoints3Vertex, corner) == 28u);
        static_assert(sizeof(glm::vec3) == 12u);
        static_assert(sizeof(glm::vec4) == 16u);
        static_assert(BoxEdgePointCount == 13424u);
        static_assert(LogicalPointCount == 91859u);
        static_assert(ExpandedVertexCount == 367436u);
        static_assert(ExpandedIndexCount == 551154u);

        /** Creates parent directories for one explicitly requested points3 artifact. */
        void preparePoints3OutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computePoints3RgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_custom_attributes_points3 RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Reads one bounded immutable file into exact bytes. */
        eastl::vector<uint8_t> readPoints3AssetBytes(
            const std::filesystem::path &inputPath)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open Three r185 ball asset: " + inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    "Three r185 ball asset has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete Three r185 ball asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte range. */
        eastl::string calculatePoints3Sha256(const void *bytes, size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error(
                    "webgl_custom_attributes_points3 SHA-256 input is too large.");
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

        /** Reads one network-order uint32 from a validated PNG byte offset. */
        uint32_t readPoints3BigEndianUint32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error(
                    "textures/sprites/ball.png has a truncated uint32 field.");
            }
            return (uint32_t(bytes[offset]) << 24u) |
                   (uint32_t(bytes[offset + 1u]) << 16u) |
                   (uint32_t(bytes[offset + 2u]) << 8u) |
                   uint32_t(bytes[offset + 3u]);
        }

        /** Returns PNG's Paeth predictor for one filtered byte. */
        uint8_t predictPoints3PngPaeth(
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
            {
                return left;
            }
            if (aboveDistance <= upperLeftDistance)
            {
                return above;
            }
            return upperLeft;
        }

        /** Decodes the pinned noninterlaced RGBA8 PNG without altering hidden RGB. */
        eastl::vector<uint8_t> decodePoints3BallPng(
            const eastl::vector<uint8_t> &pngBytes)
        {
            constexpr eastl::array<uint8_t, 8u> PngSignature = {
                0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
            if (pngBytes.size() < PngSignature.size() ||
                !eastl::equal(
                    PngSignature.begin(),
                    PngSignature.end(),
                    pngBytes.begin()))
            {
                throw std::runtime_error(
                    "textures/sprites/ball.png has an invalid PNG signature.");
            }

            eastl::vector<uint8_t> compressed;
            bool ihdrSeen = false;
            bool iendSeen = false;
            size_t offset = PngSignature.size();
            while (offset < pngBytes.size())
            {
                if (pngBytes.size() - offset < 12u)
                {
                    throw std::runtime_error(
                        "textures/sprites/ball.png has a truncated chunk header.");
                }
                const uint32_t payloadSize =
                    readPoints3BigEndianUint32(pngBytes, offset);
                const size_t payloadOffset = offset + 8u;
                if (payloadOffset > pngBytes.size() ||
                    pngBytes.size() - payloadOffset < 4u ||
                    payloadSize > pngBytes.size() - payloadOffset - 4u)
                {
                    throw std::runtime_error(
                        "textures/sprites/ball.png has a truncated chunk payload.");
                }
                const char type0 = char(pngBytes[offset + 4u]);
                const char type1 = char(pngBytes[offset + 5u]);
                const char type2 = char(pngBytes[offset + 6u]);
                const char type3 = char(pngBytes[offset + 7u]);
                if (type0 == 'I' && type1 == 'H' && type2 == 'D' && type3 == 'R')
                {
                    if (ihdrSeen || payloadSize != 13u ||
                        readPoints3BigEndianUint32(pngBytes, payloadOffset) !=
                            BallTextureExtent ||
                        readPoints3BigEndianUint32(pngBytes, payloadOffset + 4u) !=
                            BallTextureExtent ||
                        pngBytes[payloadOffset + 8u] != 8u ||
                        pngBytes[payloadOffset + 9u] != 6u ||
                        pngBytes[payloadOffset + 10u] != 0u ||
                        pngBytes[payloadOffset + 11u] != 0u ||
                        pngBytes[payloadOffset + 12u] != 0u)
                    {
                        throw std::runtime_error(
                            "ball.png must remain a 64x64 noninterlaced RGBA8 PNG.");
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
                    "textures/sprites/ball.png lacks its required PNG chunks.");
            }
            const uint32_t zlibHeader =
                uint32_t(compressed[0u]) * 256u + uint32_t(compressed[1u]);
            if ((compressed[0u] & 0x0fu) != 8u || zlibHeader % 31u != 0u)
            {
                throw std::runtime_error(
                    "textures/sprites/ball.png has an unsupported zlib stream.");
            }

            constexpr size_t RowByteCount = BallTextureExtent * 4u;
            constexpr size_t FilteredByteCount =
                BallTextureExtent * (RowByteCount + 1u);
            eastl::vector<uint8_t> filtered(FilteredByteCount);
            const size_t decodedSize = compression_decode_buffer(
                filtered.data(),
                filtered.size(),
                compressed.data() + 2u,
                compressed.size() - 6u,
                nullptr,
                COMPRESSION_ZLIB);
            if (decodedSize != FilteredByteCount)
            {
                throw std::runtime_error(
                    "Could not inflate the complete ball.png RGBA payload.");
            }

            eastl::vector<uint8_t> decoded(
                static_cast<size_t>(BallTextureExtent) *
                BallTextureExtent * 4u);
            for (uint32_t y = 0u; y < BallTextureExtent; ++y)
            {
                const size_t filteredRow =
                    static_cast<size_t>(y) * (RowByteCount + 1u);
                const uint8_t filter = filtered[filteredRow];
                if (filter > 4u)
                {
                    throw std::runtime_error(
                        "ball.png uses an unsupported PNG row filter.");
                }
                for (size_t x = 0u; x < RowByteCount; ++x)
                {
                    const uint8_t source = filtered[filteredRow + 1u + x];
                    const uint8_t left = x >= 4u
                        ? decoded[static_cast<size_t>(y) * RowByteCount + x - 4u]
                        : 0u;
                    const uint8_t above = y > 0u
                        ? decoded[(static_cast<size_t>(y) - 1u) * RowByteCount + x]
                        : 0u;
                    const uint8_t upperLeft = y > 0u && x >= 4u
                        ? decoded[(static_cast<size_t>(y) - 1u) * RowByteCount + x - 4u]
                        : 0u;
                    uint8_t predictor = 0u;
                    if (filter == 1u)
                    {
                        predictor = left;
                    }
                    else if (filter == 2u)
                    {
                        predictor = above;
                    }
                    else if (filter == 3u)
                    {
                        predictor = uint8_t(
                            (uint32_t(left) + uint32_t(above)) / 2u);
                    }
                    else if (filter == 4u)
                    {
                        predictor =
                            predictPoints3PngPaeth(left, above, upperLeft);
                    }
                    decoded[static_cast<size_t>(y) * RowByteCount + x] =
                        uint8_t(uint32_t(source) + uint32_t(predictor));
                }
            }
            return decoded;
        }

        /** Flips source rows exactly like Three's default UNPACK_FLIP_Y path. */
        eastl::vector<uint8_t> flipPoints3TextureRows(
            const eastl::vector<uint8_t> &source,
            uint32_t extent)
        {
            const size_t rowByteCount = static_cast<size_t>(extent) * 4u;
            if (source.size() != rowByteCount * extent)
            {
                throw std::invalid_argument(
                    "Point texture row flip requires tightly packed RGBA8 bytes.");
            }
            eastl::vector<uint8_t> flipped(source.size());
            for (uint32_t row = 0u; row < extent; ++row)
            {
                eastl::copy_n(
                    source.begin() + static_cast<size_t>(extent - 1u - row) *
                                         rowByteCount,
                    rowByteCount,
                    flipped.begin() + static_cast<size_t>(row) * rowByteCount);
            }
            return flipped;
        }

        /** Builds one raw-UNORM 2x2 mip with nearest integer channel rounding. */
        eastl::vector<uint8_t> buildNextPoints3TextureMip(
            const eastl::vector<uint8_t> &source,
            uint32_t sourceExtent)
        {
            if (sourceExtent < 2u ||
                source.size() !=
                    static_cast<size_t>(sourceExtent) * sourceExtent * 4u)
            {
                throw std::invalid_argument(
                    "Point texture mip generation requires one square RGBA8 level.");
            }
            const uint32_t targetExtent = sourceExtent / 2u;
            eastl::vector<uint8_t> target(
                static_cast<size_t>(targetExtent) * targetExtent * 4u);
            for (uint32_t y = 0u; y < targetExtent; ++y)
            {
                for (uint32_t x = 0u; x < targetExtent; ++x)
                {
                    for (uint32_t channel = 0u; channel < 4u; ++channel)
                    {
                        uint32_t sum = 0u;
                        for (uint32_t sampleY = 0u; sampleY < 2u; ++sampleY)
                        {
                            for (uint32_t sampleX = 0u; sampleX < 2u; ++sampleX)
                            {
                                const size_t sourceOffset =
                                    (static_cast<size_t>(y * 2u + sampleY) *
                                         sourceExtent +
                                     (x * 2u + sampleX)) *
                                        4u +
                                    channel;
                                sum += source[sourceOffset];
                            }
                        }
                        const size_t targetOffset =
                            (static_cast<size_t>(y) * targetExtent + x) * 4u +
                            channel;
                        target[targetOffset] =
                            static_cast<uint8_t>((sum + 2u) / 4u);
                    }
                }
            }
            return target;
        }

        /** Builds all seven CPU-authored raw-UNORM mips from flipped browser bytes. */
        eastl::vector<eastl::vector<uint8_t>> buildPoints3TextureMips(
            const eastl::vector<uint8_t> &decoded)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(BallTextureMipCount);
            result.push_back(
                flipPoints3TextureRows(decoded, BallTextureExtent));
            uint32_t extent = BallTextureExtent;
            while (extent > 1u)
            {
                result.push_back(
                    buildNextPoints3TextureMip(result.back(), extent));
                extent /= 2u;
            }
            if (result.size() != BallTextureMipCount ||
                calculatePoints3Sha256(
                    result.front().data(),
                    result.front().size()) != FlippedBallBaseSha256)
            {
                throw std::runtime_error(
                    "ball.png decoded texture bytes diverged from the Chrome r185 probe.");
            }
            return result;
        }

        /** Sets one Cartesian component selected by a Three BoxGeometry axis index. */
        void setPoints3Axis(glm::dvec3 &value, uint32_t axis, double component)
        {
            if (axis == 0u)
            {
                value.x = component;
            }
            else if (axis == 1u)
            {
                value.y = component;
            }
            else
            {
                value.z = component;
            }
        }

        /** Appends one exact segmented Three BoxGeometry plane. */
        void appendPoints3BoxPlane(
            eastl::vector<glm::vec3> &positions,
            eastl::vector<uint32_t> &indices,
            uint32_t uAxis,
            uint32_t vAxis,
            uint32_t wAxis,
            double uDirection,
            double vDirection,
            double width,
            double height,
            double depth,
            uint32_t gridX,
            uint32_t gridY)
        {
            const double segmentWidth = width / double(gridX);
            const double segmentHeight = height / double(gridY);
            const double widthHalf = width / 2.0;
            const double heightHalf = height / 2.0;
            const double depthHalf = depth / 2.0;
            const uint32_t gridX1 = gridX + 1u;
            const uint32_t baseVertex = static_cast<uint32_t>(positions.size());
            glm::dvec3 value(0.0);
            for (uint32_t iy = 0u; iy <= gridY; ++iy)
            {
                const double y = double(iy) * segmentHeight - heightHalf;
                for (uint32_t ix = 0u; ix <= gridX; ++ix)
                {
                    const double x = double(ix) * segmentWidth - widthHalf;
                    setPoints3Axis(value, uAxis, x * uDirection);
                    setPoints3Axis(value, vAxis, y * vDirection);
                    setPoints3Axis(value, wAxis, depthHalf);
                    positions.push_back(glm::vec3(
                        static_cast<float>(value.x),
                        static_cast<float>(value.y),
                        static_cast<float>(value.z)));
                }
            }
            for (uint32_t iy = 0u; iy < gridY; ++iy)
            {
                for (uint32_t ix = 0u; ix < gridX; ++ix)
                {
                    const uint32_t a = baseVertex + ix + gridX1 * iy;
                    const uint32_t b =
                        baseVertex + ix + gridX1 * (iy + 1u);
                    const uint32_t c =
                        baseVertex + ix + 1u + gridX1 * (iy + 1u);
                    const uint32_t d =
                        baseVertex + ix + 1u + gridX1 * iy;
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);
                    indices.push_back(b);
                    indices.push_back(c);
                    indices.push_back(d);
                }
            }
        }

        /** Builds one exact Float32 segmented BoxGeometry position and index stream. */
        void buildPoints3RawBox(
            double width,
            double height,
            double depth,
            uint32_t widthSegments,
            uint32_t heightSegments,
            uint32_t depthSegments,
            eastl::vector<glm::vec3> &positions,
            eastl::vector<uint32_t> &indices)
        {
            positions.clear();
            indices.clear();
            appendPoints3BoxPlane(
                positions, indices, 2u, 1u, 0u,
                -1.0, -1.0, depth, height, width,
                depthSegments, heightSegments);
            appendPoints3BoxPlane(
                positions, indices, 2u, 1u, 0u,
                1.0, -1.0, depth, height, -width,
                depthSegments, heightSegments);
            appendPoints3BoxPlane(
                positions, indices, 0u, 2u, 1u,
                1.0, 1.0, width, depth, height,
                widthSegments, depthSegments);
            appendPoints3BoxPlane(
                positions, indices, 0u, 2u, 1u,
                1.0, -1.0, width, depth, -height,
                widthSegments, depthSegments);
            appendPoints3BoxPlane(
                positions, indices, 0u, 1u, 2u,
                1.0, -1.0, width, height, depth,
                widthSegments, heightSegments);
            appendPoints3BoxPlane(
                positions, indices, 0u, 1u, 2u,
                -1.0, -1.0, width, height, -depth,
                widthSegments, heightSegments);
        }

        /** Packs Three's three 1e-4 quantized position components into one key. */
        uint64_t makePoints3MergeKey(const glm::vec3 &position)
        {
            constexpr int32_t QuantizedOffset = 1 << 20;
            constexpr uint64_t QuantizedMask = (1ull << 21u) - 1ull;
            const int32_t x = static_cast<int32_t>(
                double(position.x) * 10000.0 + 0.5);
            const int32_t y = static_cast<int32_t>(
                double(position.y) * 10000.0 + 0.5);
            const int32_t z = static_cast<int32_t>(
                double(position.z) * 10000.0 + 0.5);
            return (uint64_t(x + QuantizedOffset) & QuantizedMask) |
                   ((uint64_t(y + QuantizedOffset) & QuantizedMask) << 21u) |
                   ((uint64_t(z + QuantizedOffset) & QuantizedMask) << 42u);
        }

        /** Reproduces BufferGeometryUtils.mergeVertices for position-only box geometry. */
        eastl::vector<glm::vec3> mergePoints3Vertices(
            const eastl::vector<glm::vec3> &positions,
            const eastl::vector<uint32_t> &indices)
        {
            eastl::unordered_map<uint64_t, uint32_t> hashToIndex;
            eastl::vector<glm::vec3> merged;
            hashToIndex.reserve(positions.size());
            merged.reserve(positions.size());
            for (const uint32_t sourceIndex : indices)
            {
                if (sourceIndex >= positions.size())
                {
                    throw std::runtime_error(
                        "points3 source geometry contains an invalid index.");
                }
                const uint64_t key = makePoints3MergeKey(positions[sourceIndex]);
                if (hashToIndex.find(key) == hashToIndex.end())
                {
                    hashToIndex.emplace(key, static_cast<uint32_t>(merged.size()));
                    merged.push_back(positions[sourceIndex]);
                }
            }
            return merged;
        }

        /** Appends one Three Matrix4-composed Y rotation and translation copy. */
        void appendPoints3TransformedBox(
            const eastl::vector<glm::vec3> &source,
            double translationX,
            double translationY,
            double translationZ,
            double rotationY,
            eastl::vector<glm::vec3> &target)
        {
            const double quaternionY = std::sin(rotationY * 0.5);
            const double quaternionW = std::cos(rotationY * 0.5);
            const double doubledY = quaternionY + quaternionY;
            const double yy = quaternionY * doubledY;
            const double wy = quaternionW * doubledY;
            for (const glm::vec3 &sourcePosition : source)
            {
                const double x = double(sourcePosition.x);
                const double y = double(sourcePosition.y);
                const double z = double(sourcePosition.z);
                target.push_back(glm::vec3(
                    static_cast<float>(
                        (1.0 - yy) * x + wy * z + translationX),
                    static_cast<float>(y + translationY),
                    static_cast<float>(
                        -wy * x + (1.0 - yy) * z + translationZ)));
            }
        }

        /** Returns one exact JavaScript Math.random value as a binary64 number. */
        double nextPoints3RandomDouble(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Returns Three's wrapped HSL channel interpolation for one hue phase. */
        double points3HueToRgb(double minimum, double maximum, double hue)
        {
            if (hue < 0.0)
            {
                hue += 1.0;
            }
            if (hue > 1.0)
            {
                hue -= 1.0;
            }
            if (hue < 1.0 / 6.0)
            {
                return minimum + (maximum - minimum) * 6.0 * hue;
            }
            if (hue < 1.0 / 2.0)
            {
                return maximum;
            }
            if (hue < 2.0 / 3.0)
            {
                return minimum +
                       (maximum - minimum) * 6.0 * (2.0 / 3.0 - hue);
            }
            return minimum;
        }

        /** Evaluates Three Color.setHSL in the default linear working color space. */
        glm::vec3 makePoints3HslColor(
            double hue,
            double saturation,
            double lightness)
        {
            hue = std::fmod(hue, 1.0);
            if (hue < 0.0)
            {
                hue += 1.0;
            }
            saturation = eastl::clamp(saturation, 0.0, 1.0);
            lightness = eastl::clamp(lightness, 0.0, 1.0);
            if (saturation == 0.0)
            {
                return glm::vec3(static_cast<float>(lightness));
            }
            const double maximum = lightness <= 0.5
                ? lightness * (1.0 + saturation)
                : lightness + saturation - lightness * saturation;
            const double minimum = 2.0 * lightness - maximum;
            return glm::vec3(
                static_cast<float>(points3HueToRgb(
                    minimum, maximum, hue + 1.0 / 3.0)),
                static_cast<float>(points3HueToRgb(minimum, maximum, hue)),
                static_cast<float>(points3HueToRgb(
                    minimum, maximum, hue - 1.0 / 3.0)));
        }

        /** Advances one audited UUID segment and validates the exact resulting state. */
        void advancePoints3RandomState(
            ThreeCompat::DeterministicRandom &random,
            uint32_t drawCount,
            uint32_t expectedState,
            const char *label)
        {
            for (uint32_t drawIndex = 0u; drawIndex < drawCount; ++drawIndex)
            {
                static_cast<void>(random.nextUint32());
            }
            if (random.getState() != expectedState)
            {
                const eastl::string message =
                    eastl::string("points3 RNG segment diverged: ") + label;
                throw std::runtime_error(message.c_str());
            }
        }

        /** Builds exact random shell, merged edge copies, padded colors, and quad expansion. */
        void buildPoints3ExpandedGeometry(
            ThreeCompat::DeterministicRandom &random,
            eastl::vector<WebglCustomAttributesPoints3Vertex> &vertices,
            eastl::vector<uint32_t> &indices,
            eastl::vector<float> &sizes)
        {
            constexpr double ShellRadius = 100.0;
            constexpr double ShellInnerExtent = 60.0;
            eastl::vector<glm::vec3> logicalPositions;
            logicalPositions.reserve(LogicalPointCount);
            for (uint32_t candidateIndex = 0u;
                 candidateIndex < RandomCandidateCount;
                 ++candidateIndex)
            {
                const double x =
                    (nextPoints3RandomDouble(random) * 2.0 - 1.0) *
                    ShellRadius;
                const double y =
                    (nextPoints3RandomDouble(random) * 2.0 - 1.0) *
                    ShellRadius;
                const double z =
                    (nextPoints3RandomDouble(random) * 2.0 - 1.0) *
                    ShellRadius;
                if (x > ShellInnerExtent || x < -ShellInnerExtent ||
                    y > ShellInnerExtent || y < -ShellInnerExtent ||
                    z > ShellInnerExtent || z < -ShellInnerExtent)
                {
                    logicalPositions.push_back(glm::vec3(
                        static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(z)));
                }
            }
            if (logicalPositions.size() != ShellPointCount ||
                random.getState() != 3496169787u)
            {
                throw std::runtime_error(
                    "points3 shell filtering diverged from the clean browser probe.");
            }

            eastl::vector<glm::vec3> rawPositions;
            eastl::vector<uint32_t> rawIndices;
            buildPoints3RawBox(
                200.0, 20.0, 20.0, 50u, 5u, 5u,
                rawPositions, rawIndices);
            const eastl::vector<glm::vec3> boxGeometry1 =
                mergePoints3Vertices(rawPositions, rawIndices);
            buildPoints3RawBox(
                20.0, 240.0, 20.0, 5u, 60u, 5u,
                rawPositions, rawIndices);
            const eastl::vector<glm::vec3> boxGeometry2 =
                mergePoints3Vertices(rawPositions, rawIndices);
            if (boxGeometry1.size() != BoxGeometry1PointCount ||
                boxGeometry2.size() != BoxGeometry2PointCount)
            {
                throw std::runtime_error(
                    "points3 mergeVertices counts diverged from Three r185.");
            }

            appendPoints3TransformedBox(boxGeometry1, 0.0, 110.0, 110.0, 0.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry1, 0.0, 110.0, -110.0, 0.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry1, 0.0, -110.0, 110.0, 0.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry1, 0.0, -110.0, -110.0, 0.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry1, 110.0, 110.0, 0.0, Pi / 2.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry1, 110.0, -110.0, 0.0, Pi / 2.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry1, -110.0, 110.0, 0.0, Pi / 2.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry1, -110.0, -110.0, 0.0, Pi / 2.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry2, 110.0, 0.0, 110.0, 0.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry2, 110.0, 0.0, -110.0, 0.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry2, -110.0, 0.0, 110.0, 0.0, logicalPositions);
            appendPoints3TransformedBox(boxGeometry2, -110.0, 0.0, -110.0, 0.0, logicalPositions);
            if (logicalPositions.size() != LogicalPointCount ||
                calculatePoints3Sha256(
                    logicalPositions.data(),
                    logicalPositions.size() * sizeof(glm::vec3)) !=
                    PositionSha256)
            {
                throw std::runtime_error(
                    "points3 final Float32 positions diverged from the browser probe.");
            }

            eastl::vector<glm::vec3> logicalColors;
            logicalColors.reserve(LogicalPointCount);
            sizes.resize(LogicalPointCount);
            for (uint32_t pointIndex = 0u;
                 pointIndex < LogicalPointCount;
                 ++pointIndex)
            {
                logicalColors.push_back(pointIndex < ShellPointCount
                    ? makePoints3HslColor(
                          0.5 + 0.2 *
                                    (double(pointIndex) /
                                     double(ShellPointCount)),
                          1.0,
                          0.5)
                    : makePoints3HslColor(0.1, 1.0, 0.5));
                sizes[pointIndex] = pointIndex < ShellPointCount
                    ? 10.0f
                    : 40.0f;
            }
            if (calculatePoints3Sha256(
                    logicalColors.data(),
                    logicalColors.size() * sizeof(glm::vec3)) != ColorSha256)
            {
                throw std::runtime_error(
                    "points3 Float32 custom colors diverged from the browser probe.");
            }

            const eastl::array<glm::vec2, 4u> corners = {
                glm::vec2(-1.0f, -1.0f),
                glm::vec2(1.0f, -1.0f),
                glm::vec2(1.0f, 1.0f),
                glm::vec2(-1.0f, 1.0f)};
            vertices.clear();
            indices.clear();
            vertices.reserve(ExpandedVertexCount);
            indices.reserve(ExpandedIndexCount);
            for (uint32_t pointIndex = 0u;
                 pointIndex < LogicalPointCount;
                 ++pointIndex)
            {
                for (const glm::vec2 corner : corners)
                {
                    vertices.push_back({
                        .position = logicalPositions[pointIndex],
                        .customColor = glm::vec4(
                            logicalColors[pointIndex],
                            1.0f),
                        .corner = corner,
                    });
                }
                const uint32_t baseVertex = pointIndex * 4u;
                indices.push_back(baseVertex + 0u);
                indices.push_back(baseVertex + 1u);
                indices.push_back(baseVertex + 2u);
                indices.push_back(baseVertex + 0u);
                indices.push_back(baseVertex + 2u);
                indices.push_back(baseVertex + 3u);
            }
            if (vertices.size() != ExpandedVertexCount ||
                indices.size() != ExpandedIndexCount)
            {
                throw std::runtime_error(
                    "points3 noninstanced expansion counts diverged from r185.");
            }
        }

        /** Returns an identity matrix in Three's column-major element order. */
        Points3Matrix makePoints3IdentityMatrix()
        {
            return {
                1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0};
        }

        /** Multiplies two matrices with Three Matrix4's exact operation order. */
        Points3Matrix multiplyPoints3Matrices(
            const Points3Matrix &left,
            const Points3Matrix &right)
        {
            Points3Matrix result = {};
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                for (uint32_t row = 0u; row < 4u; ++row)
                {
                    result[column * 4u + row] =
                        left[row] * right[column * 4u] +
                        left[4u + row] * right[column * 4u + 1u] +
                        left[8u + row] * right[column * 4u + 2u] +
                        left[12u + row] * right[column * 4u + 3u];
                }
            }
            return result;
        }

        /** Builds Three's 40-degree OpenGL projection matrix as binary64 elements. */
        Points3Matrix makePoints3ProjectionMatrix(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 40.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 1000.0;
            const double top =
                NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                double(width) / double(height) * projectionHeight;
            const double projectionDepth = FarDistance - NearDistance;
            Points3Matrix projection = {};
            projection[0u] = 2.0 * NearDistance / projectionWidth;
            projection[5u] = 2.0 * NearDistance / projectionHeight;
            projection[10u] =
                -(FarDistance + NearDistance) / projectionDepth;
            projection[11u] = -1.0;
            projection[14u] =
                -2.0 * FarDistance * NearDistance / projectionDepth;
            return projection;
        }

        /** Builds the exact quaternion-composed XYZ matrix for equal Y/Z rotation. */
        Points3Matrix makePoints3ModelMatrix(double rotation)
        {
            const double c1 = 1.0;
            const double c2 = std::cos(rotation / 2.0);
            const double c3 = std::cos(rotation / 2.0);
            const double s1 = 0.0;
            const double s2 = std::sin(rotation / 2.0);
            const double s3 = std::sin(rotation / 2.0);
            const double x = s1 * c2 * c3 + c1 * s2 * s3;
            const double y = c1 * s2 * c3 - s1 * c2 * s3;
            const double z = c1 * c2 * s3 + s1 * s2 * c3;
            const double w = c1 * c2 * c3 - s1 * s2 * s3;
            const double x2 = x + x;
            const double y2 = y + y;
            const double z2 = z + z;
            const double xx = x * x2;
            const double xy = x * y2;
            const double xz = x * z2;
            const double yy = y * y2;
            const double yz = y * z2;
            const double zz = z * z2;
            const double wx = w * x2;
            const double wy = w * y2;
            const double wz = w * z2;
            return {
                1.0 - (yy + zz), xy + wz, xz - wy, 0.0,
                xy - wz, 1.0 - (xx + zz), yz + wx, 0.0,
                xz + wy, yz - wx, 1.0 - (xx + yy), 0.0,
                0.0, 0.0, 0.0, 1.0};
        }

        /** Builds the current camera matrixWorldInverse at z equals 500. */
        Points3Matrix makePoints3ViewMatrix()
        {
            Points3Matrix view = makePoints3IdentityMatrix();
            view[14u] = -500.0;
            return view;
        }

        /** Converts a binary64 Three matrix to the Float32 uniform matrix ABI. */
        glm::mat4 makePoints3FloatMatrix(const Points3Matrix &source)
        {
            glm::mat4 result(0.0f);
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                for (uint32_t row = 0u; row < 4u; ++row)
                {
                    result[column][row] =
                        static_cast<float>(source[column * 4u + row]);
                }
            }
            return result;
        }

        /** Validates the two locked scenarios and every explicit host parameter. */
        void validatePoints3Scenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_custom_attributes_points3")
            {
                throw std::invalid_argument(
                    "Points3 adapter requires case-id webgl_custom_attributes_points3.");
            }
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated-size-and-fog" &&
                options.targetFrame == 60u;
            if (!initial && !animated)
            {
                throw std::invalid_argument(
                    "points3 requires initial/frame 0 or animated-size-and-fog/frame 60.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument(
                    "points3 requires the locked 800x500 extent.");
            }
            if (options.randomSeed != Points3RandomSeed)
            {
                throw std::invalid_argument(
                    "points3 requires random seed 0x18500014.");
            }
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "points3 requires explicit --asset-root.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "points3 does not accept an input replay.");
            }
        }
    } // namespace

    void WebglCustomAttributesPoints3RuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validatePoints3Scenario(options);
        device = inDevice;
        random.reset(Points3RandomSeed);
        advancePoints3RandomState(random, 76u, 1736956936u, "module imports");
        advancePoints3RandomState(random, 4u, 3472450054u, "PerspectiveCamera");
        advancePoints3RandomState(random, 4u, 2413416337u, "Scene");
        buildPoints3ExpandedGeometry(random, vertices, indices, sizes);
        advancePoints3RandomState(random, 4u, 753332669u, "BoxGeometry one");
        advancePoints3RandomState(random, 4u, 2152637164u, "box one mergeVertices");
        advancePoints3RandomState(random, 4u, 1415315803u, "BoxGeometry two");
        advancePoints3RandomState(random, 4u, 673321235u, "box two mergeVertices");
        advancePoints3RandomState(random, 4u, 125041257u, "final BufferGeometry");
        advancePoints3RandomState(random, 8u, 2921448620u, "Texture and Source");
        advancePoints3RandomState(random, 4u, 4233772355u, "ShaderMaterial");
        advancePoints3RandomState(random, 4u, 3983851488u, "Points");
        advancePoints3RandomState(random, 36u, 2372394654u, "WebGLRenderer");
        if (random.getState() != 2372394654u ||
            TotalRandomDrawCount != 300156u)
        {
            throw std::runtime_error(
                "points3 final RNG audit diverged from the clean browser probe.");
        }

        const std::filesystem::path ballPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "sprites" / "ball.png";
        const eastl::vector<uint8_t> assetBytes =
            readPoints3AssetBytes(ballPath);
        if (calculatePoints3Sha256(assetBytes.data(), assetBytes.size()) !=
            BallAssetSha256)
        {
            throw std::invalid_argument(
                "textures/sprites/ball.png differs from the pinned Three r185 asset.");
        }
        textureMips = buildPoints3TextureMips(
            decodePoints3BallPng(assetBytes));

        projectionMatrix = makePoints3FloatMatrix(
            makePoints3ProjectionMatrix(options.width, options.height));
        modelViewMatrix = glm::mat4(1.0f);
        virtualTimeMilliseconds = 0.0;
        renderedVirtualTimeMilliseconds = 0.0;
        timeValue = 0.0;
        rotationY = 0.0;
        rotationZ = 0.0;
        zeroSizeCount = 0u;
        frameUpdateCount = 0u;
        captureWritten = false;
    }

    void WebglCustomAttributesPoints3RuntimeAdapter::advanceFrameState(
        uint32_t frameIndex,
        uint32_t targetFrame)
    {
        if (frameIndex != frameUpdateCount)
        {
            throw std::logic_error(
                "points3 must advance sequentially from frame zero.");
        }
        renderedVirtualTimeMilliseconds = virtualTimeMilliseconds;
        timeValue =
            (ReferenceEpochMilliseconds + renderedVirtualTimeMilliseconds) *
            0.01;
        rotationY = 0.02 * timeValue;
        rotationZ = rotationY;
        zeroSizeCount = 0u;
        for (uint32_t pointIndex = 0u;
             pointIndex < ShellPointCount;
             ++pointIndex)
        {
            sizes[pointIndex] = static_cast<float>(eastl::max(
                0.0,
                26.0 + 32.0 * std::sin(
                                  0.1 * double(pointIndex) +
                                  0.6 * timeValue)));
            if (sizes[pointIndex] == 0.0f)
            {
                ++zeroSizeCount;
            }
        }

        const Points3Matrix currentModel =
            makePoints3ModelMatrix(rotationY);
        const Points3Matrix currentModelView = multiplyPoints3Matrices(
            makePoints3ViewMatrix(),
            currentModel);
        modelViewMatrix = makePoints3FloatMatrix(currentModelView);
        sizeSha256 = calculatePoints3Sha256(
            sizes.data(), sizes.size() * sizeof(float));
        if (frameIndex == 0u &&
            (sizeSha256 != InitialSizeSha256 || zeroSizeCount != 15546u))
        {
            throw std::runtime_error(
                "points3 frame-zero size bytes diverged from the browser probe.");
        }
        if (frameIndex == 60u &&
            (sizeSha256 != AnimatedSizeSha256 || zeroSizeCount != 15544u))
        {
            throw std::runtime_error(
                "points3 frame-60 size bytes diverged from the browser probe.");
        }

        ++frameUpdateCount;
        virtualTimeMilliseconds += FrameStepMilliseconds;
        if (frameIndex == targetFrame && sizeSha256.empty())
        {
            throw std::runtime_error(
                "points3 target frame lacks its deterministic size digest.");
        }
    }

    void WebglCustomAttributesPoints3RuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        if (!frameUploader)
        {
            throw std::logic_error(
                "points3 generated frame uploader is unavailable.");
        }
        advanceFrameState(frameIndex, options.targetFrame);
        frameUploader(
            sizes,
            projectionMatrix,
            modelViewMatrix,
            frameIndex == options.targetFrame);
    }

    void WebglCustomAttributesPoints3RuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computePoints3RgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "points3 capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "points3 could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglCustomAttributesPoints3RuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        frameUploader = nullptr;
    }

    void WebglCustomAttributesPoints3RuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        preparePoints3OutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open points3 RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write the complete points3 RGBA capture.");
        }
    }

    void WebglCustomAttributesPoints3RuntimeAdapter::writeCaptureMetadata(
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
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        preparePoints3OutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open points3 metadata path.");
        }
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_custom_attributes_points3\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"randomState\": " << random.getState() << ",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"virtualTimeMs\": " << renderedVirtualTimeMilliseconds << ",\n"
               << "  \"nextFrameTimeMs\": " << virtualTimeMilliseconds << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }

    void WebglCustomAttributesPoints3RuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        preparePoints3OutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open points3 snapshot path.");
        }
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_custom_attributes_points3\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"canonicalState\": \""
               << (frameIndex == 0u
                       ? "seed-0x18500014-time-zero-91859-ball-billboards-no-upstream-index-color-sha-16188fb73af7-size-sha-336b6cbb8474-zero-sizes-15546"
                       : "seed-0x18500014-fixed-step-60hz-one-second-91859-ball-billboards-color-sha-16188fb73af7-size-sha-510afd7c2d0f-zero-sizes-15544-rotation-and-fragment-depth-fog")
               << "\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"computePassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"logicalDrawCommandCount\": 1,\n"
               << "  \"physicalCoverageDrawCount\": 1,\n"
               << "  \"randomCandidateCount\": " << RandomCandidateCount << ",\n"
               << "  \"sourceShellPointCount\": " << ShellPointCount << ",\n"
               << "  \"boxGeometry1MergedPointCount\": " << BoxGeometry1PointCount << ",\n"
               << "  \"boxGeometry1CopyCount\": " << BoxGeometry1CopyCount << ",\n"
               << "  \"boxGeometry2MergedPointCount\": " << BoxGeometry2PointCount << ",\n"
               << "  \"boxGeometry2CopyCount\": " << BoxGeometry2CopyCount << ",\n"
               << "  \"sourceBoxEdgePointCount\": " << BoxEdgePointCount << ",\n"
               << "  \"logicalPointCount\": " << LogicalPointCount << ",\n"
               << "  \"expandedVertexCount\": " << ExpandedVertexCount << ",\n"
               << "  \"explicitIndexCount\": " << ExpandedIndexCount << ",\n"
               << "  \"expandedTriangleCount\": " << ExpandedIndexCount / 3u << ",\n"
               << "  \"vertexStrideBytes\": 36,\n"
               << "  \"sourceColorComponentCount\": 3,\n"
               << "  \"normalizedColorComponentCount\": 4,\n"
               << "  \"defaultColorAlpha\": 1,\n"
               << "  \"standaloneGeometryBufferCount\": 2,\n"
               << "  \"dynamicSizeBufferCount\": 1,\n"
               << "  \"primitiveTopology\": \"triangle-list\",\n"
               << "  \"pointExpansion\": \"four-corners-six-indices-noninstanced\",\n"
               << "  \"nativePointSubpixelBits\": 4,\n"
               << "  \"nativePointSizeRange\": [1,511],\n"
               << "  \"antialias\": false,\n"
               << "  \"coverageSampleCount\": 1,\n"
               << "  \"internalCoverageResolve\": false,\n"
               << "  \"projectionConvention\": \"three-opengl-positive-y-negative-one-to-one\",\n"
               << "  \"dslClipConversion\": \"y-negate-and-z-half-range-after-point-expansion\",\n"
               << "  \"pointCoordOrigin\": \"upper-left\",\n"
               << "  \"dynamicSizeUploadCount\": " << frameUpdateCount << ",\n"
               << "  \"renderedVirtualTimeMs\": " << renderedVirtualTimeMilliseconds << ",\n"
               << "  \"dateNowSource\": \"epoch-plus-fixed-step\",\n"
               << "  \"timeValue\": " << timeValue << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"rotationZ\": " << rotationZ << ",\n"
               << "  \"zeroSizeCount\": " << zeroSizeCount << ",\n"
               << "  \"sizeSha256\": \"" << sizeSha256.c_str() << "\",\n"
               << "  \"positionSha256\": \"" << PositionSha256 << "\",\n"
               << "  \"colorSha256\": \"" << ColorSha256 << "\",\n"
               << "  \"seed\": " << Points3RandomSeed << ",\n"
               << "  \"moduleImportRandomDrawCount\": 76,\n"
               << "  \"cameraRandomDrawCount\": 4,\n"
               << "  \"sceneRandomDrawCount\": 4,\n"
               << "  \"positionRandomDrawCount\": 300000,\n"
               << "  \"geometryUuidRandomDrawCount\": 20,\n"
               << "  \"textureSourceRandomDrawCount\": 8,\n"
               << "  \"materialRandomDrawCount\": 4,\n"
               << "  \"pointsRandomDrawCount\": 4,\n"
               << "  \"rendererConstructorRandomDrawCount\": 36,\n"
               << "  \"firstRenderLazyRandomDrawCount\": 0,\n"
               << "  \"subsequentFrameRandomDrawCount\": 0,\n"
               << "  \"totalRandomDrawCount\": " << TotalRandomDrawCount << ",\n"
               << "  \"finalRandomState\": " << random.getState() << ",\n"
               << "  \"texturePath\": \"textures/sprites/ball.png\",\n"
               << "  \"textureAssetSha256\": \"" << BallAssetSha256 << "\",\n"
               << "  \"textureColorSpace\": \"NoColorSpace\",\n"
               << "  \"textureFormat\": \"rgba8unorm\",\n"
               << "  \"textureExtent\": " << BallTextureExtent << ",\n"
               << "  \"textureMipLevelCount\": " << BallTextureMipCount << ",\n"
               << "  \"textureBaseGpuBytesSha256\": \"" << FlippedBallBaseSha256 << "\",\n"
               << "  \"textureFlipY\": true,\n"
               << "  \"textureHiddenRgbPreserved\": true,\n"
               << "  \"mipmapGeneration\": \"explicit-cpu-raw-unorm-box-filter\",\n"
               << "  \"samplerAddressMode\": \"repeat\",\n"
               << "  \"samplerMinMagFilter\": \"linear\",\n"
               << "  \"samplerMipmapFilter\": \"linear\",\n"
               << "  \"alphaDiscardThreshold\": 0.5,\n"
               << "  \"fragmentDepthExpression\": \"position-z-divided-by-position-w\",\n"
               << "  \"fogNear\": 200,\n"
               << "  \"fogFar\": 600,\n"
               << "  \"fogColor\": [0,0,0],\n"
               << "  \"blendEnabled\": false,\n"
               << "  \"depthTest\": true,\n"
               << "  \"depthWrite\": true,\n"
               << "  \"depthCompare\": \"less-equal\",\n"
               << "  \"rendererAlpha\": false,\n"
               << "  \"rendererOutputColorSpace\": \"srgb\",\n"
               << "  \"shaderOutputTransfer\": \"none-custom-shader\",\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main-alpha-tested-ball-points\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"sceneRoots\": [\n"
               << "    {\"id\":\"scene\",\"renderSetCount\":0,\"renderSetId\":null,\"renderSetType\":null,\"renderableObjectCount\":1,\"entityCount\":0,\"entities\":[],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main-alpha-tested-ball-points\",\"renderClass\":\"WebglCustomAttributesPoints3MainPass\",\"renderSetId\":null,\"renderSetBindingCount\":0,\"drawMode\":\"explicit-indexed\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":true,\"usesExplicitDrawCount\":true}]}\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
