#include "WebglBuffergeometryLinesRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"

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
        constexpr uint32_t LineVertexCount = 10000u;
        constexpr uint32_t LineRandomSeed = DefaultThreeRandomSeed;
        constexpr uint32_t UpstreamPreVertexRandomDrawCount = 92u;
        constexpr uint32_t UpstreamPostVertexRandomDrawCount = 40u;
        constexpr uint32_t ExpectedFinalRandomState = 1523321029u;
        constexpr double LineCoordinateExtent = 800.0;

        static_assert(sizeof(WebglBuffergeometryLinesVertex) == 36u);
        static_assert(offsetof(WebglBuffergeometryLinesVertex, position) == 0u);
        static_assert(offsetof(WebglBuffergeometryLinesVertex, morphPosition) == 12u);
        static_assert(offsetof(WebglBuffergeometryLinesVertex, color) == 24u);

        /** Creates parent directories for one explicitly requested line artifact. */
        void prepareLinesOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes the tightly packed RGBA8 byte count while rejecting overflow. */
        uint64_t computeLinesRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_buffergeometry_lines RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Validates the two manifest-locked line scenarios and capture frames. */
        void validateLinesScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_buffergeometry_lines")
            {
                throw std::invalid_argument(
                    "Line runtime adapter requires case-id webgl_buffergeometry_lines.");
            }
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated-morph" && options.targetFrame == 60u;
            if (!initial && !animated)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_lines requires initial/frame 0 or animated-morph/frame 60.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_lines requires the locked 800x500 extent.");
            }
            if (options.randomSeed != LineRandomSeed)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_lines requires the default Three random seed.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_lines scenarios do not accept input replay files.");
            }
        }

        /** Returns one JavaScript-precision random coordinate before Float32 attribute conversion. */
        double nextLineCoordinate(ThreeCompat::DeterministicRandom &random)
        {
            constexpr double InverseTwentyFourBitRange = 1.0 / 16777216.0;
            const double unitValue =
                double(random.nextUint32() >> 8u) * InverseTwentyFourBitRange;
            return unitValue * LineCoordinateExtent - LineCoordinateExtent * 0.5;
        }

        /** Builds the exact r185 base, color, and absolute morph vertex streams. */
        uint32_t buildLinesVertices(eastl::vector<WebglBuffergeometryLinesVertex> &vertices)
        {
            ThreeCompat::DeterministicRandom random(LineRandomSeed);
            for (uint32_t drawIndex = 0u;
                 drawIndex < UpstreamPreVertexRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }

            vertices.clear();
            vertices.resize(LineVertexCount);
            for (uint32_t vertexIndex = 0u; vertexIndex < LineVertexCount; ++vertexIndex)
            {
                const double x = nextLineCoordinate(random);
                const double y = nextLineCoordinate(random);
                const double z = nextLineCoordinate(random);
                WebglBuffergeometryLinesVertex &vertex = vertices[vertexIndex];
                vertex.position = float3(float(x), float(y), float(z));
                vertex.color = float3(
                    float(x / LineCoordinateExtent + 0.5),
                    float(y / LineCoordinateExtent + 0.5),
                    float(z / LineCoordinateExtent + 0.5));
            }
            for (uint32_t vertexIndex = 0u; vertexIndex < LineVertexCount; ++vertexIndex)
            {
                const double x = nextLineCoordinate(random);
                const double y = nextLineCoordinate(random);
                const double z = nextLineCoordinate(random);
                vertices[vertexIndex].morphPosition = float3(float(x), float(y), float(z));
            }
            for (uint32_t drawIndex = 0u;
                 drawIndex < UpstreamPostVertexRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            return random.getState();
        }

        /** Builds Three r185's perspective with the DSL backend clip-space conversion. */
        glm::mat4 makeLinesProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 27.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 4000.0;
            constexpr double Pi = 3.14159265358979323846;
            const double top = NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                (double(width) / double(height)) * projectionHeight;
            const double projectionDepth = FarDistance - NearDistance;

            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(-2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(-FarDistance / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -FarDistance * NearDistance / projectionDepth);
            return projection;
        }

        /** Builds Three's XYZ Euler object matrix followed by the fixed camera view. */
        glm::mat4 makeLinesModelView(float rotationX, float rotationY)
        {
            const double sineX = std::sin(double(rotationX));
            const double cosineX = std::cos(double(rotationX));
            const double sineY = std::sin(double(rotationY));
            const double cosineY = std::cos(double(rotationY));

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
            view[3u][2u] = -2750.0f;
            return view * model;
        }
    } // namespace

    void WebglBuffergeometryLinesRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateLinesScenario(options);
        device = inDevice;
        finalRandomState = buildLinesVertices(vertices);
        if (finalRandomState != ExpectedFinalRandomState)
        {
            throw std::runtime_error(
                "webgl_buffergeometry_lines deterministic random stream diverged from r185.");
        }

        timeSeconds = float(double(options.targetFrame) / 60.0);
        rotationX = timeSeconds * 0.25f;
        rotationY = timeSeconds * 0.5f;
        morphWeight = float(std::abs(std::sin(double(timeSeconds) * 0.5)));
        modelViewProjection =
            makeLinesProjection(options.width, options.height) *
            makeLinesModelView(rotationX, rotationY);
    }

    void WebglBuffergeometryLinesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglBuffergeometryLinesRuntimeAdapter::afterFrame(
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

        const uint64_t byteCount = computeLinesRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_buffergeometry_lines capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_buffergeometry_lines could not access the graphics queue.");
        }
        graphicsQueue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();

        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglBuffergeometryLinesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglBuffergeometryLinesRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareLinesOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_lines RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_buffergeometry_lines RGBA capture.");
        }
    }

    void WebglBuffergeometryLinesRuntimeAdapter::writeCaptureMetadata(
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
        prepareLinesOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_lines metadata output path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_buffergeometry_lines\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }

    void WebglBuffergeometryLinesRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareLinesOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_lines snapshot output path.");
        }
        output << std::fixed << std::setprecision(8);
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_buffergeometry_lines\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"canonicalState\": \""
               << (options.scenarioId == "initial"
                       ? "seed-0x12345678-morph-weight-zero"
                       : "seed-0x12345678-fixed-step-60hz-morph-and-rotation")
               << "\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"explicitVertexCount\": " << LineVertexCount << ",\n"
               << "  \"vertexStrideBytes\": 36,\n"
               << "  \"standaloneGeometryBufferCount\": 1,\n"
               << "  \"primitiveTopology\": \"line-strip\",\n"
               << "  \"morphTargetCount\": 1,\n"
               << "  \"morphTargetsRelative\": false,\n"
               << "  \"seed\": " << LineRandomSeed << ",\n"
               << "  \"upstreamPreVertexRandomDrawCount\": "
               << UpstreamPreVertexRandomDrawCount << ",\n"
               << "  \"upstreamPostVertexRandomDrawCount\": "
               << UpstreamPostVertexRandomDrawCount << ",\n"
               << "  \"finalRandomState\": " << finalRandomState << ",\n"
               << "  \"timeSeconds\": " << timeSeconds << ",\n"
               << "  \"rotationX\": " << rotationX << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"morphWeight\": " << morphWeight << ",\n"
               << "  \"cameraFovDegrees\": 27,\n"
               << "  \"cameraNear\": 1,\n"
               << "  \"cameraFar\": 4000,\n"
               << "  \"cameraPositionZ\": 2750,\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main-line-strip\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"sceneRoots\": [\n"
               << "    {\"id\":\"scene\",\"renderSetCount\":0,\"renderSetId\":null,"
                  "\"renderSetType\":null,\"renderableObjectCount\":1,\"entityCount\":0,"
                  "\"entities\":[],\"drawCommandCount\":1,\"directDrawFallback\":false,"
                  "\"scenePasses\":[{\"name\":\"main-line-strip\","
                  "\"renderClass\":\"WebglBuffergeometryLinesMainPass\","
                  "\"renderSetId\":null,\"renderSetBindingCount\":0,"
                  "\"drawMode\":\"explicit-nonindexed\",\"invocationCount\":1,"
                  "\"drawCommandCount\":1,\"usesStandaloneGeometry\":true,"
                  "\"usesExplicitDrawCount\":true}]}\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
