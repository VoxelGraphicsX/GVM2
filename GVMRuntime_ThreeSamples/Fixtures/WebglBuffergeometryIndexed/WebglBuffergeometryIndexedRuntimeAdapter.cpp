#include "WebglBuffergeometryIndexedRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

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
        constexpr uint32_t IndexedRandomSeed = 0x18500005u;
        constexpr uint32_t IndexedVertexCount = 121u;
        constexpr uint32_t IndexedIndexCount = 600u;
        constexpr uint32_t IndexedTriangleCount = 200u;
        constexpr uint32_t IndexedWireSegmentCount = IndexedTriangleCount * 3u;
        constexpr uint32_t IndexedWireVertexCount = IndexedWireSegmentCount * 2u;
        constexpr uint32_t GridSegments = 10u;
        constexpr double GridSize = 20.0;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *WireframeReplaySha256 =
            "ddb31fa3f0adaad97b5b644f14020d00be88f1537d1b23ce0be0bf2442b55398";

        static_assert(sizeof(WebglBuffergeometryIndexedVertex) == 36u);
        static_assert(offsetof(WebglBuffergeometryIndexedVertex, position) == 0u);
        static_assert(offsetof(WebglBuffergeometryIndexedVertex, normal) == 12u);
        static_assert(offsetof(WebglBuffergeometryIndexedVertex, color) == 24u);
        static_assert(sizeof(WebglBuffergeometryIndexedWireVertex) == 56u);
        static_assert(offsetof(WebglBuffergeometryIndexedWireVertex, startPosition) == 0u);
        static_assert(offsetof(WebglBuffergeometryIndexedWireVertex, endPosition) == 12u);
        static_assert(offsetof(WebglBuffergeometryIndexedWireVertex, startColor) == 24u);
        static_assert(offsetof(WebglBuffergeometryIndexedWireVertex, endColor) == 36u);
        static_assert(offsetof(WebglBuffergeometryIndexedWireVertex, lineCoordinate) == 48u);

        /** Creates parent directories for one explicitly requested indexed artifact. */
        void prepareIndexedOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeIndexedRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_buffergeometry_indexed RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Reads one bounded replay file as exact bytes for immutable validation. */
        eastl::vector<uint8_t> readIndexedReplayBytes(
            const std::filesystem::path &inputPath)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open webgl_buffergeometry_indexed input replay: " +
                    inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    "webgl_buffergeometry_indexed input replay has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete webgl_buffergeometry_indexed input replay.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte sequence. */
        eastl::string calculateIndexedSha256(const eastl::vector<uint8_t> &bytes)
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

        /** Resolves the explicitly supplied wireframe replay without environment state. */
        std::filesystem::path resolveIndexedReplayPath(
            const ThreeSampleHostOptions &options)
        {
            const std::filesystem::path requested(options.inputReplayPath.c_str());
            if (requested.is_absolute() && std::filesystem::is_regular_file(requested))
            {
                return requested;
            }
            if (!requested.empty() && std::filesystem::is_regular_file(requested))
            {
                return std::filesystem::absolute(requested);
            }
            if (!options.assetRoot.empty())
            {
                const std::filesystem::path assetPath =
                    std::filesystem::path(options.assetRoot.c_str()) / requested;
                if (std::filesystem::is_regular_file(assetPath))
                {
                    return assetPath;
                }
            }
            throw std::invalid_argument(
                "webgl_buffergeometry_indexed could not resolve --input-replay.");
        }

        /** Reproduces the bootstrap's repeated 60 Hz virtual timestamp. */
        double makeIndexedVirtualTimeMilliseconds(uint32_t frameIndex)
        {
            double virtualTimeMilliseconds = 0.0;
            for (uint32_t index = 0u; index < frameIndex; ++index)
            {
                virtualTimeMilliseconds += FrameStepMilliseconds;
            }
            return virtualTimeMilliseconds;
        }

        /** Converts one authored sRGB channel to Three r185's linear working space. */
        double indexedSrgbToLinear(double value)
        {
            if (value < 0.04045)
            {
                return value * 0.0773993808;
            }
            return std::pow(value * 0.9478672986 + 0.0521327014, 2.4);
        }

        /** Builds the exact 121 shared vertices and 600 source indices from r185. */
        void buildIndexedGridGeometry(
            eastl::vector<WebglBuffergeometryIndexedVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr double HalfSize = GridSize * 0.5;
            constexpr double SegmentSize = GridSize / double(GridSegments);
            vertices.clear();
            vertices.reserve(IndexedVertexCount);
            for (uint32_t row = 0u; row <= GridSegments; ++row)
            {
                const double y = double(row) * SegmentSize - HalfSize;
                for (uint32_t column = 0u; column <= GridSegments; ++column)
                {
                    const double x = double(column) * SegmentSize - HalfSize;
                    WebglBuffergeometryIndexedVertex vertex;
                    vertex.position = float3(
                        static_cast<float>(x),
                        static_cast<float>(-y),
                        0.0f);
                    vertex.normal = float3(0.0f, 0.0f, 1.0f);
                    vertex.color = float3(
                        static_cast<float>(indexedSrgbToLinear(x / GridSize + 0.5)),
                        static_cast<float>(indexedSrgbToLinear(y / GridSize + 0.5)),
                        1.0f);
                    vertices.push_back(vertex);
                }
            }

            indices.clear();
            indices.reserve(IndexedIndexCount);
            for (uint32_t row = 0u; row < GridSegments; ++row)
            {
                for (uint32_t column = 0u; column < GridSegments; ++column)
                {
                    const uint32_t a =
                        row * (GridSegments + 1u) + column + 1u;
                    const uint32_t b =
                        row * (GridSegments + 1u) + column;
                    const uint32_t c =
                        (row + 1u) * (GridSegments + 1u) + column;
                    const uint32_t d =
                        (row + 1u) * (GridSegments + 1u) + column + 1u;
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);
                    indices.push_back(b);
                    indices.push_back(c);
                    indices.push_back(d);
                }
            }
            if (vertices.size() != IndexedVertexCount ||
                indices.size() != IndexedIndexCount)
            {
                throw std::runtime_error(
                    "webgl_buffergeometry_indexed source geometry count diverged from r185.");
            }
        }

        /** Appends two immutable line-list endpoints for one source wire edge. */
        void appendIndexedWireEdge(
            eastl::vector<WebglBuffergeometryIndexedWireVertex> &wireVertices,
            const WebglBuffergeometryIndexedVertex &start,
            const WebglBuffergeometryIndexedVertex &end)
        {
            constexpr float Coordinates[2][2] = {
                {0.0f, 0.0f},
                {1.0f, 0.0f},
            };
            for (uint32_t corner = 0u; corner < 2u; ++corner)
            {
                WebglBuffergeometryIndexedWireVertex vertex;
                vertex.startPosition = start.position;
                vertex.endPosition = end.position;
                vertex.startColor = start.color;
                vertex.endColor = end.color;
                vertex.lineCoordinate = float2(
                    Coordinates[corner][0],
                    Coordinates[corner][1]);
                wireVertices.push_back(vertex);
            }
        }

        /** Emits every indexed triangle edge in Three's exact wireframe index order. */
        void buildIndexedWireGeometry(
            const eastl::vector<WebglBuffergeometryIndexedVertex> &vertices,
            const eastl::vector<uint32_t> &indices,
            eastl::vector<WebglBuffergeometryIndexedWireVertex> &wireVertices)
        {
            wireVertices.clear();
            wireVertices.reserve(IndexedWireVertexCount);
            for (uint32_t indexOffset = 0u;
                 indexOffset < indices.size();
                 indexOffset += 3u)
            {
                const WebglBuffergeometryIndexedVertex &a =
                    vertices[indices[indexOffset]];
                const WebglBuffergeometryIndexedVertex &b =
                    vertices[indices[indexOffset + 1u]];
                const WebglBuffergeometryIndexedVertex &c =
                    vertices[indices[indexOffset + 2u]];
                appendIndexedWireEdge(wireVertices, a, b);
                appendIndexedWireEdge(wireVertices, b, c);
                appendIndexedWireEdge(wireVertices, c, a);
            }
            if (wireVertices.size() != IndexedWireVertexCount)
            {
                throw std::runtime_error(
                    "webgl_buffergeometry_indexed wire expansion count is invalid.");
            }
        }

        /** Builds Three's perspective matrix in the current backend clip-space convention. */
        glm::mat4 makeIndexedProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 27.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 3500.0;
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
                -2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(
                -FarDistance / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -FarDistance * NearDistance / projectionDepth);
            return projection;
        }

        /** Builds Three's XYZ Euler model matrix followed by camera-z translation. */
        glm::mat4 makeIndexedModelView(double rotationX, double rotationY)
        {
            const double sineX = std::sin(rotationX);
            const double cosineX = std::cos(rotationX);
            const double sineY = std::sin(rotationY);
            const double cosineY = std::cos(rotationY);

            glm::mat4 model(1.0f);
            model[0u][0u] = static_cast<float>(cosineY);
            model[0u][1u] = static_cast<float>(sineX * sineY);
            model[0u][2u] = static_cast<float>(-cosineX * sineY);
            model[1u][0u] = 0.0f;
            model[1u][1u] = static_cast<float>(cosineX);
            model[1u][2u] = static_cast<float>(sineX);
            model[2u][0u] = static_cast<float>(sineY);
            model[2u][1u] = static_cast<float>(-sineX * cosineY);
            model[2u][2u] = static_cast<float>(cosineX * cosineY);

            glm::mat4 view(1.0f);
            view[3u][2u] = -64.0f;
            return view * model;
        }
    } // namespace

    void WebglBuffergeometryIndexedRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_buffergeometry_indexed")
        {
            throw std::invalid_argument(
                "Indexed adapter requires case-id webgl_buffergeometry_indexed.");
        }
        const bool initial =
            options.scenarioId == "initial-filled" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated-filled" && options.targetFrame == 60u;
        const bool wireframe =
            options.scenarioId == "wireframe" && options.targetFrame == 61u;
        if (!initial && !animated && !wireframe)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_indexed requires initial-filled/frame 0, "
                "animated-filled/frame 60, or wireframe/frame 61.");
        }
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_indexed requires the locked 800x500 extent.");
        }
        if (options.randomSeed != IndexedRandomSeed)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_indexed requires random seed 0x18500005.");
        }
        if (!wireframe && !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Filled indexed scenarios must not consume an input replay.");
        }
        if (wireframe && options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "The indexed wireframe scenario requires --input-replay.");
        }

        device = inDevice;
        wireframeEnabled = wireframe;
        if (wireframe)
        {
            replaySha256 = calculateIndexedSha256(
                readIndexedReplayBytes(resolveIndexedReplayPath(options)));
            if (replaySha256 != WireframeReplaySha256)
            {
                throw std::invalid_argument(
                    "Indexed wireframe replay SHA-256 differs from the locked GUI sequence.");
            }
        }

        buildIndexedGridGeometry(vertices, indices);
        buildIndexedWireGeometry(vertices, indices, wireVertices);
        const double virtualTimeMilliseconds =
            makeIndexedVirtualTimeMilliseconds(options.targetFrame);
        timeSeconds =
            (ReferenceEpochMilliseconds + virtualTimeMilliseconds) * 0.001;
        rotationX = timeSeconds * 0.25;
        rotationY = timeSeconds * 0.5;
        modelViewProjection =
            makeIndexedProjection(options.width, options.height) *
            makeIndexedModelView(rotationX, rotationY);
    }

    void WebglBuffergeometryIndexedRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglBuffergeometryIndexedRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computeIndexedRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Indexed capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "Indexed capture could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglBuffergeometryIndexedRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglBuffergeometryIndexedRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareIndexedOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_indexed RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_buffergeometry_indexed RGBA capture.");
        }
    }

    void WebglBuffergeometryIndexedRuntimeAdapter::writeCaptureMetadata(
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
        prepareIndexedOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_indexed metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_indexed\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"inputReplay\": ";
        if (!wireframeEnabled)
        {
            output << "null\n";
        }
        else
        {
            output << "{\n"
                   << "    \"schemaVersion\": 1,\n"
                   << "    \"sha256\": \"" << replaySha256.c_str() << "\",\n"
                   << "    \"caseId\": \"webgl_buffergeometry_indexed\",\n"
                   << "    \"scenarioId\": \"wireframe\",\n"
                   << "    \"captureFrame\": 61,\n"
                   << "    \"eventCount\": 2,\n"
                   << "    \"lastEventFrame\": 0,\n"
                   << "    \"target\": \"body > canvas\"\n"
                   << "  }\n";
        }
        output << "}\n";
    }

    void WebglBuffergeometryIndexedRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareIndexedOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_indexed snapshot output path.");
        }
        const char *scenePassName =
            wireframeEnabled ? "main-wireframe" : "main-filled";
        const char *renderClassName = wireframeEnabled
            ? "WebglBuffergeometryIndexedWireframePass"
            : "WebglBuffergeometryIndexedFilledPass";
        const char *drawMode = wireframeEnabled
            ? "explicit-indexed-line-list"
            : "explicit-indexed-triangle-list";
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_indexed\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"screenPasses\": [],\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"physicalCoverageDrawCount\": 1,\n"
               << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\","
                  "\"scenePass\":\""
               << scenePassName
               << "\",\"entityOrdinal\":0}],\n"
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
               << "    \"scenePasses\": [{\"name\":\""
               << scenePassName
               << "\",\"renderClass\":\""
               << renderClassName
               << "\",\"renderSetId\":null,\"renderSetBindingCount\":0,"
                  "\"drawMode\":\""
               << drawMode
               << "\",\"invocationCount\":1,\"drawCommandCount\":1,"
                  "\"usesStandaloneGeometry\":true,"
                  "\"usesExplicitDrawCount\":true}]\n"
               << "  }],\n"
               << "  \"sourceVertexCount\": " << vertices.size() << ",\n"
               << "  \"sourceIndexCount\": " << indices.size() << ",\n"
               << "  \"sourceTriangleCount\": " << IndexedTriangleCount << ",\n"
               << "  \"wireSegmentCount\": " << IndexedWireSegmentCount << ",\n"
               << "  \"wireExpandedVertexCount\": " << wireVertices.size() << ",\n"
               << "  \"vertexStrideBytes\": 36,\n"
               << "  \"wireVertexStrideBytes\": 56,\n"
               << "  \"standaloneGeometryBufferCount\": 3,\n"
               << "  \"wireframe\": " << (wireframeEnabled ? "true" : "false") << ",\n"
               << "  \"timeSeconds\": " << timeSeconds << ",\n"
               << "  \"rotationX\": " << rotationX << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"cameraFovDegrees\": 27,\n"
               << "  \"cameraNear\": 1,\n"
               << "  \"cameraFar\": 3500,\n"
               << "  \"cameraPositionZ\": 64,\n"
               << "  \"hemisphereLightIntensity\": 3,\n"
               << "  \"antialiasResolve\": \"disabled-single-sample\"\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
