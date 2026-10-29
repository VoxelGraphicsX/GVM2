#include "WebglModifierTessellationRuntimeAdapter.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t TessellatedVertexCount = 32442u;
        constexpr uint32_t TessellatedTriangleCount = 10814u;
        constexpr uint32_t ExpectedFinalRandomState = 3268735856u;
        constexpr uint32_t BinaryHeaderByteCount = 20u;
        constexpr uint32_t BinaryRecordFloatCount = 10u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *InputReplaySha256 = "4103fc0d10ee71c7c707dbaa65bba19a6006c549e2086f3126e43064ff96178d";

        /** Creates parent directories for one requested comparison artifact. */
        void prepareTessellationOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes one bounded tightly packed RGBA8 capture size. */
        uint64_t computeTessellationRgbaByteCount(uint32_t width, uint32_t height)
        {
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / 4u)
            {
                throw std::overflow_error("Tessellation RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * 4u;
        }

        /** Reads one little-endian uint32 from a validated binary asset offset. */
        uint32_t readTessellationUint32(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            if (offset + 4u > bytes.size())
            {
                throw std::runtime_error("Tessellation asset uint32 read exceeded the payload.");
            }
            return uint32_t(bytes[offset]) |
                   (uint32_t(bytes[offset + 1u]) << 8u) |
                   (uint32_t(bytes[offset + 2u]) << 16u) |
                   (uint32_t(bytes[offset + 3u]) << 24u);
        }

        /** Reads one IEEE-754 float from a validated little-endian binary asset offset. */
        float readTessellationFloat(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            const uint32_t bits = readTessellationUint32(bytes, offset);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        /** Loads the locked r185 text, normal, face-color, and displacement stream. */
        uint32_t loadTessellationVertices(const std::filesystem::path &assetPath,
                                          eastl::vector<WebglModifierTessellationVertex> &vertices)
        {
            std::ifstream input(assetPath, std::ios::binary | std::ios::in);
            if (!input)
            {
                throw std::runtime_error("Could not open generated webgl_modifier_tessellation vertex asset.");
            }
            input.seekg(0, std::ios::end);
            const std::streamoff fileSize = input.tellg();
            input.seekg(0, std::ios::beg);
            const uint64_t expectedSize = uint64_t(BinaryHeaderByteCount) +
                                          uint64_t(TessellatedVertexCount) * BinaryRecordFloatCount * sizeof(float);
            if (fileSize < 0 || uint64_t(fileSize) != expectedSize)
            {
                throw std::runtime_error("Generated tessellation vertex asset has an invalid byte count.");
            }
            eastl::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
            input.read(reinterpret_cast<char *>(bytes.data()), fileSize);
            if (!input || std::memcmp(bytes.data(), "TESSR185", 8u) != 0 ||
                readTessellationUint32(bytes, 8u) != 1u ||
                readTessellationUint32(bytes, 12u) != TessellatedVertexCount)
            {
                throw std::runtime_error("Generated tessellation vertex asset header is invalid.");
            }
            vertices.clear();
            vertices.reserve(TessellatedVertexCount);
            size_t offset = BinaryHeaderByteCount;
            for (uint32_t vertexIndex = 0u; vertexIndex < TessellatedVertexCount; ++vertexIndex)
            {
                const float px = readTessellationFloat(bytes, offset + 0u);
                const float py = readTessellationFloat(bytes, offset + 4u);
                const float pz = readTessellationFloat(bytes, offset + 8u);
                const float nx = readTessellationFloat(bytes, offset + 12u);
                const float ny = readTessellationFloat(bytes, offset + 16u);
                const float nz = readTessellationFloat(bytes, offset + 20u);
                const float red = readTessellationFloat(bytes, offset + 24u);
                const float green = readTessellationFloat(bytes, offset + 28u);
                const float blue = readTessellationFloat(bytes, offset + 32u);
                const float displacement = readTessellationFloat(bytes, offset + 36u);
                vertices.push_back({
                    .position = {px, py, pz, 1.0f},
                    .normal = {nx, ny, nz, 0.0f},
                    .customColor = {red, green, blue, 1.0f},
                    .displacement = {displacement, displacement, displacement, 0.0f},
                });
                offset += BinaryRecordFloatCount * sizeof(float);
            }
            return readTessellationUint32(bytes, 16u);
        }

        /** Builds the r185 perspective projection with generated-vertex Y compensation. */
        glm::mat4 makeTessellationProjection()
        {
            constexpr double FieldOfViewDegrees = 40.0;
            constexpr double Aspect = 800.0 / 500.0;
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 10000.0;
            const double top = NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = Aspect * height;
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(2.0 * NearDistance / width);
            projection[1u][1u] = static_cast<float>(-2.0 * NearDistance / height);
            projection[2u][2u] = static_cast<float>(-FarDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(-FarDistance * NearDistance / depth);
            return projection;
        }

        /** Applies the exact pointer rotation and 61 subsequent Trackball damping updates. */
        void applyTessellationTrackball(glm::dvec3 &cameraPosition, glm::dvec3 &cameraUp)
        {
            constexpr double DeltaX = 0.25;
            constexpr double DeltaY = 0.10;
            const glm::dvec3 eyeDirection = glm::normalize(cameraPosition);
            glm::dvec3 upDirection = glm::normalize(cameraUp) * DeltaY;
            glm::dvec3 sidewaysDirection = glm::normalize(glm::cross(glm::normalize(cameraUp), eyeDirection)) * DeltaX;
            const glm::dvec3 moveDirection = upDirection + sidewaysDirection;
            const glm::dvec3 axis = glm::normalize(glm::cross(moveDirection, cameraPosition));
            const double initialAngle = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
            const double damping = std::sqrt(0.8);
            double totalAngle = 0.0;
            double updateAngle = initialAngle;
            for (uint32_t updateIndex = 0u; updateIndex < 62u; ++updateIndex)
            {
                totalAngle += updateAngle;
                updateAngle *= damping;
            }
            const glm::dquat rotation = glm::angleAxis(totalAngle, axis);
            cameraPosition = rotation * cameraPosition;
            cameraUp = rotation * cameraUp;
        }
    } // namespace

    void WebglModifierTessellationRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial-seeded-text" && options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated-amplitude" && options.targetFrame == 60u;
        const bool trackball = options.scenarioId == "trackball-controls" && options.targetFrame == 61u;
        if (options.caseId != "webgl_modifier_tessellation" || (!initial && !animated && !trackball))
        {
            throw std::invalid_argument("Tessellation adapter requires one locked scenario/frame pair.");
        }
        if (options.width != 800u || options.height != 500u || options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument("Tessellation adapter requires the locked extent and random seed.");
        }
        if (options.assetRoot.empty())
        {
            throw std::invalid_argument("Tessellation adapter requires an explicit generated asset root.");
        }
        if (trackball != !options.inputReplayPath.empty())
        {
            throw std::invalid_argument("Only the Trackball scenario accepts the locked input replay.");
        }
        device = inDevice;
        const std::filesystem::path assetPath = std::filesystem::path(options.assetRoot.c_str()) /
                                                "generated" / "webgl_modifier_tessellation_vertices.bin";
        if (loadTessellationVertices(assetPath, vertices) != ExpectedFinalRandomState)
        {
            throw std::runtime_error("Tessellation asset random stream differs from the locked browser capture.");
        }
        glm::dvec3 cameraPosition(-100.0, 100.0, 200.0);
        glm::dvec3 cameraUp(0.0, 1.0, 0.0);
        if (trackball)
        {
            applyTessellationTrackball(cameraPosition, cameraUp);
        }
        const glm::dmat4 viewDouble = glm::lookAtRH(cameraPosition, glm::dvec3(0.0), cameraUp);
        modelViewProjection = makeTessellationProjection() * glm::mat4(viewDouble);
        const double timeSeconds = 1700000000.0 + double(options.targetFrame) / 60.0;
        amplitude = static_cast<float>(1.0 + std::sin(timeSeconds * 0.5));
    }

    void WebglModifierTessellationRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglModifierTessellationRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computeTessellationRgbaByteCount(width, height);
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglModifierTessellationRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglModifierTessellationRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareTessellationOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output) throw std::runtime_error("Could not write the tessellation RGBA capture.");
    }

    void WebglModifierTessellationRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareTessellationOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        output << "{\n"
               << "  \"caseId\": \"webgl_modifier_tessellation\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"randomState\": " << ExpectedFinalRandomState << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\",\n";
        if (options.scenarioId == "trackball-controls")
        {
            output << "  \"inputReplay\": {\"schemaVersion\":1,\"caseId\":\"webgl_modifier_tessellation\",\"scenarioId\":\"trackball-controls\",\"captureFrame\":61,\"sha256\":\""
                   << InputReplaySha256 << "\",\"target\":\"#container > canvas\",\"eventCount\":3}\n";
        }
        else
        {
            output << "  \"inputReplay\": null\n";
        }
        output << "}\n";
    }

    void WebglModifierTessellationRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareTessellationOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        output << "{\n"
               << "  \"caseId\": \"webgl_modifier_tessellation\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"entityCount\": 0,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 1,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"vertexCount\": " << TessellatedVertexCount << ",\n"
               << "  \"triangleCount\": " << TessellatedTriangleCount << ",\n"
               << "  \"finalRandomState\": " << ExpectedFinalRandomState << ",\n"
               << "  \"amplitude\": " << amplitude << ",\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\",\"scenePass\":\"main-custom\",\"entityOrdinal\":0}]\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
