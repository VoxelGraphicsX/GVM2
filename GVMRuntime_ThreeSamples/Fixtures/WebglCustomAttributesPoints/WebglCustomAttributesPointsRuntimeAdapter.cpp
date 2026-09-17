#include "WebglCustomAttributesPointsRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <compression.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>

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
        constexpr uint32_t PointsRandomSeed = 0x18500012u;
        constexpr uint32_t LogicalPointCount = 100000u;
        constexpr uint32_t ExpandedVertexCount = LogicalPointCount * 4u;
        constexpr uint32_t ExpandedIndexCount = LogicalPointCount * 6u;
        constexpr uint32_t SparkTextureExtent = 32u;
        constexpr uint32_t SparkTextureMipCount = 6u;
        constexpr uint32_t PrePositionRandomDrawCount = 84u;
        constexpr uint32_t PositionRandomDrawCount = LogicalPointCount * 3u;
        constexpr uint32_t PostPositionObjectRandomDrawCount = 20u;
        constexpr uint32_t FirstRenderLazyRandomDrawCount = 36u;
        constexpr uint32_t ExpectedPrePositionRandomState = 2815147167u;
        constexpr uint32_t ExpectedPostPositionRandomState = 124731722u;
        constexpr uint32_t ExpectedPostObjectRandomState = 3825047168u;
        constexpr uint32_t ExpectedFinalRandomState = 1274764516u;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *SparkAssetSha256 =
            "f79341aaf44ea6c1a912cf5b29e8bd1fc045376f77b8bde8637e07b179a7d67e";
        constexpr const char *FlippedSparkBaseSha256 =
            "645fc710eba86744a7cba29ca6bcfcd7bafceab2612901bd50beecf74a48c16e";
        constexpr const char *PositionSha256 =
            "7cc037d18d7e4574f1e9fed3da1190962ce414c64ea175d94f02a6ea06861ca9";
        constexpr const char *ColorSha256 =
            "d47d91e3f1b739b0bd8b0b799c43964e56686d67ebb3056faf593e9ba100fd56";
        constexpr const char *InitialSizeSha256 =
            "8da63c3f661ea04611000a70ee66f408c0a258758fb3db7e59328718eae122dc";
        constexpr const char *AnimatedSizeSha256 =
            "d0c384965667d71149dcb1d1e671f9718a0316fece1eb4a7b291235b8d45f94f";

        static_assert(sizeof(WebglCustomAttributesPointsVertex) == 32u);
        static_assert(offsetof(WebglCustomAttributesPointsVertex, position) == 0u);
        static_assert(offsetof(WebglCustomAttributesPointsVertex, customColor) == 12u);
        static_assert(offsetof(WebglCustomAttributesPointsVertex, corner) == 24u);
        static_assert(sizeof(glm::vec3) == 12u);

        /** Creates parent directories for one explicitly requested point artifact. */
        void preparePointsOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computePointsRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_custom_attributes_points RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Reads one bounded immutable file into exact bytes. */
        eastl::vector<uint8_t> readPointsAssetBytes(
            const std::filesystem::path &inputPath)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open Three r185 spark asset: " + inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    "Three r185 spark asset has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete Three r185 spark asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte range. */
        eastl::string calculatePointsSha256(const void *bytes, size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error(
                    "webgl_custom_attributes_points SHA-256 input is too large.");
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

        /** Reads one network-order uint32 from a validated PNG byte offset. */
        uint32_t readPointsBigEndianUint32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error(
                    "textures/sprites/spark1.png has a truncated uint32 field.");
            }
            return (uint32_t(bytes[offset]) << 24u) |
                   (uint32_t(bytes[offset + 1u]) << 16u) |
                   (uint32_t(bytes[offset + 2u]) << 8u) |
                   uint32_t(bytes[offset + 3u]);
        }

        /** Returns PNG's Paeth predictor for one filtered byte. */
        uint8_t predictPointsPngPaeth(uint8_t left, uint8_t above, uint8_t upperLeft)
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
        eastl::vector<uint8_t> decodePointsSparkPng(
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
                    "textures/sprites/spark1.png has an invalid PNG signature.");
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
                        "textures/sprites/spark1.png has a truncated chunk header.");
                }
                const uint32_t payloadSize =
                    readPointsBigEndianUint32(pngBytes, offset);
                const size_t payloadOffset = offset + 8u;
                if (payloadSize > pngBytes.size() - payloadOffset - 4u)
                {
                    throw std::runtime_error(
                        "textures/sprites/spark1.png has a truncated chunk payload.");
                }
                const char type0 = char(pngBytes[offset + 4u]);
                const char type1 = char(pngBytes[offset + 5u]);
                const char type2 = char(pngBytes[offset + 6u]);
                const char type3 = char(pngBytes[offset + 7u]);
                if (type0 == 'I' && type1 == 'H' && type2 == 'D' && type3 == 'R')
                {
                    if (ihdrSeen || payloadSize != 13u ||
                        readPointsBigEndianUint32(pngBytes, payloadOffset) !=
                            SparkTextureExtent ||
                        readPointsBigEndianUint32(pngBytes, payloadOffset + 4u) !=
                            SparkTextureExtent ||
                        pngBytes[payloadOffset + 8u] != 8u ||
                        pngBytes[payloadOffset + 9u] != 6u ||
                        pngBytes[payloadOffset + 10u] != 0u ||
                        pngBytes[payloadOffset + 11u] != 0u ||
                        pngBytes[payloadOffset + 12u] != 0u)
                    {
                        throw std::runtime_error(
                            "spark1.png must remain a 32x32 noninterlaced RGBA8 PNG.");
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
                    "textures/sprites/spark1.png lacks its required PNG chunks.");
            }
            const uint32_t zlibHeader =
                uint32_t(compressed[0u]) * 256u + uint32_t(compressed[1u]);
            if ((compressed[0u] & 0x0fu) != 8u || zlibHeader % 31u != 0u)
            {
                throw std::runtime_error(
                    "textures/sprites/spark1.png has an unsupported zlib stream.");
            }

            constexpr size_t RowByteCount = SparkTextureExtent * 4u;
            constexpr size_t FilteredByteCount =
                SparkTextureExtent * (RowByteCount + 1u);
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
                    "Could not inflate the complete spark1.png RGBA payload.");
            }

            eastl::vector<uint8_t> decoded(
                static_cast<size_t>(SparkTextureExtent) *
                SparkTextureExtent * 4u);
            for (uint32_t y = 0u; y < SparkTextureExtent; ++y)
            {
                const size_t filteredRow =
                    static_cast<size_t>(y) * (RowByteCount + 1u);
                const uint8_t filter = filtered[filteredRow];
                if (filter > 4u)
                {
                    throw std::runtime_error(
                        "spark1.png uses an unsupported PNG row filter.");
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
                        predictor = uint8_t((uint32_t(left) + uint32_t(above)) / 2u);
                    }
                    else if (filter == 4u)
                    {
                        predictor = predictPointsPngPaeth(left, above, upperLeft);
                    }
                    decoded[static_cast<size_t>(y) * RowByteCount + x] =
                        uint8_t(uint32_t(source) + uint32_t(predictor));
                }
            }
            return decoded;
        }

        /** Flips source rows exactly like Three's default UNPACK_FLIP_Y path. */
        eastl::vector<uint8_t> flipPointsTextureRows(
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
        eastl::vector<uint8_t> buildNextPointsTextureMip(
            const eastl::vector<uint8_t> &source,
            uint32_t sourceExtent)
        {
            if (sourceExtent < 2u || source.size() !=
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
                        target[targetOffset] = static_cast<uint8_t>((sum + 2u) / 4u);
                    }
                }
            }
            return target;
        }

        /** Builds all six CPU-authored raw-UNORM mips from the flipped browser bytes. */
        eastl::vector<eastl::vector<uint8_t>> buildPointsTextureMips(
            const eastl::vector<uint8_t> &decoded)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(SparkTextureMipCount);
            result.push_back(flipPointsTextureRows(decoded, SparkTextureExtent));
            uint32_t extent = SparkTextureExtent;
            while (extent > 1u)
            {
                result.push_back(buildNextPointsTextureMip(result.back(), extent));
                extent /= 2u;
            }
            if (result.size() != SparkTextureMipCount ||
                calculatePointsSha256(
                    result.front().data(), result.front().size()) !=
                    FlippedSparkBaseSha256)
            {
                throw std::runtime_error(
                    "spark1.png decoded texture bytes diverged from the Chrome r185 probe.");
            }
            return result;
        }

        /** Returns one exact JavaScript Math.random value as a binary64 number. */
        double nextPointsRandomDouble(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Returns Three's wrapped HSL channel interpolation for one hue phase. */
        double pointsHueToRgb(double minimum, double maximum, double hue)
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
                return minimum + (maximum - minimum) * 6.0 * (2.0 / 3.0 - hue);
            }
            return minimum;
        }

        /** Evaluates Three Color.setHSL in the default linear working color space. */
        glm::vec3 makePointsHslColor(double hue, double saturation, double lightness)
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
                static_cast<float>(pointsHueToRgb(
                    minimum, maximum, hue + 1.0 / 3.0)),
                static_cast<float>(pointsHueToRgb(minimum, maximum, hue)),
                static_cast<float>(pointsHueToRgb(
                    minimum, maximum, hue - 1.0 / 3.0)));
        }

        /** Builds all random point records and their non-instanced four-corner expansion. */
        void buildPointsExpandedGeometry(
            ThreeCompat::DeterministicRandom &random,
            eastl::vector<WebglCustomAttributesPointsVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            eastl::vector<float> &sizes)
        {
            constexpr double Radius = 200.0;
            eastl::vector<glm::vec3> positions;
            eastl::vector<glm::vec3> colors;
            positions.reserve(LogicalPointCount);
            colors.reserve(LogicalPointCount);
            sizes.assign(LogicalPointCount, 10.0f);
            for (uint32_t pointIndex = 0u;
                 pointIndex < LogicalPointCount;
                 ++pointIndex)
            {
                const glm::vec3 position(
                    static_cast<float>(
                        (nextPointsRandomDouble(random) * 2.0 - 1.0) * Radius),
                    static_cast<float>(
                        (nextPointsRandomDouble(random) * 2.0 - 1.0) * Radius),
                    static_cast<float>(
                        (nextPointsRandomDouble(random) * 2.0 - 1.0) * Radius));
                positions.push_back(position);
                const double phase =
                    double(pointIndex) / double(LogicalPointCount);
                colors.push_back(position.x < 0.0f
                    ? makePointsHslColor(0.5 + 0.1 * phase, 0.7, 0.5)
                    : makePointsHslColor(0.1 * phase, 0.9, 0.5));
            }
            if (random.getState() != ExpectedPostPositionRandomState ||
                calculatePointsSha256(
                    positions.data(), positions.size() * sizeof(glm::vec3)) !=
                    PositionSha256 ||
                calculatePointsSha256(
                    colors.data(), colors.size() * sizeof(glm::vec3)) !=
                    ColorSha256)
            {
                throw std::runtime_error(
                    "webgl_custom_attributes_points logical attributes diverged from r185.");
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
                const uint32_t baseVertex = uint32_t(vertices.size());
                for (const glm::vec2 &corner : corners)
                {
                    WebglCustomAttributesPointsVertex vertex;
                    vertex.position = positions[pointIndex];
                    vertex.customColor = colors[pointIndex];
                    vertex.corner = corner;
                    vertices.push_back(vertex);
                }
                indices.push_back(baseVertex + 0u);
                indices.push_back(baseVertex + 1u);
                indices.push_back(baseVertex + 2u);
                indices.push_back(baseVertex + 0u);
                indices.push_back(baseVertex + 2u);
                indices.push_back(baseVertex + 3u);
            }
            if (vertices.size() != ExpandedVertexCount ||
                indices.size() != ExpandedIndexCount ||
                sizes.size() != LogicalPointCount)
            {
                throw std::runtime_error(
                    "webgl_custom_attributes_points expansion counts diverged from r185.");
            }
        }

        /** Builds Three's original OpenGL positive-Y and negative-one-to-one projection. */
        glm::mat4 makePointsProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 40.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10000.0;
            const double top =
                NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
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

        /** Builds Three's quaternion-composed Z rotation followed by camera translation. */
        glm::mat4 makePointsModelView(double rotation)
        {
            const double quaternionZ = std::sin(rotation * 0.5);
            const double quaternionW = std::cos(rotation * 0.5);
            const double doubledZ = quaternionZ + quaternionZ;
            const double zz = quaternionZ * doubledZ;
            const double wz = quaternionW * doubledZ;
            glm::mat4 modelView(1.0f);
            modelView[0u][0u] = static_cast<float>(1.0 - zz);
            modelView[0u][1u] = static_cast<float>(wz);
            modelView[1u][0u] = static_cast<float>(-wz);
            modelView[1u][1u] = static_cast<float>(1.0 - zz);
            modelView[3u][2u] = -300.0f;
            return modelView;
        }

        /** Validates the two locked scenarios and every explicit host parameter. */
        void validatePointsScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_custom_attributes_points")
            {
                throw std::invalid_argument(
                    "Point adapter requires case-id webgl_custom_attributes_points.");
            }
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            if (!initial && !animated)
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes_points requires initial/frame 0 or animated/frame 60.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes_points requires the locked 800x500 extent.");
            }
            if (options.randomSeed != PointsRandomSeed)
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes_points requires random seed 0x18500012.");
            }
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes_points requires explicit --asset-root.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "webgl_custom_attributes_points does not accept an input replay.");
            }
        }
    } // namespace

    void WebglCustomAttributesPointsRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validatePointsScenario(options);
        device = inDevice;
        random.reset(PointsRandomSeed);
        // r185 imports consume 76 UUID draws. PerspectiveCamera and Scene add
        // four each before the page begins its 300,000 position draws.
        for (uint32_t drawIndex = 0u;
             drawIndex < PrePositionRandomDrawCount;
             ++drawIndex)
        {
            static_cast<void>(random.nextUint32());
        }
        if (random.getState() != ExpectedPrePositionRandomState)
        {
            throw std::runtime_error(
                "webgl_custom_attributes_points pre-position RNG state diverged from r185.");
        }
        buildPointsExpandedGeometry(random, vertices, indices, sizes);
        if (random.getState() != ExpectedPostPositionRandomState)
        {
            throw std::runtime_error(
                "webgl_custom_attributes_points position RNG state diverged from r185.");
        }
        // BufferGeometry, Texture/Source, ShaderMaterial, and Points consume
        // 4, 8, 4, and 4 draws after the logical point loop.
        for (uint32_t drawIndex = 0u;
             drawIndex < PostPositionObjectRandomDrawCount;
             ++drawIndex)
        {
            static_cast<void>(random.nextUint32());
        }
        if (random.getState() != ExpectedPostObjectRandomState)
        {
            throw std::runtime_error(
                "webgl_custom_attributes_points post-object RNG state diverged from r185.");
        }

        const std::filesystem::path sparkPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "sprites" / "spark1.png";
        const eastl::vector<uint8_t> assetBytes =
            readPointsAssetBytes(sparkPath);
        if (calculatePointsSha256(assetBytes.data(), assetBytes.size()) !=
            SparkAssetSha256)
        {
            throw std::invalid_argument(
                "textures/sprites/spark1.png differs from the pinned Three r185 asset.");
        }
        textureMips = buildPointsTextureMips(decodePointsSparkPng(assetBytes));
        projectionMatrix = makePointsProjection(options.width, options.height);
        virtualTimeMilliseconds = 0.0;
        renderedVirtualTimeMilliseconds = 0.0;
        timeValue = 0.0;
        rotationZ = 0.0;
        frameUpdateCount = 0u;
        captureWritten = false;
    }

    void WebglCustomAttributesPointsRuntimeAdapter::advanceFrameState(
        uint32_t frameIndex,
        uint32_t targetFrame)
    {
        if (frameIndex != frameUpdateCount)
        {
            throw std::logic_error(
                "webgl_custom_attributes_points must advance sequentially from frame 0.");
        }
        renderedVirtualTimeMilliseconds = virtualTimeMilliseconds;
        timeValue =
            (ReferenceEpochMilliseconds + renderedVirtualTimeMilliseconds) * 0.005;
        rotationZ = 0.01 * timeValue;
        for (uint32_t pointIndex = 0u;
             pointIndex < LogicalPointCount;
             ++pointIndex)
        {
            sizes[pointIndex] = static_cast<float>(
                14.0 + 13.0 * std::sin(0.1 * double(pointIndex) + timeValue));
        }
        modelViewMatrix = makePointsModelView(rotationZ);
        ++frameUpdateCount;
        virtualTimeMilliseconds += FrameStepMilliseconds;

        if (frameIndex == targetFrame)
        {
            sizeSha256 = calculatePointsSha256(
                sizes.data(), sizes.size() * sizeof(float));
            const char *expected = targetFrame == 0u
                ? InitialSizeSha256
                : AnimatedSizeSha256;
            if (sizeSha256 != expected)
            {
                throw std::runtime_error(
                    "webgl_custom_attributes_points size Float32 bytes diverged from r185.");
            }
        }
    }

    void WebglCustomAttributesPointsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        if (!frameUploader)
        {
            throw std::logic_error(
                "webgl_custom_attributes_points generated frame uploader is unavailable.");
        }
        advanceFrameState(frameIndex, options.targetFrame);
        frameUploader(
            sizes,
            projectionMatrix,
            modelViewMatrix,
            frameIndex == options.targetFrame);
    }

    void WebglCustomAttributesPointsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (frameIndex == 0u)
        {
            // The first renderer.render lazily creates nine UUID-bearing
            // resources after the frame-zero callback.
            for (uint32_t drawIndex = 0u;
                 drawIndex < FirstRenderLazyRandomDrawCount;
                 ++drawIndex)
            {
                static_cast<void>(random.nextUint32());
            }
            if (random.getState() != ExpectedFinalRandomState)
            {
                throw std::runtime_error(
                    "webgl_custom_attributes_points first-render RNG state diverged from r185.");
            }
        }
        if (captureWritten || frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount = computePointsRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_custom_attributes_points capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_custom_attributes_points could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglCustomAttributesPointsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        frameUploader = nullptr;
    }

    void WebglCustomAttributesPointsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        preparePointsOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_custom_attributes_points RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_custom_attributes_points RGBA capture.");
        }
    }

    void WebglCustomAttributesPointsRuntimeAdapter::writeCaptureMetadata(
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
        preparePointsOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_custom_attributes_points metadata path.");
        }
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_custom_attributes_points\",\n"
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

    void WebglCustomAttributesPointsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        preparePointsOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_custom_attributes_points snapshot path.");
        }
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_custom_attributes_points\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"canonicalState\": \""
               << (frameIndex == 0u
                       ? "seed-0x18500012-time-zero-100000-noninstanced-spark-billboards-after-size-wave-update"
                       : "seed-0x18500012-fixed-step-60hz-one-second-size-wave-and-z-rotation")
               << "\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"logicalScenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"computePassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"logicalDrawCommandCount\": 1,\n"
               << "  \"physicalCoverageDrawCount\": 1,\n"
               << "  \"logicalPointCount\": " << LogicalPointCount << ",\n"
               << "  \"expandedVertexCount\": " << ExpandedVertexCount << ",\n"
               << "  \"explicitIndexCount\": " << ExpandedIndexCount << ",\n"
               << "  \"expandedTriangleCount\": " << ExpandedIndexCount / 3u << ",\n"
               << "  \"standaloneGeometryBufferCount\": 2,\n"
               << "  \"dynamicSizeBufferCount\": 1,\n"
               << "  \"primitiveTopology\": \"triangle-list\",\n"
               << "  \"pointExpansion\": \"four-corners-six-indices-noninstanced\",\n"
               << "  \"projectionConvention\": \"three-opengl-positive-y-negative-one-to-one\",\n"
               << "  \"dslClipConversion\": \"y-negate-and-z-half-range-after-billboard-expansion\",\n"
               << "  \"antialias\": false,\n"
               << "  \"dynamicAttribute\": \"size\",\n"
               << "  \"dynamicSizeUploadCount\": " << frameUpdateCount << ",\n"
               << "  \"sequentialFrameAdvanceCount\": " << frameUpdateCount << ",\n"
               << "  \"renderedVirtualTimeMs\": " << renderedVirtualTimeMilliseconds << ",\n"
               << "  \"timeValue\": " << timeValue << ",\n"
               << "  \"rotationZ\": " << rotationZ << ",\n"
               << "  \"sizeSha256\": \"" << sizeSha256.c_str() << "\",\n"
               << "  \"positionSha256\": \"" << PositionSha256 << "\",\n"
               << "  \"colorSha256\": \"" << ColorSha256 << "\",\n"
               << "  \"seed\": " << PointsRandomSeed << ",\n"
               << "  \"prePositionRandomDrawCount\": " << PrePositionRandomDrawCount << ",\n"
               << "  \"prePositionRandomState\": " << ExpectedPrePositionRandomState << ",\n"
               << "  \"positionRandomDrawCount\": " << PositionRandomDrawCount << ",\n"
               << "  \"postPositionRandomState\": " << ExpectedPostPositionRandomState << ",\n"
               << "  \"postPositionObjectRandomDrawCount\": " << PostPositionObjectRandomDrawCount << ",\n"
               << "  \"postObjectRandomState\": " << ExpectedPostObjectRandomState << ",\n"
               << "  \"firstRenderLazyRandomDrawCount\": " << FirstRenderLazyRandomDrawCount << ",\n"
               << "  \"totalRandomDrawCount\": "
               << PrePositionRandomDrawCount + PositionRandomDrawCount +
                      PostPositionObjectRandomDrawCount +
                      FirstRenderLazyRandomDrawCount
               << ",\n"
               << "  \"finalRandomState\": " << random.getState() << ",\n"
               << "  \"texturePath\": \"textures/sprites/spark1.png\",\n"
               << "  \"textureAssetSha256\": \"" << SparkAssetSha256 << "\",\n"
               << "  \"textureColorSpace\": \"NoColorSpace\",\n"
               << "  \"textureFormat\": \"rgba8unorm\",\n"
               << "  \"textureExtent\": 32,\n"
               << "  \"textureMipLevelCount\": " << SparkTextureMipCount << ",\n"
               << "  \"textureBaseGpuBytesSha256\": \"" << FlippedSparkBaseSha256 << "\",\n"
               << "  \"textureFlipY\": true,\n"
               << "  \"textureHiddenRgbPreserved\": true,\n"
               << "  \"mipmapGeneration\": \"explicit-cpu-raw-unorm-box-filter\",\n"
               << "  \"samplerAddressMode\": \"clamp-to-edge\",\n"
               << "  \"samplerMinMagFilter\": \"linear\",\n"
               << "  \"samplerMipmapFilter\": \"linear\",\n"
               << "  \"blendColor\": \"src-alpha-plus-one\",\n"
               << "  \"blendAlpha\": \"one-plus-one\",\n"
               << "  \"depthTest\": false,\n"
               << "  \"depthWrite\": false,\n"
               << "  \"rendererOutputColorSpace\": \"srgb\",\n"
               << "  \"shaderOutputTransfer\": \"none-custom-shader-does-not-call-linearToOutputTexel\",\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main-spark-points\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
