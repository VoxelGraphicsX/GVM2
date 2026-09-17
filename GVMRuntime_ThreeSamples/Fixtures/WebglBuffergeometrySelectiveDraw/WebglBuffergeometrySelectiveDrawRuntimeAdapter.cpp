#include "WebglBuffergeometrySelectiveDrawRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t SelectiveRandomSeed = 0x1850000eu;
        constexpr uint32_t LatitudeCount = 100u;
        constexpr uint32_t LongitudeCount = 200u;
        constexpr uint32_t SourceLineCount =
            LatitudeCount * LongitudeCount;
        constexpr uint32_t SourceVertexCount = SourceLineCount * 2u;
        constexpr uint32_t ExpandedVertexCount = SourceLineCount * 2u;
        constexpr uint32_t PreGeometryRandomCallCount = 88u;
        constexpr uint32_t PreVisibilityRandomCallCount = 44u;
        constexpr uint32_t PreVisibilityRandomState = 2347915795u;
        constexpr uint32_t PostVisibilityRandomState = 248151130u;
        constexpr double Radius = 1.0;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *HideReplaySha256 =
            "6d38e4787716906c619f68ccfc21b24880ca38d86229e7fc421d656e0792d7b2";

        /** Stores one unexpanded r185 line while its visibility mask is resolved. */
        struct SelectiveSourceLine
        {
            float3 endPosition;
            float3 startColor;
            float3 endColor;
        };

        static_assert(sizeof(WebglBuffergeometrySelectiveDrawVertex) == 60u);
        static_assert(
            offsetof(
                WebglBuffergeometrySelectiveDrawVertex,
                startPosition) == 0u);
        static_assert(
            offsetof(
                WebglBuffergeometrySelectiveDrawVertex,
                endPosition) == 12u);
        static_assert(
            offsetof(
                WebglBuffergeometrySelectiveDrawVertex,
                startColor) == 24u);
        static_assert(
            offsetof(
                WebglBuffergeometrySelectiveDrawVertex,
                endColor) == 36u);
        static_assert(
            offsetof(
                WebglBuffergeometrySelectiveDrawVertex,
                lineCoordinate) == 48u);
        static_assert(
            offsetof(
                WebglBuffergeometrySelectiveDrawVertex,
                visible) == 56u);

        /** Creates parent directories for one explicitly requested capture artifact. */
        void prepareSelectiveOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeSelectiveRgbaByteCount(
            uint32_t width,
            uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_buffergeometry_selective_draw RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Reads one bounded replay file as exact bytes for immutable validation. */
        eastl::vector<uint8_t> readSelectiveReplayBytes(
            const std::filesystem::path &inputPath)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open webgl_buffergeometry_selective_draw input replay: " +
                    inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) >
                    std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    "webgl_buffergeometry_selective_draw input replay has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete webgl_buffergeometry_selective_draw input replay.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte sequence. */
        eastl::string calculateSelectiveSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
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

        /** Resolves the explicitly supplied hide replay without environment state. */
        std::filesystem::path resolveSelectiveReplayPath(
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
                "webgl_buffergeometry_selective_draw could not resolve --input-replay.");
        }

        /** Advances one xorshift32 value exactly like the reference bootstrap. */
        double nextSelectiveRandom(uint32_t &randomState)
        {
            uint32_t value = randomState;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            randomState = value;
            return double(randomState >> 8u) / 16777216.0;
        }

        /** Reproduces the bootstrap's repeated 60 Hz virtual timestamp. */
        double makeSelectiveVirtualTimeMilliseconds(uint32_t frameIndex)
        {
            double virtualTimeMilliseconds = 0.0;
            for (uint32_t index = 0u; index < frameIndex; ++index)
            {
                virtualTimeMilliseconds += FrameStepMilliseconds;
            }
            return virtualTimeMilliseconds;
        }

        /** Evaluates Three's private HSL helper for one wrapped hue. */
        double selectiveHueToRgb(double lower, double upper, double hue)
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
                return lower + (upper - lower) * 6.0 * hue;
            }
            if (hue < 0.5)
            {
                return upper;
            }
            if (hue < 2.0 / 3.0)
            {
                return lower +
                    (upper - lower) * 6.0 * (2.0 / 3.0 - hue);
            }
            return lower;
        }

        /** Converts one r185 HSL color in the linear working space to float RGB. */
        float3 makeSelectiveHslColor(double hue, double lightness)
        {
            hue -= std::floor(hue);
            const double upper = lightness <= 0.5
                ? lightness * 2.0
                : 1.0;
            const double lower = 2.0 * lightness - upper;
            return float3(
                static_cast<float>(
                    selectiveHueToRgb(lower, upper, hue + 1.0 / 3.0)),
                static_cast<float>(
                    selectiveHueToRgb(lower, upper, hue)),
                static_cast<float>(
                    selectiveHueToRgb(lower, upper, hue - 1.0 / 3.0)));
        }

        /** Appends two line-list endpoints for one independent selective line. */
        void appendSelectiveLine(
            eastl::vector<WebglBuffergeometrySelectiveDrawVertex> &vertices,
            float3 endPosition,
            float3 startColor,
            float3 endColor,
            bool visible)
        {
            constexpr float Coordinates[2][2] = {
                {0.0f, 0.0f},
                {1.0f, 0.0f},
            };
            for (uint32_t corner = 0u; corner < 2u; ++corner)
            {
                WebglBuffergeometrySelectiveDrawVertex vertex;
                vertex.startPosition = float3(0.0f, 0.0f, 0.0f);
                vertex.endPosition = endPosition;
                vertex.startColor = startColor;
                vertex.endColor = endColor;
                vertex.lineCoordinate = float2(
                    Coordinates[corner][0],
                    Coordinates[corner][1]);
                vertex.visible = visible ? 1.0f : 0.0f;
                vertices.push_back(vertex);
            }
        }

        /** Builds 20,000 deterministic lines and applies the canonical hide mask. */
        void buildSelectiveGeometry(
            bool applyCulling,
            eastl::vector<WebglBuffergeometrySelectiveDrawVertex> &vertices,
            uint32_t &visibleLineCount,
            uint32_t &culledLineCount)
        {
            eastl::vector<SelectiveSourceLine> sourceLines;
            sourceLines.reserve(SourceLineCount);
            uint32_t randomState = SelectiveRandomSeed;
            for (uint32_t randomCall = 0u;
                 randomCall < PreGeometryRandomCallCount;
                 ++randomCall)
            {
                nextSelectiveRandom(randomState);
            }
            for (uint32_t latitudeIndex = 0u;
                 latitudeIndex < LatitudeCount;
                 ++latitudeIndex)
            {
                for (uint32_t longitudeIndex = 0u;
                     longitudeIndex < LongitudeCount;
                     ++longitudeIndex)
                {
                    const double latitude =
                        nextSelectiveRandom(randomState) * Pi / 50.0 +
                        double(latitudeIndex) / double(LatitudeCount) * Pi;
                    const double longitude =
                        nextSelectiveRandom(randomState) * Pi / 50.0 +
                        double(longitudeIndex) /
                            double(LongitudeCount) * 2.0 * Pi;
                    SelectiveSourceLine line;
                    line.endPosition = float3(
                        static_cast<float>(
                            Radius * std::sin(latitude) *
                            std::cos(longitude)),
                        static_cast<float>(
                            Radius * std::cos(latitude)),
                        static_cast<float>(
                            Radius * std::sin(latitude) *
                            std::sin(longitude)));
                    line.startColor =
                        makeSelectiveHslColor(latitude / Pi, 0.2);
                    line.endColor =
                        makeSelectiveHslColor(latitude / Pi, 0.7);
                    sourceLines.push_back(line);
                }
            }
            if (sourceLines.size() != SourceLineCount)
            {
                throw std::runtime_error(
                    "webgl_buffergeometry_selective_draw source line count diverged from r185.");
            }

            if (applyCulling)
            {
                for (uint32_t randomCall = 0u;
                     randomCall < PreVisibilityRandomCallCount;
                     ++randomCall)
                {
                    nextSelectiveRandom(randomState);
                }
                if (randomState != PreVisibilityRandomState)
                {
                    throw std::runtime_error(
                        "webgl_buffergeometry_selective_draw pre-visibility random state drifted.");
                }
            }
            vertices.clear();
            vertices.reserve(ExpandedVertexCount);
            culledLineCount = 0u;
            for (const SelectiveSourceLine &line : sourceLines)
            {
                const bool visible =
                    !applyCulling ||
                    !(nextSelectiveRandom(randomState) > 0.75);
                if (!visible)
                {
                    ++culledLineCount;
                }
                appendSelectiveLine(
                    vertices,
                    line.endPosition,
                    line.startColor,
                    line.endColor,
                    visible);
            }
            visibleLineCount = SourceLineCount - culledLineCount;
            if (vertices.size() != ExpandedVertexCount ||
                (applyCulling &&
                 (culledLineCount != 4986u ||
                  randomState != PostVisibilityRandomState)) ||
                (!applyCulling && culledLineCount != 0u))
            {
                throw std::runtime_error(
                    "webgl_buffergeometry_selective_draw expanded visibility data is invalid.");
            }
        }

        /** Builds Three's perspective matrix in the backend clip-space convention. */
        glm::dmat4 makeSelectiveProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 45.0;
            constexpr double NearDistance = 0.01;
            constexpr double FarDistance = 10.0;
            const double top =
                NearDistance *
                std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                double(width) / double(height) * projectionHeight;
            const double projectionDepth = FarDistance - NearDistance;

            glm::dmat4 projection(0.0);
            projection[0u][0u] =
                2.0 * NearDistance / projectionWidth;
            projection[1u][1u] =
                2.0 * NearDistance / projectionHeight;
            projection[2u][2u] =
                -(FarDistance + NearDistance) / projectionDepth;
            projection[2u][3u] = -1.0;
            projection[3u][2u] =
                -2.0 * FarDistance * NearDistance / projectionDepth;
            return projection;
        }

        /** Builds Three's XYZ Euler model matrix followed by camera-z translation. */
        glm::dmat4 makeSelectiveModelView(
            double rotationX,
            double rotationY)
        {
            const double sineX = std::sin(rotationX);
            const double cosineX = std::cos(rotationX);
            const double sineY = std::sin(rotationY);
            const double cosineY = std::cos(rotationY);

            glm::dmat4 model(1.0);
            model[0u][0u] = cosineY;
            model[0u][1u] = sineX * sineY;
            model[0u][2u] = -cosineX * sineY;
            model[1u][0u] = 0.0f;
            model[1u][1u] = cosineX;
            model[1u][2u] = sineX;
            model[2u][0u] = sineY;
            model[2u][1u] = -sineX * cosineY;
            model[2u][2u] = cosineX * cosineY;

            glm::dmat4 view(1.0);
            view[3u][2u] = -3.5;
            return view * model;
        }
    } // namespace

    void WebglBuffergeometrySelectiveDrawRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_buffergeometry_selective_draw")
        {
            throw std::invalid_argument(
                "Selective draw adapter requires case-id webgl_buffergeometry_selective_draw.");
        }
        const bool initial =
            options.scenarioId == "initial-all-visible" &&
            options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" &&
            options.targetFrame == 60u;
        const bool culled =
            options.scenarioId == "culled" &&
            options.targetFrame == 61u;
        if (!initial && !animated && !culled)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_selective_draw requires "
                "initial-all-visible/frame 0, animated/frame 60, or culled/frame 61.");
        }
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_selective_draw requires the locked 800x500 extent.");
        }
        if (options.randomSeed != SelectiveRandomSeed)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_selective_draw requires random seed 0x1850000e.");
        }
        if (!culled && !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "All-visible selective draw scenarios must not consume an input replay.");
        }
        if (culled && options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "The selective draw culled scenario requires --input-replay.");
        }

        device = inDevice;
        cullingApplied = culled;
        if (culled)
        {
            replaySha256 = calculateSelectiveSha256(
                readSelectiveReplayBytes(
                    resolveSelectiveReplayPath(options)));
            if (replaySha256 != HideReplaySha256)
            {
                throw std::invalid_argument(
                    "Selective draw replay SHA-256 differs from the locked hide sequence.");
            }
        }

        uint32_t initialVisibleLineCount = 0u;
        uint32_t initialCulledLineCount = 0u;
        buildSelectiveGeometry(
            false,
            initialVertices,
            initialVisibleLineCount,
            initialCulledLineCount);
        if (initialVisibleLineCount != SourceLineCount ||
            initialCulledLineCount != 0u)
        {
            throw std::runtime_error(
                "webgl_buffergeometry_selective_draw initial visibility upload is invalid.");
        }
        if (cullingApplied)
        {
            buildSelectiveGeometry(
                true,
                vertices,
                visibleLineCount,
                culledLineCount);
        }
        else
        {
            vertices = initialVertices;
            visibleLineCount = SourceLineCount;
            culledLineCount = 0u;
        }
        const double virtualTimeMilliseconds =
            makeSelectiveVirtualTimeMilliseconds(options.targetFrame);
        timeSeconds =
            (ReferenceEpochMilliseconds + virtualTimeMilliseconds) * 0.001;
        rotationX = timeSeconds * 0.25;
        rotationY = timeSeconds * 0.5;
        projectionMatrix = glm::mat4(
            makeSelectiveProjection(options.width, options.height));
        modelViewMatrix = glm::mat4(
            makeSelectiveModelView(rotationX, rotationY));
    }

    void WebglBuffergeometrySelectiveDrawRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglBuffergeometrySelectiveDrawRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount =
            computeSelectiveRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Selective draw capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "Selective draw capture could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
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

    void WebglBuffergeometrySelectiveDrawRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglBuffergeometrySelectiveDrawRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.captureRgbaPath.c_str());
        prepareSelectiveOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_selective_draw RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_buffergeometry_selective_draw RGBA capture.");
        }
    }

    void WebglBuffergeometrySelectiveDrawRuntimeAdapter::writeCaptureMetadata(
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
        prepareSelectiveOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_selective_draw metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_selective_draw\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str()
               << "\",\n"
               << "  \"backend\": \""
               << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u
               << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"inputReplay\": ";
        if (!cullingApplied)
        {
            output << "null\n";
        }
        else
        {
            output << "{\n"
                   << "    \"schemaVersion\": 1,\n"
                   << "    \"sha256\": \"" << replaySha256.c_str()
                   << "\",\n"
                   << "    \"caseId\": \"webgl_buffergeometry_selective_draw\",\n"
                   << "    \"scenarioId\": \"culled\",\n"
                   << "    \"captureFrame\": 61,\n"
                   << "    \"eventCount\": 2,\n"
                   << "    \"lastEventFrame\": 0,\n"
                   << "    \"target\": \"#hideLines\"\n"
                   << "  }\n";
        }
        output << "}\n";
    }

    void WebglBuffergeometrySelectiveDrawRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(
            options.sceneSnapshotPath.c_str());
        prepareSelectiveOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_selective_draw snapshot output path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_selective_draw\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"screenPasses\": "
                  "[],\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"physicalCoverageDrawCount\": 1,\n"
               << "  \"scenePassSequence\": "
                  "[{\"sceneRoot\":\"scene\","
                  "\"scenePass\":\"main-selective-lines\","
                  "\"entityOrdinal\":0}],\n"
               << "  \"sceneRoots\": [{\n"
               << "    \"id\": \"scene\",\n"
               << "    \"renderSetCount\": 0,\n"
               << "    \"renderSetId\": null,\n"
               << "    \"renderSetType\": null,\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"entityCount\": 0,\n"
               << "    \"entities\": [],\n"
               << "    \"drawCommandCount\": 1,\n"
               << "    \"directDrawFallback\": false,\n"
               << "    \"scenePasses\": [{"
                  "\"name\":\"main-selective-lines\","
                  "\"renderClass\":"
                  "\"WebglBuffergeometrySelectiveDrawMainPass\","
                  "\"renderSetId\":null,"
                  "\"renderSetBindingCount\":0,"
                  "\"drawMode\":\"explicit-line-list\","
                  "\"invocationCount\":1,"
                  "\"drawCommandCount\":1,"
                  "\"usesStandaloneGeometry\":true,"
                  "\"usesExplicitDrawCount\":true}]\n"
               << "  }],\n"
               << "  \"sourceLineCount\": " << SourceLineCount << ",\n"
               << "  \"sourceVertexCount\": " << SourceVertexCount << ",\n"
               << "  \"expandedVertexCount\": " << vertices.size() << ",\n"
               << "  \"visibleLineCount\": " << visibleLineCount << ",\n"
               << "  \"culledLineCount\": " << culledLineCount << ",\n"
               << "  \"preGeometryRandomCallCount\": "
               << PreGeometryRandomCallCount << ",\n"
               << "  \"canonicalHidePreVisibilityRandomCallCount\": "
               << PreVisibilityRandomCallCount << ",\n"
               << "  \"canonicalHidePreVisibilityRandomState\": "
               << PreVisibilityRandomState << ",\n"
               << "  \"canonicalHidePostVisibilityRandomState\": "
               << PostVisibilityRandomState << ",\n"
               << "  \"vertexStrideBytes\": 60,\n"
               << "  \"standaloneGeometryBufferCount\": 1,\n"
               << "  \"customVertexColorAttribute\": true,\n"
               << "  \"customVisibilityAttribute\": true,\n"
               << "  \"fragmentVisibilityDiscard\": true,\n"
               << "  \"vertexBufferUploadCount\": "
               << (cullingApplied ? 2 : 1) << ",\n"
               << "  \"initialVisibilityUploadAllVisible\": true,\n"
               << "  \"replayVisibilityUpdateApplied\": "
               << (cullingApplied ? "true" : "false") << ",\n"
               << "  \"visibilityReplayApplied\": "
               << (cullingApplied ? "true" : "false") << ",\n"
               << "  \"timeSeconds\": " << timeSeconds << ",\n"
               << "  \"rotationX\": " << rotationX << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"cameraFovDegrees\": 45,\n"
               << "  \"cameraNear\": 0.01,\n"
               << "  \"cameraFar\": 10,\n"
               << "  \"cameraPositionZ\": 3.5,\n"
               << "  \"antialiasResolve\": "
                  "\"disabled-single-sample\"\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
