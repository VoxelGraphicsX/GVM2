#include "WebglBuffergeometryRuntimeAdapter.hpp"

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
        constexpr uint32_t BuffergeometryRandomSeed = 0x18500015u;
        constexpr uint32_t BuffergeometryTriangleCount = 160000u;
        constexpr uint32_t BuffergeometryVertexCount =
            BuffergeometryTriangleCount * 3u;
        constexpr uint32_t BuffergeometryModuleRandomDrawCount = 76u;
        constexpr uint32_t BuffergeometryCameraRandomDrawCount = 4u;
        constexpr uint32_t BuffergeometrySceneRandomDrawCount = 4u;
        constexpr uint32_t BuffergeometryAmbientLightRandomDrawCount = 4u;
        constexpr uint32_t BuffergeometryDirectionalLightRandomDrawCount = 12u;
        constexpr uint32_t BuffergeometryGeometryObjectRandomDrawCount = 4u;
        constexpr uint32_t BuffergeometryObjectRandomDrawCount =
            BuffergeometryCameraRandomDrawCount +
            BuffergeometrySceneRandomDrawCount +
            BuffergeometryAmbientLightRandomDrawCount +
            BuffergeometryDirectionalLightRandomDrawCount * 2u +
            BuffergeometryGeometryObjectRandomDrawCount;
        constexpr uint32_t BuffergeometryPreGeometryRandomDrawCount =
            BuffergeometryModuleRandomDrawCount +
            BuffergeometryObjectRandomDrawCount;
        constexpr uint32_t BuffergeometryPerTriangleRandomDrawCount = 13u;
        constexpr uint32_t BuffergeometryRandomDrawCount =
            BuffergeometryTriangleCount *
            BuffergeometryPerTriangleRandomDrawCount;
        constexpr uint32_t BuffergeometryMaterialRandomDrawCount = 4u;
        constexpr uint32_t BuffergeometryMeshRandomDrawCount = 4u;
        constexpr uint32_t BuffergeometryRendererRandomDrawCount = 36u;
        constexpr uint32_t BuffergeometryFirstRenderRandomDrawCount = 8u;
        constexpr uint32_t BuffergeometryPostGeometryRandomDrawCount =
            BuffergeometryMaterialRandomDrawCount +
            BuffergeometryMeshRandomDrawCount +
            BuffergeometryRendererRandomDrawCount +
            BuffergeometryFirstRenderRandomDrawCount;
        constexpr uint32_t BuffergeometryTotalReferenceRandomDrawCount =
            BuffergeometryPreGeometryRandomDrawCount +
            BuffergeometryRandomDrawCount +
            BuffergeometryPostGeometryRandomDrawCount;
        constexpr uint32_t BuffergeometryExpectedGeometryRandomState =
            2315725594u;
        constexpr uint32_t BuffergeometryExpectedReferenceRandomState =
            4038982052u;
        constexpr uint32_t BuffergeometryAfterModuleRandomState = 1057064717u;
        constexpr uint32_t BuffergeometryAfterCameraRandomState = 2007558102u;
        constexpr uint32_t BuffergeometryAfterSceneRandomState = 2221413969u;
        constexpr uint32_t BuffergeometryAfterAmbientLightRandomState =
            3793295688u;
        constexpr uint32_t BuffergeometryAfterDirectionalLight1RandomState =
            1669311323u;
        constexpr uint32_t BuffergeometryAfterDirectionalLight2RandomState =
            1990145794u;
        constexpr uint32_t BuffergeometryBeforeGeometryRandomState =
            3108691359u;
        constexpr uint32_t BuffergeometryAfterMaterialRandomState = 2913495208u;
        constexpr uint32_t BuffergeometryAfterMeshRandomState = 3864669586u;
        constexpr uint32_t BuffergeometryAfterRendererRandomState = 1193638393u;
        constexpr double ReferenceEpochMilliseconds = 1700000000000.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebglBuffergeometryVertex) == 40u);
        static_assert(offsetof(WebglBuffergeometryVertex, position) == 0u);
        static_assert(offsetof(WebglBuffergeometryVertex, normal) == 12u);
        static_assert(offsetof(WebglBuffergeometryVertex, color) == 24u);
        static_assert(BuffergeometryRandomDrawCount == 2080000u);
        static_assert(BuffergeometryObjectRandomDrawCount == 40u);
        static_assert(BuffergeometryPostGeometryRandomDrawCount == 52u);
        static_assert(BuffergeometryTotalReferenceRandomDrawCount == 2080168u);

        /** Creates parent directories for one explicitly requested output artifact. */
        void prepareBuffergeometryOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeBuffergeometryRgbaByteCount(
            uint32_t width,
            uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_buffergeometry RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns one upper-24-bit xorshift value as the reference JavaScript unit double. */
        double nextBuffergeometryRandomUnit(
            ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Reproduces the reference bootstrap's repeated fixed-step clock. */
        double makeBuffergeometryVirtualTimeMilliseconds(uint32_t frameIndex)
        {
            double result = 0.0;
            for (uint32_t index = 0u; index < frameIndex; ++index)
            {
                result += FrameStepMilliseconds;
            }
            return result;
        }

        /** Appends one authored vertex after JavaScript Array-to-Float32 conversion. */
        void appendBuffergeometryVertex(
            eastl::vector<WebglBuffergeometryVertex> &vertices,
            double x,
            double y,
            double z,
            double normalX,
            double normalY,
            double normalZ,
            double red,
            double green,
            double blue,
            double alpha)
        {
            WebglBuffergeometryVertex vertex;
            vertex.position = float3(
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(z));
            vertex.normal = float3(
                static_cast<float>(normalX),
                static_cast<float>(normalY),
                static_cast<float>(normalZ));
            vertex.color = float4(
                static_cast<float>(red),
                static_cast<float>(green),
                static_cast<float>(blue),
                static_cast<float>(alpha));
            vertices.push_back(vertex);
        }

        /** Builds all 160,000 r185 triangles while preserving JavaScript numeric order. */
        uint32_t buildBuffergeometry(
            eastl::vector<WebglBuffergeometryVertex> &vertices)
        {
            ThreeCompat::DeterministicRandom random(BuffergeometryRandomSeed);
            for (uint32_t drawIndex = 0u;
                 drawIndex < BuffergeometryPreGeometryRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            vertices.clear();
            vertices.reserve(BuffergeometryVertexCount);

            for (uint32_t triangle = 0u;
                 triangle < BuffergeometryTriangleCount;
                 ++triangle)
            {
                const double x =
                    nextBuffergeometryRandomUnit(random) * 800.0 - 400.0;
                const double y =
                    nextBuffergeometryRandomUnit(random) * 800.0 - 400.0;
                const double z =
                    nextBuffergeometryRandomUnit(random) * 800.0 - 400.0;

                const double ax =
                    x + nextBuffergeometryRandomUnit(random) * 12.0 - 6.0;
                const double ay =
                    y + nextBuffergeometryRandomUnit(random) * 12.0 - 6.0;
                const double az =
                    z + nextBuffergeometryRandomUnit(random) * 12.0 - 6.0;
                const double bx =
                    x + nextBuffergeometryRandomUnit(random) * 12.0 - 6.0;
                const double by =
                    y + nextBuffergeometryRandomUnit(random) * 12.0 - 6.0;
                const double bz =
                    z + nextBuffergeometryRandomUnit(random) * 12.0 - 6.0;
                const double cx =
                    x + nextBuffergeometryRandomUnit(random) * 12.0 - 6.0;
                const double cy =
                    y + nextBuffergeometryRandomUnit(random) * 12.0 - 6.0;
                const double cz =
                    z + nextBuffergeometryRandomUnit(random) * 12.0 - 6.0;

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
                const double normalizedX = normalX * inverseNormalLength;
                const double normalizedY = normalY * inverseNormalLength;
                const double normalizedZ = normalZ * inverseNormalLength;

                const double red = x / 800.0 + 0.5;
                const double green = y / 800.0 + 0.5;
                const double blue = z / 800.0 + 0.5;
                const double alpha = nextBuffergeometryRandomUnit(random);

                appendBuffergeometryVertex(
                    vertices,
                    ax,
                    ay,
                    az,
                    normalizedX,
                    normalizedY,
                    normalizedZ,
                    red,
                    green,
                    blue,
                    alpha);
                appendBuffergeometryVertex(
                    vertices,
                    bx,
                    by,
                    bz,
                    normalizedX,
                    normalizedY,
                    normalizedZ,
                    red,
                    green,
                    blue,
                    alpha);
                appendBuffergeometryVertex(
                    vertices,
                    cx,
                    cy,
                    cz,
                    normalizedX,
                    normalizedY,
                    normalizedZ,
                    red,
                    green,
                    blue,
                    alpha);
            }

            const uint32_t geometryRandomState = random.getState();
            for (uint32_t drawIndex = 0u;
                 drawIndex < BuffergeometryPostGeometryRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }
            if (vertices.size() != BuffergeometryVertexCount ||
                geometryRandomState !=
                    BuffergeometryExpectedGeometryRandomState ||
                random.getState() !=
                    BuffergeometryExpectedReferenceRandomState)
            {
                throw std::runtime_error(
                    "webgl_buffergeometry deterministic geometry diverged from r185.");
            }
            return geometryRandomState;
        }

        /** Builds Three's exact OpenGL perspective matrix before DSL backend conversion. */
        glm::mat4 makeBuffergeometryProjection(uint32_t width, uint32_t height)
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
        glm::mat4 makeBuffergeometryModelView(
            double rotationX,
            double rotationY)
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

    void WebglBuffergeometryRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_buffergeometry")
        {
            throw std::invalid_argument(
                "Buffergeometry adapter requires case-id webgl_buffergeometry.");
        }
        const bool initial =
            options.scenarioId == "initial-seeded" &&
            options.targetFrame == 0u;
        const bool fixedRotation =
            options.scenarioId == "fixed-rotation" &&
            options.targetFrame == 120u;
        if (!initial && !fixedRotation)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry requires initial-seeded/frame 0 or fixed-rotation/frame 120.");
        }
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry requires the locked 800x500 extent.");
        }
        if (options.randomSeed != BuffergeometryRandomSeed)
        {
            throw std::invalid_argument(
                "webgl_buffergeometry requires random seed 0x18500015.");
        }
        if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgl_buffergeometry scenarios must not consume an input replay.");
        }
        if (options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "webgl_buffergeometry requires explicit --asset-root.");
        }

        device = inDevice;
        geometryFinalRandomState = buildBuffergeometry(vertices);
        virtualTimeMilliseconds =
            makeBuffergeometryVirtualTimeMilliseconds(options.targetFrame);
        timeSeconds =
            (ReferenceEpochMilliseconds + virtualTimeMilliseconds) * 0.001;
        rotationX = timeSeconds * 0.25;
        rotationY = timeSeconds * 0.5;
        modelView = makeBuffergeometryModelView(rotationX, rotationY);
        projection = makeBuffergeometryProjection(options.width, options.height);
    }

    void WebglBuffergeometryRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglBuffergeometryRuntimeAdapter::afterFrame(
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
            computeBuffergeometryRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_buffergeometry capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_buffergeometry could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglBuffergeometryRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglBuffergeometryRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareBuffergeometryOutputPath(outputPath);
        std::ofstream output(
            outputPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_buffergeometry RGBA capture.");
        }
    }

    void WebglBuffergeometryRuntimeAdapter::writeCaptureMetadata(
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
        prepareBuffergeometryOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry metadata path.");
        }
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry\",\n"
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
               << "  \"inputReplay\": null\n"
               << "}\n";
    }

    void WebglBuffergeometryRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareBuffergeometryOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry snapshot path.");
        }
        output.precision(17);
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str()
               << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 2,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"screenPasses\": [],\n"
               << "  \"drawCommandCount\": 2,\n"
               << "  \"physicalCoverageDrawCount\": 2,\n"
               << "  \"scenePassSequence\": ["
                  "{\"sceneRoot\":\"scene\",\"scenePass\":"
                  "\"main-transparent-phong-back-side\",\"entityOrdinal\":0},"
                  "{\"sceneRoot\":\"scene\",\"scenePass\":"
                  "\"main-transparent-phong-front-side\",\"entityOrdinal\":0}],\n"
               << "  \"sceneRoots\": [{\n"
               << "    \"id\": \"scene\",\n"
               << "    \"renderSetCount\": 0,\n"
               << "    \"renderSetId\": null,\n"
               << "    \"renderSetType\": null,\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"entityCount\": 0,\n"
               << "    \"entities\": [],\n"
               << "    \"drawCommandCount\": 2,\n"
               << "    \"directDrawFallback\": false,\n"
               << "    \"scenePasses\": ["
                  "{\"name\":\"main-transparent-phong-back-side\","
                  "\"renderClass\":\"WebglBuffergeometryBackSidePass\","
                  "\"renderSetId\":null,\"renderSetBindingCount\":0,"
                  "\"drawMode\":\"explicit-non-indexed-triangle-list\","
                  "\"invocationCount\":1,\"drawCommandCount\":1,"
                  "\"usesStandaloneGeometry\":true,"
                  "\"usesExplicitDrawCount\":true},"
                  "{\"name\":\"main-transparent-phong-front-side\","
                  "\"renderClass\":\"WebglBuffergeometryFrontSidePass\","
                  "\"renderSetId\":null,\"renderSetBindingCount\":0,"
                  "\"drawMode\":\"explicit-non-indexed-triangle-list\","
                  "\"invocationCount\":1,\"drawCommandCount\":1,"
                  "\"usesStandaloneGeometry\":true,"
                  "\"usesExplicitDrawCount\":true}]\n"
               << "  }],\n"
               << "  \"sourceTriangleCount\": "
               << BuffergeometryTriangleCount << ",\n"
               << "  \"sourceVertexCount\": " << vertices.size() << ",\n"
               << "  \"vertexStrideBytes\": 40,\n"
               << "  \"sourcePositionFormat\": \"float32x3\",\n"
               << "  \"sourceNormalFormat\": \"float32x3\",\n"
               << "  \"sourceColorFormat\": \"float32x4\",\n"
               << "  \"doubleSideStrategy\": "
                  "\"transparent-back-side-then-front-side\",\n"
               << "  \"blendColor\": \"src-alpha-one-minus-src-alpha\",\n"
               << "  \"blendAlpha\": \"one-one-minus-src-alpha\",\n"
               << "  \"depthWriteEnabled\": true,\n"
               << "  \"depthCompare\": \"less-equal\",\n"
               << "  \"preGeometryRandomDrawCount\": "
               << BuffergeometryPreGeometryRandomDrawCount << ",\n"
               << "  \"geometryRandomDrawCount\": "
               << BuffergeometryRandomDrawCount << ",\n"
               << "  \"postGeometryRandomDrawCount\": "
               << BuffergeometryPostGeometryRandomDrawCount << ",\n"
               << "  \"totalReferenceRandomDrawCount\": "
               << BuffergeometryTotalReferenceRandomDrawCount << ",\n"
               << "  \"geometryFinalRandomState\": "
               << geometryFinalRandomState << ",\n"
               << "  \"referenceFinalRandomState\": "
               << BuffergeometryExpectedReferenceRandomState << ",\n"
               << "  \"referenceRandomAudit\": {\n"
               << "    \"moduleImport\": {\"drawCount\":76,\"state\":"
               << BuffergeometryAfterModuleRandomState << "},\n"
               << "    \"camera\": {\"drawCount\":4,\"state\":"
               << BuffergeometryAfterCameraRandomState << "},\n"
               << "    \"scene\": {\"drawCount\":4,\"state\":"
               << BuffergeometryAfterSceneRandomState << "},\n"
               << "    \"ambientLight\": {\"drawCount\":4,\"state\":"
               << BuffergeometryAfterAmbientLightRandomState << "},\n"
               << "    \"directionalLight1\": {\"drawCount\":12,\"state\":"
               << BuffergeometryAfterDirectionalLight1RandomState << "},\n"
               << "    \"directionalLight2\": {\"drawCount\":12,\"state\":"
               << BuffergeometryAfterDirectionalLight2RandomState << "},\n"
               << "    \"geometryObject\": {\"drawCount\":4,\"state\":"
               << BuffergeometryBeforeGeometryRandomState << "},\n"
               << "    \"geometryValues\": {\"drawCount\":2080000,\"state\":"
               << BuffergeometryExpectedGeometryRandomState << "},\n"
               << "    \"material\": {\"drawCount\":4,\"state\":"
               << BuffergeometryAfterMaterialRandomState << "},\n"
               << "    \"mesh\": {\"drawCount\":4,\"state\":"
               << BuffergeometryAfterMeshRandomState << "},\n"
               << "    \"rendererConstructor\": {\"drawCount\":36,\"state\":"
               << BuffergeometryAfterRendererRandomState << "},\n"
               << "    \"firstRender\": {\"drawCount\":8,\"state\":"
               << BuffergeometryExpectedReferenceRandomState << "},\n"
               << "    \"subsequentFrames\": {\"drawCount\":0,\"state\":"
               << BuffergeometryExpectedReferenceRandomState << "}\n"
               << "  },\n"
               << "  \"virtualTimeMilliseconds\": "
               << virtualTimeMilliseconds << ",\n"
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
               << "  \"transparent\": true,\n"
               << "  \"antialiasResolve\": "
                  "\"disabled-single-sample\"\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
