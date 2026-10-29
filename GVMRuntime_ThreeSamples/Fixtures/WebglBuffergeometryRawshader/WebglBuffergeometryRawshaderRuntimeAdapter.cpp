#include "WebglBuffergeometryRawshaderRuntimeAdapter.hpp"

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
        constexpr uint32_t RawshaderVertexCount = 600u;
        constexpr uint32_t RawshaderRandomSeed = DefaultThreeRandomSeed;
        constexpr uint32_t UpstreamModuleRandomDrawCount = 76u;
        constexpr uint32_t UpstreamObjectUuidRandomDrawCount = 12u;
        constexpr uint32_t UpstreamPreVertexRandomDrawCount =
            UpstreamModuleRandomDrawCount + UpstreamObjectUuidRandomDrawCount;

        static_assert(sizeof(WebglBuffergeometryRawshaderVertex) == 16u);
        static_assert(offsetof(WebglBuffergeometryRawshaderVertex, position) == 0u);
        static_assert(offsetof(WebglBuffergeometryRawshaderVertex, colorRgba8) == 12u);

        /** Creates parent directories for one explicitly requested raw-shader artifact. */
        void prepareRawshaderOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes the tightly packed RGBA8 byte count while rejecting overflow. */
        uint64_t computeRawshaderRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_buffergeometry_rawshader RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Validates the two manifest-locked raw-shader scenarios and capture frames. */
        void validateRawshaderScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_buffergeometry_rawshader")
            {
                throw std::invalid_argument(
                    "Raw-shader runtime adapter requires case-id webgl_buffergeometry_rawshader.");
            }
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            if (!initial && !animated)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_rawshader requires initial/frame 0 or animated/frame 60.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_rawshader requires the locked 800x500 extent.");
            }
            if (options.randomSeed != RawshaderRandomSeed)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_rawshader requires the default Three random seed.");
            }
        }

        /** Converts one upper-24-bit random value to JavaScript Uint8Array truncation. */
        uint32_t makeRawshaderColorByte(uint32_t randomValue)
        {
            const uint64_t upperTwentyFourBits = uint64_t(randomValue >> 8u);
            return static_cast<uint32_t>((upperTwentyFourBits * 255u) >> 24u);
        }

        /** Builds the exact Three r185 xorshift32 vertex stream and returns its final state. */
        uint32_t buildRawshaderVertices(
            eastl::vector<WebglBuffergeometryRawshaderVertex> &vertices)
        {
            ThreeCompat::DeterministicRandom random(RawshaderRandomSeed);
            for (uint32_t drawIndex = 0u;
                 drawIndex < UpstreamPreVertexRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            vertices.clear();
            vertices.reserve(RawshaderVertexCount);
            for (uint32_t vertexIndex = 0u; vertexIndex < RawshaderVertexCount; ++vertexIndex)
            {
                WebglBuffergeometryRawshaderVertex vertex;
                const float positionX = random.nextFloat() - 0.5f;
                const float positionY = random.nextFloat() - 0.5f;
                const float positionZ = random.nextFloat() - 0.5f;
                vertex.position = float3(positionX, positionY, positionZ);
                const uint32_t red = makeRawshaderColorByte(random.nextUint32());
                const uint32_t green = makeRawshaderColorByte(random.nextUint32());
                const uint32_t blue = makeRawshaderColorByte(random.nextUint32());
                const uint32_t alpha = makeRawshaderColorByte(random.nextUint32());
                vertex.colorRgba8 =
                    red | (green << 8u) | (blue << 16u) | (alpha << 24u);
                vertices.push_back(vertex);
            }
            return random.getState();
        }

        /** Builds Three r185's perspective with the DSL backend clip-space conversion. */
        glm::mat4 makeRawshaderProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 50.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10.0;
            constexpr double Pi = 3.14159265358979323846;
            const double top = NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                (double(width) / double(height)) * projectionHeight;
            const double left = -0.5 * projectionWidth;
            const double projectionDepth = FarDistance - NearDistance;

            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(-2.0 * NearDistance / projectionHeight);
            projection[2u][0u] = static_cast<float>(
                -(2.0 * left + projectionWidth) / projectionWidth);
            projection[2u][2u] = static_cast<float>(
                -FarDistance / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -FarDistance * NearDistance / projectionDepth);
            return projection;
        }

        /** Builds the target frame's camera view and positive-Y object rotation. */
        glm::mat4 makeRawshaderModelView(float rotationY)
        {
            const double sineY = std::sin(double(rotationY));
            const double cosineY = std::cos(double(rotationY));
            glm::mat4 model(1.0f);
            model[0u][0u] = static_cast<float>(cosineY);
            model[0u][2u] = static_cast<float>(-sineY);
            model[2u][0u] = static_cast<float>(sineY);
            model[2u][2u] = static_cast<float>(cosineY);

            glm::mat4 view(1.0f);
            view[3u][2u] = -2.0f;
            return view * model;
        }
    } // namespace

    void WebglBuffergeometryRawshaderRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateRawshaderScenario(options);
        device = inDevice;
        finalRandomState = buildRawshaderVertices(vertices);
        const double timeSeconds = double(options.targetFrame) / 60.0;
        rotationY = static_cast<float>(timeSeconds * 0.5);
        shaderTime = static_cast<float>(timeSeconds * 5.0);
        modelViewProjection =
            makeRawshaderProjection(options.width, options.height) *
            makeRawshaderModelView(rotationY);
    }

    void WebglBuffergeometryRawshaderRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglBuffergeometryRawshaderRuntimeAdapter::afterFrame(
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

        const uint64_t byteCount = computeRawshaderRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_buffergeometry_rawshader capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_buffergeometry_rawshader could not access the graphics queue.");
        }
        graphicsQueue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();

        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglBuffergeometryRawshaderRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglBuffergeometryRawshaderRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareRawshaderOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_rawshader RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_buffergeometry_rawshader RGBA capture.");
        }
    }

    void WebglBuffergeometryRawshaderRuntimeAdapter::writeCaptureMetadata(
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
        prepareRawshaderOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_rawshader metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_rawshader\",\n"
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

    void WebglBuffergeometryRawshaderRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareRawshaderOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_rawshader snapshot output path.");
        }
        output << std::fixed << std::setprecision(8);
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_rawshader\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"explicitVertexCount\": 600,\n"
               << "  \"vertexStrideBytes\": 16,\n"
               << "  \"positionStorageBytes\": 7200,\n"
               << "  \"normalizedColorStorageBytes\": 2400,\n"
               << "  \"standaloneGeometryBufferCount\": 1,\n"
               << "  \"seed\": " << RawshaderRandomSeed << ",\n"
               << "  \"upstreamModuleRandomDrawCount\": "
               << UpstreamModuleRandomDrawCount << ",\n"
               << "  \"upstreamObjectUuidRandomDrawCount\": "
               << UpstreamObjectUuidRandomDrawCount << ",\n"
               << "  \"upstreamPreVertexRandomDrawCount\": "
               << UpstreamPreVertexRandomDrawCount << ",\n"
               << "  \"finalRandomState\": " << finalRandomState << ",\n"
               << "  \"timeSeconds\": " << double(frameIndex) / 60.0 << ",\n"
               << "  \"shaderTime\": " << shaderTime << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"fixedStepSeconds\": " << (1.0 / 60.0) << ",\n"
               << "  \"cameraFovDegrees\": 50,\n"
               << "  \"cameraNear\": 1,\n"
               << "  \"cameraFar\": 10,\n"
               << "  \"cameraPositionZ\": 2,\n"
               << "  \"backgroundRgbHex\": \"101010\",\n"
               << "  \"doubleSided\": true,\n"
               << "  \"transparent\": true,\n"
               << "  \"normalBlending\": true,\n"
               << "  \"depthTest\": true,\n"
               << "  \"depthWrite\": true,\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main-private-shader\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"sceneRoots\": [\n"
               << "    {\"id\":\"scene\",\"renderSetCount\":0,\"renderSetId\":null,"
                  "\"renderSetType\":null,\"renderableObjectCount\":1,\"entityCount\":0,"
                  "\"entities\":[],\"drawCommandCount\":1,\"directDrawFallback\":false,"
                  "\"scenePasses\":[{\"name\":\"main-private-shader\","
                  "\"renderClass\":\"WebglBuffergeometryRawshaderMainPass\","
                  "\"renderSetId\":null,\"renderSetBindingCount\":0,"
                  "\"drawMode\":\"explicit-nonindexed\",\"invocationCount\":1,"
                  "\"drawCommandCount\":1,\"usesStandaloneGeometry\":true,"
                  "\"usesExplicitDrawCount\":true}]}\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
