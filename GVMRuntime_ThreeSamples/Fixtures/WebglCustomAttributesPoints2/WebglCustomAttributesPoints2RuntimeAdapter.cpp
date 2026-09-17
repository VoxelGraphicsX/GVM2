#include "WebglCustomAttributesPoints2RuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <compression.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/sort.h>
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
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t Points2RandomSeed = 0x18500013u;
        constexpr uint32_t SpherePointCount = 2518u;
        constexpr uint32_t BoxPointCount = 602u;
        constexpr uint32_t LogicalPointCount = SpherePointCount + BoxPointCount;
        constexpr uint32_t ExpandedVertexCount = LogicalPointCount * 4u;
        constexpr uint32_t ExpandedIndexCount = LogicalPointCount * 6u;
        constexpr uint32_t DiscTextureExtent = 32u;
        constexpr uint32_t DiscTextureMipCount = 6u;
        constexpr uint32_t TotalRandomDrawCount = 160u;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *DiscAssetSha256 =
            "5429d0ce673fe08a0dc8bc852728ebd624bf8677650fa74f2c98c861e0de3721";
        constexpr const char *FlippedDiscBaseSha256 =
            "bdd23b9074323ec6ca8ec3c685374797ba2bf2bdedb945745a3befee6d5be2e0";
        constexpr const char *PositionSha256 =
            "8e24230550f6602ca179173649a5c4061c9b92a2a1f96504dc5aac63e1475999";
        constexpr const char *ColorSha256 =
            "52972aa0b0876d91e234e953f3220f777bf83ff17591d589e2b3e94d7e540f35";
        constexpr const char *InitialSizeSha256 =
            "5494433543fb44b1a1ac074700d3a56a5e65b43f53e322a383163e846d9069ff";
        constexpr const char *AnimatedSizeSha256 =
            "ffdf685b288850dd8074ee84de9f9c95ba43dc17492d8019393885fbd2d3e1c2";
        constexpr const char *InitialLogicalIndexSha256 =
            "eb906a9f26f4b171010ab45bc9d828c451721f380acf3e90509e9f64ad67ea05";
        constexpr const char *AnimatedLogicalIndexSha256 =
            "2d52c3c9f02934b06e69c8714039efc527519769188804a95c1d64cfd1ac2d33";
        constexpr const char *InitialExpandedIndexSha256 =
            "3801c9fd75312ac6ed377786531b90f8e39ca1530f294035d8d660110cf9b6b4";
        constexpr const char *AnimatedExpandedIndexSha256 =
            "b76c2055b4b63f47e9f1dae47785aebbec7878507e562288136a634251370a84";

        using Points2Matrix = eastl::array<double, 16u>;

        /** Stores one source point's projected sort depth and stable original ordinal. */
        struct Points2DepthRecord
        {
            double depth = 0.0;
            uint32_t ordinal = 0u;
        };

        /** Orders projected point depths back-to-front with the source ordinal as the stable tie-break. */
        struct Points2DepthDescending
        {
            /** Returns true only when the left depth must precede the right depth. */
            bool operator()(
                const Points2DepthRecord &left,
                const Points2DepthRecord &right) const
            {
                if (left.depth == right.depth)
                {
                    return left.ordinal < right.ordinal;
                }
                return left.depth > right.depth;
            }
        };

        static_assert(sizeof(WebglCustomAttributesPoints2Vertex) == 32u);
        static_assert(offsetof(WebglCustomAttributesPoints2Vertex, position) == 0u);
        static_assert(offsetof(WebglCustomAttributesPoints2Vertex, customColor) == 12u);
        static_assert(offsetof(WebglCustomAttributesPoints2Vertex, corner) == 24u);
        static_assert(sizeof(glm::vec3) == 12u);
        static_assert(LogicalPointCount == 3120u);
        static_assert(ExpandedVertexCount == 12480u);
        static_assert(ExpandedIndexCount == 18720u);

        /** Creates parent directories for one explicitly requested points2 artifact. */
        void preparePoints2OutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computePoints2RgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_custom_attributes_points2 RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Reads one bounded immutable file into exact bytes. */
        eastl::vector<uint8_t> readPoints2AssetBytes(
            const std::filesystem::path &inputPath)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open Three r185 disc asset: " + inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    "Three r185 disc asset has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete Three r185 disc asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte range. */
        eastl::string calculatePoints2Sha256(const void *bytes, size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error(
                    "webgl_custom_attributes_points2 SHA-256 input is too large.");
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
        uint32_t readPoints2BigEndianUint32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error(
                    "textures/sprites/disc.png has a truncated uint32 field.");
            }
            return (uint32_t(bytes[offset]) << 24u) |
                   (uint32_t(bytes[offset + 1u]) << 16u) |
                   (uint32_t(bytes[offset + 2u]) << 8u) |
                   uint32_t(bytes[offset + 3u]);
        }

        /** Returns PNG's Paeth predictor for one filtered byte. */
        uint8_t predictPoints2PngPaeth(
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
        eastl::vector<uint8_t> decodePoints2DiscPng(
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
                    "textures/sprites/disc.png has an invalid PNG signature.");
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
                        "textures/sprites/disc.png has a truncated chunk header.");
                }
                const uint32_t payloadSize =
                    readPoints2BigEndianUint32(pngBytes, offset);
                const size_t payloadOffset = offset + 8u;
                if (payloadOffset > pngBytes.size() ||
                    pngBytes.size() - payloadOffset < 4u ||
                    payloadSize > pngBytes.size() - payloadOffset - 4u)
                {
                    throw std::runtime_error(
                        "textures/sprites/disc.png has a truncated chunk payload.");
                }
                const char type0 = char(pngBytes[offset + 4u]);
                const char type1 = char(pngBytes[offset + 5u]);
                const char type2 = char(pngBytes[offset + 6u]);
                const char type3 = char(pngBytes[offset + 7u]);
                if (type0 == 'I' && type1 == 'H' && type2 == 'D' && type3 == 'R')
                {
                    if (ihdrSeen || payloadSize != 13u ||
                        readPoints2BigEndianUint32(pngBytes, payloadOffset) !=
                            DiscTextureExtent ||
                        readPoints2BigEndianUint32(pngBytes, payloadOffset + 4u) !=
                            DiscTextureExtent ||
                        pngBytes[payloadOffset + 8u] != 8u ||
                        pngBytes[payloadOffset + 9u] != 6u ||
                        pngBytes[payloadOffset + 10u] != 0u ||
                        pngBytes[payloadOffset + 11u] != 0u ||
                        pngBytes[payloadOffset + 12u] != 0u)
                    {
                        throw std::runtime_error(
                            "disc.png must remain a 32x32 noninterlaced RGBA8 PNG.");
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
                    "textures/sprites/disc.png lacks its required PNG chunks.");
            }
            const uint32_t zlibHeader =
                uint32_t(compressed[0u]) * 256u + uint32_t(compressed[1u]);
            if ((compressed[0u] & 0x0fu) != 8u || zlibHeader % 31u != 0u)
            {
                throw std::runtime_error(
                    "textures/sprites/disc.png has an unsupported zlib stream.");
            }

            constexpr size_t RowByteCount = DiscTextureExtent * 4u;
            constexpr size_t FilteredByteCount =
                DiscTextureExtent * (RowByteCount + 1u);
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
                    "Could not inflate the complete disc.png RGBA payload.");
            }

            eastl::vector<uint8_t> decoded(
                static_cast<size_t>(DiscTextureExtent) *
                DiscTextureExtent * 4u);
            for (uint32_t y = 0u; y < DiscTextureExtent; ++y)
            {
                const size_t filteredRow =
                    static_cast<size_t>(y) * (RowByteCount + 1u);
                const uint8_t filter = filtered[filteredRow];
                if (filter > 4u)
                {
                    throw std::runtime_error(
                        "disc.png uses an unsupported PNG row filter.");
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
                            predictPoints2PngPaeth(left, above, upperLeft);
                    }
                    decoded[static_cast<size_t>(y) * RowByteCount + x] =
                        uint8_t(uint32_t(source) + uint32_t(predictor));
                }
            }
            return decoded;
        }

        /** Flips source rows exactly like Three's default UNPACK_FLIP_Y path. */
        eastl::vector<uint8_t> flipPoints2TextureRows(
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
        eastl::vector<uint8_t> buildNextPoints2TextureMip(
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

        /** Builds all six CPU-authored raw-UNORM mips from the flipped browser bytes. */
        eastl::vector<eastl::vector<uint8_t>> buildPoints2TextureMips(
            const eastl::vector<uint8_t> &decoded)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(DiscTextureMipCount);
            result.push_back(
                flipPoints2TextureRows(decoded, DiscTextureExtent));
            uint32_t extent = DiscTextureExtent;
            while (extent > 1u)
            {
                result.push_back(
                    buildNextPoints2TextureMip(result.back(), extent));
                extent /= 2u;
            }
            if (result.size() != DiscTextureMipCount ||
                calculatePoints2Sha256(
                    result.front().data(),
                    result.front().size()) != FlippedDiscBaseSha256)
            {
                throw std::runtime_error(
                    "disc.png decoded texture bytes diverged from the Chrome r185 probe.");
            }
            return result;
        }

        /** Sets one Cartesian component selected by a Three BoxGeometry axis index. */
        void setPoints2Axis(glm::dvec3 &value, uint32_t axis, double component)
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

        /** Appends the exact Float32 SphereGeometry position stream and triangle indices. */
        void buildPoints2RawSphere(
            eastl::vector<glm::vec3> &positions,
            eastl::vector<uint32_t> &indices)
        {
            constexpr uint32_t WidthSegments = 68u;
            constexpr uint32_t HeightSegments = 38u;
            constexpr double Radius = 100.0;
            positions.clear();
            indices.clear();
            positions.reserve((WidthSegments + 1u) * (HeightSegments + 1u));
            indices.reserve(WidthSegments * (HeightSegments - 1u) * 6u);
            const double phiLength = Pi * 2.0;
            for (uint32_t iy = 0u; iy <= HeightSegments; ++iy)
            {
                const double v = double(iy) / double(HeightSegments);
                const double theta = v * Pi;
                const double y = Radius * std::cos(theta);
                const double ringRadius =
                    std::sqrt(Radius * Radius - y * y);
                for (uint32_t ix = 0u; ix <= WidthSegments; ++ix)
                {
                    const double u = double(ix) / double(WidthSegments);
                    const double phi = u * phiLength;
                    positions.push_back(glm::vec3(
                        static_cast<float>(-ringRadius * std::cos(phi)),
                        static_cast<float>(y),
                        static_cast<float>(ringRadius * std::sin(phi))));
                }
            }
            const uint32_t rowSize = WidthSegments + 1u;
            for (uint32_t iy = 0u; iy < HeightSegments; ++iy)
            {
                for (uint32_t ix = 0u; ix < WidthSegments; ++ix)
                {
                    const uint32_t a = iy * rowSize + ix + 1u;
                    const uint32_t b = iy * rowSize + ix;
                    const uint32_t c = (iy + 1u) * rowSize + ix;
                    const uint32_t d = (iy + 1u) * rowSize + ix + 1u;
                    if (iy != 0u)
                    {
                        indices.push_back(a);
                        indices.push_back(b);
                        indices.push_back(d);
                    }
                    if (iy != HeightSegments - 1u)
                    {
                        indices.push_back(b);
                        indices.push_back(c);
                        indices.push_back(d);
                    }
                }
            }
        }

        /** Appends one exact segmented Three BoxGeometry plane. */
        void appendPoints2BoxPlane(
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
                    setPoints2Axis(value, uAxis, x * uDirection);
                    setPoints2Axis(value, vAxis, y * vDirection);
                    setPoints2Axis(value, wAxis, depthHalf);
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

        /** Builds the exact Float32 segmented BoxGeometry position and index streams. */
        void buildPoints2RawBox(
            eastl::vector<glm::vec3> &positions,
            eastl::vector<uint32_t> &indices)
        {
            positions.clear();
            indices.clear();
            positions.reserve(726u);
            indices.reserve(3600u);
            appendPoints2BoxPlane(
                positions, indices, 2u, 1u, 0u,
                -1.0, -1.0, 80.0, 80.0, 80.0, 10u, 10u);
            appendPoints2BoxPlane(
                positions, indices, 2u, 1u, 0u,
                1.0, -1.0, 80.0, 80.0, -80.0, 10u, 10u);
            appendPoints2BoxPlane(
                positions, indices, 0u, 2u, 1u,
                1.0, 1.0, 80.0, 80.0, 80.0, 10u, 10u);
            appendPoints2BoxPlane(
                positions, indices, 0u, 2u, 1u,
                1.0, -1.0, 80.0, 80.0, -80.0, 10u, 10u);
            appendPoints2BoxPlane(
                positions, indices, 0u, 1u, 2u,
                1.0, -1.0, 80.0, 80.0, 80.0, 10u, 10u);
            appendPoints2BoxPlane(
                positions, indices, 0u, 1u, 2u,
                -1.0, -1.0, 80.0, 80.0, -80.0, 10u, 10u);
        }

        /** Packs Three's three 1e-4 quantized position components into one key. */
        uint64_t makePoints2MergeKey(const glm::vec3 &position)
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

        /** Reproduces BufferGeometryUtils.mergeVertices for position-only indexed geometry. */
        eastl::vector<glm::vec3> mergePoints2Vertices(
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
                        "points2 source geometry contains an invalid index.");
                }
                const uint64_t key = makePoints2MergeKey(positions[sourceIndex]);
                if (hashToIndex.find(key) == hashToIndex.end())
                {
                    hashToIndex.emplace(key, static_cast<uint32_t>(merged.size()));
                    merged.push_back(positions[sourceIndex]);
                }
            }
            return merged;
        }

        /** Returns Three's wrapped HSL channel interpolation for one hue phase. */
        double points2HueToRgb(double minimum, double maximum, double hue)
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
        glm::vec3 makePoints2HslColor(
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
                static_cast<float>(points2HueToRgb(
                    minimum, maximum, hue + 1.0 / 3.0)),
                static_cast<float>(points2HueToRgb(minimum, maximum, hue)),
                static_cast<float>(points2HueToRgb(
                    minimum, maximum, hue - 1.0 / 3.0)));
        }

        /** Builds merged logical attributes and their immutable four-corner expansion. */
        void buildPoints2ExpandedGeometry(
            eastl::vector<glm::vec3> &logicalPositions,
            eastl::vector<WebglCustomAttributesPoints2Vertex> &vertices,
            eastl::vector<uint32_t> &indices,
            eastl::vector<float> &sizes)
        {
            eastl::vector<glm::vec3> rawPositions;
            eastl::vector<uint32_t> rawIndices;
            buildPoints2RawSphere(rawPositions, rawIndices);
            eastl::vector<glm::vec3> spherePositions =
                mergePoints2Vertices(rawPositions, rawIndices);
            buildPoints2RawBox(rawPositions, rawIndices);
            eastl::vector<glm::vec3> boxPositions =
                mergePoints2Vertices(rawPositions, rawIndices);
            if (spherePositions.size() != SpherePointCount ||
                boxPositions.size() != BoxPointCount)
            {
                throw std::runtime_error(
                    "points2 mergeVertices counts diverged from Three r185.");
            }

            logicalPositions = spherePositions;
            logicalPositions.insert(
                logicalPositions.end(),
                boxPositions.begin(),
                boxPositions.end());
            if (calculatePoints2Sha256(
                    logicalPositions.data(),
                    logicalPositions.size() * sizeof(glm::vec3)) !=
                PositionSha256)
            {
                throw std::runtime_error(
                    "points2 merged Float32 positions diverged from Three r185.");
            }

            eastl::vector<glm::vec3> colors;
            colors.reserve(LogicalPointCount);
            sizes.resize(LogicalPointCount);
            for (uint32_t pointIndex = 0u;
                 pointIndex < LogicalPointCount;
                 ++pointIndex)
            {
                const glm::vec3 position = logicalPositions[pointIndex];
                if (pointIndex < SpherePointCount)
                {
                    colors.push_back(makePoints2HslColor(
                        0.01 + 0.1 *
                                   (double(pointIndex) /
                                    double(SpherePointCount)),
                        0.99,
                        (double(position.y) + 100.0) / 400.0));
                    sizes[pointIndex] = 10.0f;
                }
                else
                {
                    colors.push_back(makePoints2HslColor(
                        0.6,
                        0.75,
                        0.25 + double(position.y) / 200.0));
                    sizes[pointIndex] = 40.0f;
                }
            }
            if (calculatePoints2Sha256(
                    colors.data(),
                    colors.size() * sizeof(glm::vec3)) != ColorSha256)
            {
                throw std::runtime_error(
                    "points2 Float32 HSL colors diverged from Three r185.");
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
                        .customColor = colors[pointIndex],
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
                    "points2 point expansion counts diverged from the manifest.");
            }
        }

        /** Returns an identity matrix in Three's column-major element order. */
        Points2Matrix makePoints2IdentityMatrix()
        {
            return {
                1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0};
        }

        /** Multiplies two matrices with Three Matrix4's exact operation order. */
        Points2Matrix multiplyPoints2Matrices(
            const Points2Matrix &left,
            const Points2Matrix &right)
        {
            Points2Matrix result = {};
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

        /** Builds Three's 45-degree OpenGL projection matrix as binary64 elements. */
        Points2Matrix makePoints2ProjectionMatrix(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 45.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10000.0;
            const double top =
                NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                double(width) / double(height) * projectionHeight;
            const double projectionDepth = FarDistance - NearDistance;
            Points2Matrix projection = {};
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
        Points2Matrix makePoints2ModelMatrix(double rotation)
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

        /** Builds the current camera matrixWorldInverse after renderer matrix update. */
        Points2Matrix makePoints2ViewMatrix()
        {
            Points2Matrix view = makePoints2IdentityMatrix();
            view[14u] = -300.0;
            return view;
        }

        /** Converts a binary64 Three matrix to the Float32 uniform matrix ABI. */
        glm::mat4 makePoints2FloatMatrix(const Points2Matrix &source)
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

        /** Applies Three Vector3.applyMatrix4 and returns the perspective-divided z. */
        double projectPoints2Depth(
            const glm::vec3 &position,
            const Points2Matrix &matrix)
        {
            const double x = position.x;
            const double y = position.y;
            const double z = position.z;
            const double inverseW = 1.0 /
                (matrix[3u] * x + matrix[7u] * y +
                 matrix[11u] * z + matrix[15u]);
            return (matrix[2u] * x + matrix[6u] * y +
                    matrix[10u] * z + matrix[14u]) *
                   inverseW;
        }

        /** Rebuilds the six-index quad stream from Three's stable logical point sort. */
        void sortPoints2Indices(
            const eastl::vector<glm::vec3> &logicalPositions,
            const Points2Matrix &sortMatrix,
            eastl::vector<uint32_t> &expandedIndices,
            eastl::string &logicalIndexSha256,
            eastl::string &expandedIndexSha256)
        {
            eastl::vector<Points2DepthRecord> sortRecords;
            sortRecords.reserve(logicalPositions.size());
            for (uint32_t pointIndex = 0u;
                 pointIndex < logicalPositions.size();
                 ++pointIndex)
            {
                sortRecords.push_back({
                    .depth = projectPoints2Depth(
                        logicalPositions[pointIndex], sortMatrix),
                    .ordinal = pointIndex,
                });
            }
            eastl::sort(
                sortRecords.begin(),
                sortRecords.end(),
                Points2DepthDescending{});

            eastl::vector<uint16_t> logicalIndices;
            logicalIndices.reserve(sortRecords.size());
            expandedIndices.clear();
            expandedIndices.reserve(ExpandedIndexCount);
            for (const Points2DepthRecord &record : sortRecords)
            {
                logicalIndices.push_back(static_cast<uint16_t>(record.ordinal));
                const uint32_t baseVertex = record.ordinal * 4u;
                expandedIndices.push_back(baseVertex + 0u);
                expandedIndices.push_back(baseVertex + 1u);
                expandedIndices.push_back(baseVertex + 2u);
                expandedIndices.push_back(baseVertex + 0u);
                expandedIndices.push_back(baseVertex + 2u);
                expandedIndices.push_back(baseVertex + 3u);
            }
            logicalIndexSha256 = calculatePoints2Sha256(
                logicalIndices.data(),
                logicalIndices.size() * sizeof(uint16_t));
            expandedIndexSha256 = calculatePoints2Sha256(
                expandedIndices.data(),
                expandedIndices.size() * sizeof(uint32_t));
        }

        /** Advances one audited UUID/random segment and validates its exact resulting state. */
        void advancePoints2RandomState(
            ThreeCompat::DeterministicRandom &random,
            uint32_t drawCount,
            uint32_t expectedState,
            const char *stage)
        {
            for (uint32_t drawIndex = 0u; drawIndex < drawCount; ++drawIndex)
            {
                static_cast<void>(random.nextUint32());
            }
            if (random.getState() != expectedState)
            {
                throw std::runtime_error(
                    std::string("points2 RNG state diverged after ") + stage + ".");
            }
        }

        /** Replays the complete audited module, UUID, renderer, and lazy RNG sequence. */
        void replayPoints2RandomAudit(
            ThreeCompat::DeterministicRandom &random)
        {
            random.reset(Points2RandomSeed);
            advancePoints2RandomState(random, 76u, 2099579701u, "module imports");
            advancePoints2RandomState(random, 4u, 1729371842u, "PerspectiveCamera");
            advancePoints2RandomState(random, 4u, 2893702495u, "Scene");
            advancePoints2RandomState(random, 4u, 2970501421u, "SphereGeometry");
            advancePoints2RandomState(random, 4u, 4173600148u, "BoxGeometry");
            advancePoints2RandomState(random, 4u, 2818997995u, "sphere mergeVertices");
            advancePoints2RandomState(random, 4u, 830588404u, "box mergeVertices");
            advancePoints2RandomState(random, 4u, 3791256602u, "mergeGeometries");
            advancePoints2RandomState(random, 4u, 775777594u, "final BufferGeometry");
            advancePoints2RandomState(random, 8u, 3480931377u, "Texture and Source");
            advancePoints2RandomState(random, 4u, 1806173057u, "ShaderMaterial");
            advancePoints2RandomState(random, 4u, 1907300212u, "Points");
            advancePoints2RandomState(random, 36u, 2509245698u, "WebGLRenderer");
        }

        /** Validates the two locked scenarios and every explicit host parameter. */
        void validatePoints2Scenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_custom_attributes_points2")
            {
                throw std::invalid_argument(
                    "Points2 adapter requires case-id webgl_custom_attributes_points2.");
            }
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated-sorted-index" &&
                options.targetFrame == 60u;
            if (!initial && !animated)
            {
                throw std::invalid_argument(
                    "points2 requires initial/frame 0 or animated-sorted-index/frame 60.");
            }
            if (options.frameCount != options.targetFrame + 1u)
            {
                throw std::invalid_argument(
                    "points2 must advance every callback through the target frame.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument(
                    "points2 requires the locked 800x500 extent.");
            }
            if (options.randomSeed != Points2RandomSeed)
            {
                throw std::invalid_argument(
                    "points2 requires random seed 0x18500013.");
            }
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "points2 requires explicit --asset-root.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "points2 does not accept an input replay.");
            }
        }
    } // namespace

    void WebglCustomAttributesPoints2RuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validatePoints2Scenario(options);
        device = inDevice;
        buildPoints2ExpandedGeometry(
            logicalPositions,
            vertices,
            indices,
            sizes);
        replayPoints2RandomAudit(random);
        if (random.getState() != 2509245698u ||
            TotalRandomDrawCount != 160u)
        {
            throw std::runtime_error(
                "points2 final RNG audit diverged from r185.");
        }

        const std::filesystem::path discPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "sprites" / "disc.png";
        const eastl::vector<uint8_t> assetBytes =
            readPoints2AssetBytes(discPath);
        if (calculatePoints2Sha256(assetBytes.data(), assetBytes.size()) !=
            DiscAssetSha256)
        {
            throw std::invalid_argument(
                "textures/sprites/disc.png differs from the pinned Three r185 asset.");
        }
        textureMips = buildPoints2TextureMips(
            decodePoints2DiscPng(assetBytes));

        const Points2Matrix projection =
            makePoints2ProjectionMatrix(options.width, options.height);
        projectionMatrix = makePoints2FloatMatrix(projection);
        modelViewMatrix = glm::mat4(1.0f);
        virtualTimeMilliseconds = 0.0;
        renderedVirtualTimeMilliseconds = 0.0;
        timeValue = 0.0;
        currentRotation = 0.0;
        sortRotation = 0.0;
        previousRenderedRotation = 0.0;
        frameUpdateCount = 0u;
        cameraMatricesInitialized = false;
        captureWritten = false;
    }

    void WebglCustomAttributesPoints2RuntimeAdapter::advanceFrameState(
        uint32_t frameIndex)
    {
        if (frameIndex != frameUpdateCount)
        {
            throw std::logic_error(
                "points2 must advance sequentially from frame zero.");
        }
        renderedVirtualTimeMilliseconds = virtualTimeMilliseconds;
        timeValue = renderedVirtualTimeMilliseconds * 0.005;
        currentRotation = 0.02 * timeValue;
        for (uint32_t pointIndex = 0u;
             pointIndex < SpherePointCount;
             ++pointIndex)
        {
            sizes[pointIndex] = static_cast<float>(
                16.0 + 12.0 *
                           std::sin(0.1 * double(pointIndex) + timeValue));
        }

        const Points2Matrix projection = makePoints2ProjectionMatrix(800u, 500u);
        Points2Matrix sortModel = makePoints2IdentityMatrix();
        Points2Matrix sortView = makePoints2IdentityMatrix();
        sortRotation = 0.0;
        if (cameraMatricesInitialized)
        {
            sortRotation = previousRenderedRotation;
            sortModel = makePoints2ModelMatrix(sortRotation);
            sortView = makePoints2ViewMatrix();
        }
        const Points2Matrix sortMatrix = multiplyPoints2Matrices(
            multiplyPoints2Matrices(projection, sortView),
            sortModel);
        sortPoints2Indices(
            logicalPositions,
            sortMatrix,
            indices,
            logicalIndexSha256,
            expandedIndexSha256);

        const Points2Matrix currentModel =
            makePoints2ModelMatrix(currentRotation);
        const Points2Matrix currentModelView = multiplyPoints2Matrices(
            makePoints2ViewMatrix(), currentModel);
        modelViewMatrix = makePoints2FloatMatrix(currentModelView);
        sizeSha256 = calculatePoints2Sha256(
            sizes.data(), sizes.size() * sizeof(float));

        if (frameIndex == 0u)
        {
            if (sizeSha256 != InitialSizeSha256 ||
                logicalIndexSha256 != InitialLogicalIndexSha256 ||
                expandedIndexSha256 != InitialExpandedIndexSha256)
            {
                throw std::runtime_error(
                    "points2 frame-zero size or stable sort bytes diverged from r185.");
            }
        }
        else if (frameIndex == 60u)
        {
            if (sizeSha256 != AnimatedSizeSha256 ||
                logicalIndexSha256 != AnimatedLogicalIndexSha256 ||
                expandedIndexSha256 != AnimatedExpandedIndexSha256)
            {
                throw std::runtime_error(
                    "points2 frame-60 size or stale-matrix sort bytes diverged from r185.");
            }
        }

        previousRenderedRotation = currentRotation;
        cameraMatricesInitialized = true;
        ++frameUpdateCount;
        virtualTimeMilliseconds += FrameStepMilliseconds;
    }

    void WebglCustomAttributesPoints2RuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        if (!frameUploader)
        {
            throw std::logic_error(
                "points2 generated frame uploader is unavailable.");
        }
        advanceFrameState(frameIndex);
        frameUploader(sizes, indices, projectionMatrix, modelViewMatrix);
    }

    void WebglCustomAttributesPoints2RuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computePoints2RgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "points2 capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "points2 could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglCustomAttributesPoints2RuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        frameUploader = nullptr;
    }

    void WebglCustomAttributesPoints2RuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        preparePoints2OutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open points2 RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write the complete points2 RGBA capture.");
        }
    }

    void WebglCustomAttributesPoints2RuntimeAdapter::writeCaptureMetadata(
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
        preparePoints2OutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open points2 metadata path.");
        }
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_custom_attributes_points2\",\n"
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

    void WebglCustomAttributesPoints2RuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        preparePoints2OutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open points2 snapshot path.");
        }
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_custom_attributes_points2\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"canonicalState\": \""
               << (frameIndex == 0u
                       ? "seed-0x18500013-time-zero-3120-disc-billboards-pre-render-identity-matrices-index-sha-eb906a9f26f4-size-sha-5494433543fb-color-sha-52972aa0b087"
                       : "seed-0x18500013-fixed-step-60hz-one-second-frame59-matrix-index-sha-2d52c3c9f029-size-sha-ffdf685b2888-color-sha-52972aa0b087")
               << "\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"physicalCoverageDrawCount\": 1,\n"
               << "  \"sourceSpherePointCount\": " << SpherePointCount << ",\n"
               << "  \"sourceBoxPointCount\": " << BoxPointCount << ",\n"
               << "  \"logicalPointCount\": " << LogicalPointCount << ",\n"
               << "  \"expandedVertexCount\": " << ExpandedVertexCount << ",\n"
               << "  \"explicitIndexCount\": " << ExpandedIndexCount << ",\n"
               << "  \"expandedTriangleCount\": " << ExpandedIndexCount / 3u << ",\n"
               << "  \"vertexStrideBytes\": 32,\n"
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
               << "  \"dynamicIndexUploadCount\": " << frameUpdateCount << ",\n"
               << "  \"renderedVirtualTimeMs\": " << renderedVirtualTimeMilliseconds << ",\n"
               << "  \"dateNowSource\": \"performance.now-fixed-step\",\n"
               << "  \"timeValue\": " << timeValue << ",\n"
               << "  \"drawRotationY\": " << currentRotation << ",\n"
               << "  \"drawRotationZ\": " << currentRotation << ",\n"
               << "  \"sortRotationY\": " << sortRotation << ",\n"
               << "  \"sortRotationZ\": " << sortRotation << ",\n"
               << "  \"sortUsesIdentityCameraMatrix\": "
               << (frameIndex == 0u ? "true" : "false") << ",\n"
               << "  \"sortUsesIdentityObjectMatrix\": "
               << (frameIndex == 0u ? "true" : "false") << ",\n"
               << "  \"sortMatrixLagFrames\": "
               << (frameIndex == 0u ? 0u : 1u) << ",\n"
               << "  \"drawCameraPositionZ\": 300,\n"
               << "  \"drawUsesCurrentCameraMatrix\": true,\n"
               << "  \"drawUsesCurrentObjectMatrix\": true,\n"
               << "  \"sizeSha256\": \"" << sizeSha256.c_str() << "\",\n"
               << "  \"logicalIndexSha256\": \"" << logicalIndexSha256.c_str() << "\",\n"
               << "  \"expandedIndexSha256\": \"" << expandedIndexSha256.c_str() << "\",\n"
               << "  \"positionSha256\": \"" << PositionSha256 << "\",\n"
               << "  \"colorSha256\": \"" << ColorSha256 << "\",\n"
               << "  \"seed\": " << Points2RandomSeed << ",\n"
               << "  \"moduleImportRandomDrawCount\": 76,\n"
               << "  \"cameraRandomDrawCount\": 4,\n"
               << "  \"sceneRandomDrawCount\": 4,\n"
               << "  \"geometryUuidRandomDrawCount\": 24,\n"
               << "  \"textureSourceRandomDrawCount\": 8,\n"
               << "  \"materialRandomDrawCount\": 4,\n"
               << "  \"pointsRandomDrawCount\": 4,\n"
               << "  \"rendererConstructorRandomDrawCount\": 36,\n"
               << "  \"firstRenderLazyRandomDrawCount\": 0,\n"
               << "  \"subsequentFrameRandomDrawCount\": 0,\n"
               << "  \"totalRandomDrawCount\": " << TotalRandomDrawCount << ",\n"
               << "  \"finalRandomState\": " << random.getState() << ",\n"
               << "  \"texturePath\": \"textures/sprites/disc.png\",\n"
               << "  \"textureAssetSha256\": \"" << DiscAssetSha256 << "\",\n"
               << "  \"textureColorSpace\": \"NoColorSpace\",\n"
               << "  \"textureFormat\": \"rgba8unorm\",\n"
               << "  \"textureExtent\": 32,\n"
               << "  \"textureMipLevelCount\": " << DiscTextureMipCount << ",\n"
               << "  \"textureBaseGpuBytesSha256\": \"" << FlippedDiscBaseSha256 << "\",\n"
               << "  \"textureFlipY\": true,\n"
               << "  \"textureHiddenRgbPreserved\": true,\n"
               << "  \"mipmapGeneration\": \"explicit-cpu-raw-unorm-box-filter\",\n"
               << "  \"samplerAddressMode\": \"repeat\",\n"
               << "  \"samplerMinMagFilter\": \"linear\",\n"
               << "  \"samplerMipmapFilter\": \"linear\",\n"
               << "  \"blendColor\": \"src-alpha-one-minus-src-alpha\",\n"
               << "  \"blendAlpha\": \"one-one-minus-src-alpha\",\n"
               << "  \"depthTest\": true,\n"
               << "  \"depthWrite\": true,\n"
               << "  \"depthCompare\": \"less-equal\",\n"
               << "  \"rendererOutputColorSpace\": \"srgb\",\n"
               << "  \"shaderOutputTransfer\": \"none-custom-shader\",\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main-sorted-disc-points\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"sceneRoots\": [\n"
               << "    {\"id\":\"scene\",\"renderSetCount\":0,\"renderSetId\":null,\"renderSetType\":null,\"renderableObjectCount\":1,\"entityCount\":0,\"entities\":[],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main-sorted-disc-points\",\"renderClass\":\"WebglCustomAttributesPoints2MainPass\",\"renderSetId\":null,\"renderSetBindingCount\":0,\"drawMode\":\"explicit-indexed\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":true,\"usesExplicitDrawCount\":true}]}\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
