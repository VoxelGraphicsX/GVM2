#include "WebglBuffergeometryUintRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t UintRandomSeed = 0x1850000fu;
        constexpr uint32_t UintSourceTriangleCount = 500000u;
        constexpr uint32_t UintSourceVertexCount = UintSourceTriangleCount * 3u;
        constexpr uint32_t UintExpandedVertexCount = UintSourceTriangleCount * 6u;
        constexpr uint32_t UintModuleRandomDrawCount = 76u;
        constexpr uint32_t UintObjectUuidRandomDrawCount = 40u;
        constexpr uint32_t UintPreGeometryRandomDrawCount =
            UintModuleRandomDrawCount + UintObjectUuidRandomDrawCount;
        constexpr uint32_t UintRandomDrawCount = UintSourceTriangleCount * 12u;
        constexpr uint32_t UintExpectedFinalRandomState = 3215844914u;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebglBuffergeometryUintVertex) == 24u);
        static_assert(offsetof(WebglBuffergeometryUintVertex, position) == 0u);
        static_assert(
            offsetof(WebglBuffergeometryUintVertex, packedNormalSnorm16x3) ==
            12u);
        static_assert(
            offsetof(WebglBuffergeometryUintVertex, packedColorUnorm8x3) ==
            20u);
        static_assert(UintRandomDrawCount == 6000000u);

        /** Creates parent directories for one explicitly requested output artifact. */
        void prepareUintOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeUintRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_buffergeometry_uint RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns one upper-24-bit xorshift value as the reference JavaScript unit double. */
        double nextUintRandomUnit(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Reproduces the reference bootstrap's repeated fixed-step clock. */
        double makeUintVirtualTimeMilliseconds(uint32_t frameIndex)
        {
            double virtualTimeMilliseconds = 0.0;
            for (uint32_t index = 0u; index < frameIndex; ++index)
            {
                virtualTimeMilliseconds += FrameStepMilliseconds;
            }
            return virtualTimeMilliseconds;
        }

        /** Packs three authored Int16 values into existing uint2 vertex attributes. */
        uint2 packUintNormal(int16_t x, int16_t y, int16_t z)
        {
            const uint32_t xBits = static_cast<uint16_t>(x);
            const uint32_t yBits = static_cast<uint16_t>(y);
            const uint32_t zBits = static_cast<uint16_t>(z);
            return uint2(xBits | (yBits << 16u), zBits);
        }

        /** Packs three authored Uint8 values into one existing uint vertex attribute. */
        uint32_t packUintColor(uint8_t red, uint8_t green, uint8_t blue)
        {
            return uint32_t(red) |
                   (uint32_t(green) << 8u) |
                   (uint32_t(blue) << 16u);
        }

        /** Appends one Float32 position with untouched packed normal and color bits. */
        void appendUintVertex(
            eastl::vector<WebglBuffergeometryUintVertex> &vertices,
            double x,
            double y,
            double z,
            uint2 packedNormal,
            uint32_t packedColor)
        {
            WebglBuffergeometryUintVertex vertex;
            vertex.position = float3(
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(z));
            vertex.packedNormalSnorm16x3 = packedNormal;
            vertex.packedColorUnorm8x3 = packedColor;
            vertices.push_back(vertex);
        }

        /** Maps Three DoubleSide facing onto two back-culled opposite windings. */
        void appendUintDualWindingTriangle(
            eastl::vector<WebglBuffergeometryUintVertex> &vertices,
            double ax,
            double ay,
            double az,
            double bx,
            double by,
            double bz,
            double cx,
            double cy,
            double cz,
            int16_t normalX,
            int16_t normalY,
            int16_t normalZ,
            uint32_t packedColor)
        {
            const uint2 sourceWindingNormal = packUintNormal(
                static_cast<int16_t>(-normalX),
                static_cast<int16_t>(-normalY),
                static_cast<int16_t>(-normalZ));
            const uint2 reversedWindingNormal = packUintNormal(
                normalX,
                normalY,
                normalZ);
            appendUintVertex(
                vertices, ax, ay, az, sourceWindingNormal, packedColor);
            appendUintVertex(
                vertices, bx, by, bz, sourceWindingNormal, packedColor);
            appendUintVertex(
                vertices, cx, cy, cz, sourceWindingNormal, packedColor);
            appendUintVertex(
                vertices, ax, ay, az, reversedWindingNormal, packedColor);
            appendUintVertex(
                vertices, cx, cy, cz, reversedWindingNormal, packedColor);
            appendUintVertex(
                vertices, bx, by, bz, reversedWindingNormal, packedColor);
        }

        /** Builds all 500,000 r185 triangles while preserving Float32/Int16/Uint8 conversion order. */
        uint32_t buildUintGeometry(
            eastl::vector<WebglBuffergeometryUintVertex> &vertices)
        {
            ThreeCompat::DeterministicRandom random(UintRandomSeed);
            for (uint32_t drawIndex = 0u;
                 drawIndex < UintPreGeometryRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            vertices.clear();
            vertices.reserve(UintExpandedVertexCount);

            for (uint32_t triangle = 0u;
                 triangle < UintSourceTriangleCount;
                 ++triangle)
            {
                const double x = nextUintRandomUnit(random) * 800.0 - 400.0;
                const double y = nextUintRandomUnit(random) * 800.0 - 400.0;
                const double z = nextUintRandomUnit(random) * 800.0 - 400.0;

                const double ax = x + nextUintRandomUnit(random) * 12.0 - 6.0;
                const double ay = y + nextUintRandomUnit(random) * 12.0 - 6.0;
                const double az = z + nextUintRandomUnit(random) * 12.0 - 6.0;
                const double bx = x + nextUintRandomUnit(random) * 12.0 - 6.0;
                const double by = y + nextUintRandomUnit(random) * 12.0 - 6.0;
                const double bz = z + nextUintRandomUnit(random) * 12.0 - 6.0;
                const double cx = x + nextUintRandomUnit(random) * 12.0 - 6.0;
                const double cy = y + nextUintRandomUnit(random) * 12.0 - 6.0;
                const double cz = z + nextUintRandomUnit(random) * 12.0 - 6.0;

                const double cbX = cx - bx;
                const double cbY = cy - by;
                const double cbZ = cz - bz;
                const double abX = ax - bx;
                const double abY = ay - by;
                const double abZ = az - bz;
                const double normalX = cbY * abZ - cbZ * abY;
                const double normalY = cbZ * abX - cbX * abZ;
                const double normalZ = cbX * abY - cbY * abX;
                const double inverseNormalLength = 1.0 / std::sqrt(
                    normalX * normalX +
                    normalY * normalY +
                    normalZ * normalZ);
                const int16_t quantizedNormalX = static_cast<int16_t>(
                    normalX * inverseNormalLength * 32767.0);
                const int16_t quantizedNormalY = static_cast<int16_t>(
                    normalY * inverseNormalLength * 32767.0);
                const int16_t quantizedNormalZ = static_cast<int16_t>(
                    normalZ * inverseNormalLength * 32767.0);

                const uint8_t red = static_cast<uint8_t>(
                    (x / 800.0 + 0.5) * 255.0);
                const uint8_t green = static_cast<uint8_t>(
                    (y / 800.0 + 0.5) * 255.0);
                const uint8_t blue = static_cast<uint8_t>(
                    (z / 800.0 + 0.5) * 255.0);
                appendUintDualWindingTriangle(
                    vertices,
                    ax,
                    ay,
                    az,
                    bx,
                    by,
                    bz,
                    cx,
                    cy,
                    cz,
                    quantizedNormalX,
                    quantizedNormalY,
                    quantizedNormalZ,
                    packUintColor(red, green, blue));
            }

            if (vertices.size() != UintExpandedVertexCount ||
                random.getState() != UintExpectedFinalRandomState)
            {
                throw std::runtime_error(
                    "webgl_buffergeometry_uint deterministic geometry diverged from r185.");
            }
            return random.getState();
        }

        /** Builds Three's exact OpenGL perspective matrix before DSL backend conversion. */
        glm::mat4 makeUintProjection(uint32_t width, uint32_t height)
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
                2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(
                -(FarDistance + NearDistance) / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -2.0 * FarDistance * NearDistance / projectionDepth);
            return projection;
        }

        /** Builds Three's XYZ Euler model matrix followed by camera-z translation. */
        glm::mat4 makeUintModelView(double rotationX, double rotationY)
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
            view[3u][2u] = -2750.0f;
            return view * model;
        }
    } // namespace

    void WebglBuffergeometryUintRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_buffergeometry_uint")
        {
            throw std::invalid_argument(
                "Uint adapter requires case-id webgl_buffergeometry_uint.");
        }
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 60u;
        if (!initial && !animated)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_uint requires initial/frame 0 or animated/frame 60.");
        }
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_uint requires the locked 800x500 extent.");
        }
        if (options.randomSeed != UintRandomSeed)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_uint requires random seed 0x1850000f.");
        }
        if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_uint scenarios must not consume an input replay.");
        }
        if (options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "webgl_buffergeometry_uint requires explicit --asset-root.");
        }

        device = inDevice;
        finalRandomState = buildUintGeometry(vertices);
        const double virtualTimeMilliseconds =
            makeUintVirtualTimeMilliseconds(options.targetFrame);
        timeSeconds =
            (ReferenceEpochMilliseconds + virtualTimeMilliseconds) * 0.001;
        rotationX = timeSeconds * 0.25;
        rotationY = timeSeconds * 0.5;
        modelView = makeUintModelView(rotationX, rotationY);
        projection = makeUintProjection(options.width, options.height);
    }

    void WebglBuffergeometryUintRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglBuffergeometryUintRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computeUintRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_buffergeometry_uint capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_buffergeometry_uint could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglBuffergeometryUintRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglBuffergeometryUintRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareUintOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_uint RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_buffergeometry_uint RGBA capture.");
        }
    }

    void WebglBuffergeometryUintRuntimeAdapter::writeCaptureMetadata(
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
        prepareUintOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_uint metadata path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_uint\",\n"
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
               << "  \"inputReplay\": null\n"
               << "}\n";
    }

    void WebglBuffergeometryUintRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareUintOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_uint snapshot path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_uint\",\n"
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
                  "\"scenePass\":\"main-packed-phong\",\"entityOrdinal\":0}],\n"
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
               << "    \"scenePasses\": [{\"name\":\"main-packed-phong\","
                  "\"renderClass\":\"WebglBuffergeometryUintMainPass\","
                  "\"renderSetId\":null,\"renderSetBindingCount\":0,"
                  "\"drawMode\":\"explicit-dual-winding-triangle-list\","
                  "\"invocationCount\":1,\"drawCommandCount\":1,"
                  "\"usesStandaloneGeometry\":true,"
                  "\"usesExplicitDrawCount\":true}]\n"
               << "  }],\n"
               << "  \"sourceTriangleCount\": " << UintSourceTriangleCount << ",\n"
               << "  \"sourceVertexCount\": " << UintSourceVertexCount << ",\n"
               << "  \"expandedVertexCount\": " << vertices.size() << ",\n"
               << "  \"vertexStrideBytes\": 24,\n"
               << "  \"sourcePositionFormat\": \"float32x3\",\n"
               << "  \"sourceNormalFormat\": \"snorm16x3-packed-as-uint2\",\n"
               << "  \"sourceColorFormat\": \"unorm8x3-packed-as-uint\",\n"
               << "  \"normalizationStage\": \"dsl-vertex\",\n"
               << "  \"doubleSideStrategy\": \"dual-winding-negated-normal-cull-back\",\n"
               << "  \"preGeometryRandomDrawCount\": "
               << UintPreGeometryRandomDrawCount << ",\n"
               << "  \"randomDrawCount\": " << UintRandomDrawCount << ",\n"
               << "  \"finalRandomState\": " << finalRandomState << ",\n"
               << "  \"timeSeconds\": " << timeSeconds << ",\n"
               << "  \"rotationX\": " << rotationX << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"cameraFovDegrees\": 27,\n"
               << "  \"cameraNear\": 1,\n"
               << "  \"cameraFar\": 3500,\n"
               << "  \"cameraPositionZ\": 2750,\n"
               << "  \"fogType\": \"linear\",\n"
               << "  \"fogNear\": 2000,\n"
               << "  \"fogFar\": 3500,\n"
               << "  \"materialColorHex\": \"d5d5d5\",\n"
               << "  \"shininess\": 250,\n"
               << "  \"antialiasResolve\": \"disabled-single-sample\"\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
