#include "WebglPointsWavesRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/vec3.hpp>

#include <cmath>
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
        constexpr uint32_t AmountX = 50u;
        constexpr uint32_t AmountY = 50u;
        constexpr uint32_t LogicalPointCount = AmountX * AmountY;
        constexpr uint32_t ExpandedVertexCount = LogicalPointCount * 3u;
        constexpr uint32_t ExpandedIndexCount = LogicalPointCount * 3u;
        constexpr double Separation = 100.0;
        constexpr double FrameStepMilliseconds = 1000.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t ReferenceRandomState = 2488893119u;
        constexpr char CameraReplaySha256[] =
            "8fe5fa752c1f674042813b6d4e82266a1fd93ec1af207d751b8b4d1f7f1837b4";

        static_assert(sizeof(WebglPointsWavesVertex) == 32u);

        /** Creates parent directories for one explicitly requested artifact path. */
        void preparePointsWavesOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computePointsWavesRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_points_waves RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Reads one bounded replay file into exact bytes. */
        eastl::vector<uint8_t> readPointsWavesFile(
            const std::filesystem::path &inputPath)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open webgl_points_waves input replay: " +
                    inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 ||
                static_cast<uint64_t>(end) >
                    std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(
                    "webgl_points_waves input replay has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete webgl_points_waves input replay.");
            }
            return bytes;
        }

        /** Returns one lowercase SHA-256 digest for a bounded byte range. */
        eastl::string calculatePointsWavesSha256(
            const void *bytes,
            size_t byteCount)
        {
            if (byteCount > std::numeric_limits<CC_LONG>::max())
            {
                throw std::overflow_error(
                    "webgl_points_waves SHA-256 input is too large.");
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

        /** Writes one string as a minimally escaped JSON value. */
        void writePointsWavesJsonString(
            std::ostream &output,
            const eastl::string &value)
        {
            output << '"';
            for (const char character : value)
            {
                if (character == '"' || character == '\\')
                {
                    output << '\\';
                }
                output << character;
            }
            output << '"';
        }

        /** Returns the manifest-locked canonical state for one supported scenario. */
        const char *pointsWavesCanonicalState(const eastl::string &scenarioId)
        {
            if (scenarioId == "initial")
            {
                return "50x50-wave-grid-count-zero-single-sample";
            }
            if (scenarioId == "animated")
            {
                return "50x50-wave-grid-count-six-single-sample";
            }
            return "pointer-x-100-y-zero-camera-eased-62-frames-count-6.1";
        }
    } // namespace

    void WebglPointsWavesRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_points_waves")
        {
            throw std::invalid_argument(
                "WebglPointsWaves host received an unexpected --case-id.");
        }
        if (options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument(
                "webgl_points_waves requires the repository seed 0x12345678.");
        }
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument(
                "webgl_points_waves capture dimensions are fixed at 800x500.");
        }
        const bool initialScenario =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animatedScenario =
            options.scenarioId == "animated" && options.targetFrame == 60u;
        cameraInputScenario =
            options.scenarioId == "camera-input" && options.targetFrame == 61u;
        if (!initialScenario && !animatedScenario && !cameraInputScenario)
        {
            throw std::invalid_argument(
                "webgl_points_waves scenario and frame do not match the manifest.");
        }
        if (cameraInputScenario)
        {
            if (options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "camera-input requires its explicit --input-replay path.");
            }
            const eastl::vector<uint8_t> replayBytes =
                readPointsWavesFile(options.inputReplayPath.c_str());
            inputReplaySha256 = calculatePointsWavesSha256(
                replayBytes.data(),
                replayBytes.size());
            if (inputReplaySha256 != CameraReplaySha256)
            {
                throw std::runtime_error(
                    "webgl_points_waves input replay SHA-256 diverged from the lock.");
            }
            pointerX = 100.0;
            pointerY = 0.0;
        }
        else if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Only camera-input accepts --input-replay.");
        }

        device = inDevice;
        projectionMatrix = makeProjectionMatrix(options.width, options.height);
        vertices.reserve(ExpandedVertexCount);
        indices.reserve(ExpandedIndexCount);
        const eastl::array<float2, 3u> corners = {
            float2(-1.0f, -1.0f),
            float2(3.0f, -1.0f),
            float2(-1.0f, 3.0f)};
        constexpr eastl::array<uint32_t, 3u> LocalIndices = {0u, 1u, 2u};
        for (uint32_t gridX = 0u; gridX < AmountX; ++gridX)
        {
            for (uint32_t gridY = 0u; gridY < AmountY; ++gridY)
            {
                const float x = static_cast<float>(
                    double(gridX) * Separation -
                    double(AmountX) * Separation * 0.5);
                const float z = static_cast<float>(
                    double(gridY) * Separation -
                    double(AmountY) * Separation * 0.5);
                const uint32_t baseVertex =
                    static_cast<uint32_t>(vertices.size());
                for (const float2 corner : corners)
                {
                    WebglPointsWavesVertex vertex;
                    vertex.positionAndGridX =
                        float4(x, 0.0f, z, float(gridX));
                    vertex.cornerAndGridY =
                        float4(corner.x, corner.y, float(gridY), 0.0f);
                    vertices.push_back(vertex);
                }
                for (const uint32_t localIndex : LocalIndices)
                {
                    indices.push_back(baseVertex + localIndex);
                }
            }
        }
        if (vertices.size() != ExpandedVertexCount ||
            indices.size() != ExpandedIndexCount)
        {
            throw std::runtime_error(
                "webgl_points_waves expanded grid counts are inconsistent.");
        }
    }

    glm::mat4 WebglPointsWavesRuntimeAdapter::makeProjectionMatrix(
        uint32_t width,
        uint32_t height) const
    {
        constexpr double NearDistance = 1.0;
        constexpr double FarDistance = 10000.0;
        const double top =
            NearDistance * std::tan(75.0 * Pi / 360.0);
        const double projectionHeight = 2.0 * top;
        const double projectionWidth =
            double(width) / double(height) * projectionHeight;
        const double depth = FarDistance - NearDistance;
        glm::mat4 projection(0.0f);
        projection[0u][0u] =
            static_cast<float>(2.0 * NearDistance / projectionWidth);
        projection[1u][1u] =
            static_cast<float>(2.0 * NearDistance / projectionHeight);
        projection[2u][2u] =
            static_cast<float>(-(FarDistance + NearDistance) / depth);
        projection[2u][3u] = -1.0f;
        projection[3u][2u] =
            static_cast<float>(-2.0 * FarDistance * NearDistance / depth);
        return projection;
    }

    glm::mat4 WebglPointsWavesRuntimeAdapter::advanceCameraAndMakeView()
    {
        cameraX += (pointerX - cameraX) * 0.05;
        cameraY += (-pointerY - cameraY) * 0.05;
        cameraUpdateCount += 1u;
        return glm::mat4(glm::lookAtRH(
            glm::dvec3(cameraX, -cameraY, 1000.0),
            glm::dvec3(0.0, 0.0, 0.0),
            glm::dvec3(0.0, 1.0, 0.0)));
    }

    void WebglPointsWavesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        viewMatrix = advanceCameraAndMakeView();
        phase = double(frameIndex) * 0.1;
        WebglPointsWavesUniforms uniforms;
        uniforms.projectionMatrix = projectionMatrix;
        uniforms.viewMatrix = viewMatrix;
        uniforms.phaseAndViewport = float4(
            static_cast<float>(phase),
            800.0f,
            500.0f,
            0.0f);
        frameUploader(uniforms);
    }

    void WebglPointsWavesRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (frameIndex != options.targetFrame || captureWritten)
        {
            return;
        }
        const uint64_t byteCount =
            computePointsWavesRgbaByteCount(width, height);
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
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

    void WebglPointsWavesRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        const std::filesystem::path outputPath(
            options.captureRgbaPath.c_str());
        preparePointsWavesOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write webgl_points_waves RGBA capture.");
        }
    }

    void WebglPointsWavesRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        const std::filesystem::path outputPath(
            options.captureMetadataPath.c_str());
        preparePointsWavesOutputPath(outputPath);
        std::ofstream output(outputPath);
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_points_waves\",\n"
               << "  \"scenarioId\": ";
        writePointsWavesJsonString(output, options.scenarioId);
        output << ",\n  \"pipeline\": ";
        writePointsWavesJsonString(output, options.pipeline);
        output << ",\n  \"backend\": \""
               << threeSampleBackendName(options.backend)
               << "\",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"randomState\": " << ReferenceRandomState << ",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"virtualTimeMs\": "
               << double(frameIndex) * FrameStepMilliseconds << ",\n"
               << "  \"nextFrameTimeMs\": "
               << double(frameIndex + 1u) * FrameStepMilliseconds << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << width * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n"
               << "  \"inputReplay\": ";
        if (inputReplaySha256.empty())
        {
            output << "null";
        }
        else
        {
            output << "{\n"
                   << "    \"schemaVersion\": 1,\n"
                   << "    \"caseId\": \"webgl_points_waves\",\n"
                   << "    \"scenarioId\": \"camera-input\",\n"
                   << "    \"captureFrame\": 61,\n"
                   << "    \"sha256\": ";
            writePointsWavesJsonString(output, inputReplaySha256);
            output << ",\n"
                   << "    \"target\": \"body > div:nth-of-type(2) > canvas\",\n"
                   << "    \"eventCount\": 1\n"
                   << "  }";
        }
        output << "\n}\n";
        if (!output)
        {
            throw std::runtime_error(
                "Could not write webgl_points_waves capture metadata.");
        }
    }

    void WebglPointsWavesRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        const std::filesystem::path outputPath(
            options.sceneSnapshotPath.c_str());
        preparePointsWavesOutputPath(outputPath);
        std::ofstream output(outputPath);
        output << std::setprecision(17)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_points_waves\",\n"
               << "  \"scenarioId\": ";
        writePointsWavesJsonString(output, options.scenarioId);
        output << ",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"canonicalState\": \""
               << pointsWavesCanonicalState(options.scenarioId)
               << "\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"logicalPointCount\": " << LogicalPointCount << ",\n"
               << "  \"expandedVertexCount\": " << ExpandedVertexCount << ",\n"
               << "  \"explicitIndexCount\": " << ExpandedIndexCount << ",\n"
               << "  \"expandedTriangleCount\": "
               << ExpandedIndexCount / 3u << ",\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"logicalScenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"logicalDrawCommandCount\": 1,\n"
               << "  \"physicalCoverageDrawCount\": 1,\n"
               << "  \"computePassCount\": 0,\n"
               << "  \"standaloneGeometryBufferCount\": 2,\n"
               << "  \"primitiveTopology\": \"triangle-list\",\n"
               << "  \"pointExpansion\": \"oversized-triangle-three-indices-noninstanced\",\n"
               << "  \"pointRadius\": 0.475,\n"
               << "  \"wavePhase\": " << phase << ",\n"
               << "  \"cameraUpdateCount\": " << cameraUpdateCount << ",\n"
               << "  \"cameraPosition\": ["
               << cameraX << ", " << cameraY << ", 1000],\n"
               << "  \"pointer\": ["
               << pointerX << ", " << pointerY << "],\n"
               << "  \"antialias\": \"disabled-single-sample\",\n"
               << "  \"samplePattern\": [[0, 0]],\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\": \"scene\", \"scenePass\": \"main-sample\", \"entityOrdinal\": 0, \"sampleOrdinal\": 0}\n"
               << "  ],\n"
               << "  \"screenPassSequence\": [],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
        if (!output)
        {
            throw std::runtime_error(
                "Could not write webgl_points_waves structural snapshot.");
        }
    }

    void WebglPointsWavesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        frameUploader = {};
    }
} // namespace GVM::ThreeSamples
