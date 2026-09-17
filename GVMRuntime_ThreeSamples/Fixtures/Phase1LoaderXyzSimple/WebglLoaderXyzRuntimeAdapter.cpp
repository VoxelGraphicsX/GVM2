#include "WebglLoaderXyzRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <EASTL/string.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t XyzPointCount = 201u;
        constexpr uint32_t XyzVertexCount = XyzPointCount * 4u;
        constexpr uint32_t XyzIndexCount = XyzPointCount * 6u;
        constexpr uint32_t XyzRandomSeed = DefaultThreeRandomSeed;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *XyzAssetSha256 =
            "489c27c4b619c9a47c15df62ebb7a5474791a7ae85f0c9c3f8323a9504288519";

        static_assert(sizeof(WebglLoaderXyzVertex) == 20u);
        static_assert(offsetof(WebglLoaderXyzVertex, position) == 0u);
        static_assert(offsetof(WebglLoaderXyzVertex, corner) == 12u);

        /** Creates parent directories for one explicitly requested loader artifact. */
        void prepareXyzOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeXyzRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_loader_xyz RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Validates the three manifest-locked scenarios, extent, seed, and input policy. */
        void validateXyzScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_loader_xyz")
            {
                throw std::invalid_argument(
                    "XYZ runtime adapter requires case-id webgl_loader_xyz.");
            }
            const bool initial =
                options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool canonical =
                options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 120u;
            if (!initial && !canonical && !animated)
            {
                throw std::invalid_argument(
                    "webgl_loader_xyz requires initial-loader/frame 0, "
                    "canonical-loader/frame 0, or animated/frame 120.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument(
                    "webgl_loader_xyz requires the locked 800x500 extent.");
            }
            if (options.randomSeed != XyzRandomSeed)
            {
                throw std::invalid_argument(
                    "webgl_loader_xyz requires the default Three random seed.");
            }
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "webgl_loader_xyz requires explicit --asset-root.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "webgl_loader_xyz scenarios do not accept input replays.");
            }
        }

        /** Reads the bounded immutable XYZ asset into exact bytes. */
        eastl::vector<uint8_t> readXyzAssetBytes(const std::filesystem::path &assetPath)
        {
            std::ifstream input(assetPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open pinned XYZ asset: " + assetPath.string());
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                static_cast<uint64_t>(byteCount) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error("Pinned XYZ asset has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete pinned XYZ asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded byte sequence. */
        eastl::string calculateXyzSha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
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

        /** Parses XYZ-only rows with JavaScript-double input and Float32 attribute conversion. */
        eastl::vector<glm::vec3> parseXyzPositions(
            const eastl::vector<uint8_t> &assetBytes)
        {
            const std::string text(
                reinterpret_cast<const char *>(assetBytes.data()), assetBytes.size());
            std::istringstream input(text);
            eastl::vector<glm::vec3> positions;
            std::string line;
            while (std::getline(input, line))
            {
                const size_t first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos || line[first] == '#')
                {
                    continue;
                }
                std::istringstream row(line.substr(first));
                double x = 0.0;
                double y = 0.0;
                double z = 0.0;
                std::string trailing;
                if (!(row >> x >> y >> z) || (row >> trailing))
                {
                    throw std::runtime_error(
                        "Pinned helix_201.xyz contains a non-XYZ data row.");
                }
                positions.push_back(glm::vec3(float(x), float(y), float(z)));
            }
            if (positions.size() != XyzPointCount)
            {
                throw std::runtime_error(
                    "Pinned helix_201.xyz did not produce exactly 201 points.");
            }
            return positions;
        }

        /** Applies BufferGeometry.center and builds the deterministic triangle-list expansion. */
        glm::vec3 buildXyzBillboards(
            const eastl::vector<glm::vec3> &positions,
            eastl::vector<WebglLoaderXyzVertex> &vertices,
            eastl::vector<uint> &indices)
        {
            glm::vec3 minimum(std::numeric_limits<float>::infinity());
            glm::vec3 maximum(-std::numeric_limits<float>::infinity());
            for (const glm::vec3 &position : positions)
            {
                minimum = glm::min(minimum, position);
                maximum = glm::max(maximum, position);
            }
            const glm::dvec3 centerDouble =
                (glm::dvec3(minimum) + glm::dvec3(maximum)) * 0.5;
            const glm::vec3 center(centerDouble);
            const glm::vec2 corners[4] = {
                glm::vec2(-1.0f, -1.0f),
                glm::vec2(1.0f, -1.0f),
                glm::vec2(1.0f, 1.0f),
                glm::vec2(-1.0f, 1.0f),
            };

            vertices.clear();
            indices.clear();
            vertices.reserve(XyzVertexCount);
            indices.reserve(XyzIndexCount);
            for (uint32_t pointIndex = 0u; pointIndex < XyzPointCount; ++pointIndex)
            {
                const glm::vec3 source = positions[pointIndex];
                const glm::vec3 centered(
                    float(double(source.x) - centerDouble.x),
                    float(double(source.y) - centerDouble.y),
                    float(double(source.z) - centerDouble.z));
                const uint baseVertex = uint(vertices.size());
                for (const glm::vec2 &corner : corners)
                {
                    WebglLoaderXyzVertex vertex;
                    vertex.position = float3(centered.x, centered.y, centered.z);
                    vertex.corner = float2(corner.x, corner.y);
                    vertices.push_back(vertex);
                }
                indices.push_back(baseVertex + 0u);
                indices.push_back(baseVertex + 1u);
                indices.push_back(baseVertex + 2u);
                indices.push_back(baseVertex + 0u);
                indices.push_back(baseVertex + 2u);
                indices.push_back(baseVertex + 3u);
            }
            return center;
        }

        /** Builds Three's XYZ Euler object matrix with JavaScript-double intermediates. */
        glm::dmat4 makeXyzObjectMatrix(double rotationX, double rotationY)
        {
            const double sineX = std::sin(rotationX);
            const double cosineX = std::cos(rotationX);
            const double sineY = std::sin(rotationY);
            const double cosineY = std::cos(rotationY);
            glm::dmat4 model(1.0);
            model[0u][0u] = cosineY;
            model[0u][1u] = sineX * sineY;
            model[0u][2u] = -cosineX * sineY;
            model[1u][0u] = 0.0;
            model[1u][1u] = cosineX;
            model[1u][2u] = sineX;
            model[2u][0u] = sineY;
            model[2u][1u] = -sineX * cosineY;
            model[2u][2u] = cosineX * cosineY;
            return model;
        }

        /** Builds Three's perspective projection with the backend canvas-orientation flip. */
        glm::mat4 makeXyzProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 50.0;
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 100.0;
            const double top = NearDistance *
                               std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                double(width) / double(height) * projectionHeight;
            const double projectionDepth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] =
                static_cast<float>(2.0 * NearDistance / projectionWidth);
            result[1u][1u] =
                static_cast<float>(-2.0 * NearDistance / projectionHeight);
            result[2u][2u] = static_cast<float>(
                -(FarDistance + NearDistance) / projectionDepth);
            result[2u][3u] = -1.0f;
            result[3u][2u] = static_cast<float>(
                -2.0 * FarDistance * NearDistance / projectionDepth);
            return result;
        }

        /** Builds the fixed camera view followed by the target-frame Points rotation. */
        glm::mat4 makeXyzModelView(double rotationX, double rotationY)
        {
            const glm::dmat4 view = glm::lookAtRH(
                glm::dvec3(10.0, 7.0, 10.0),
                glm::dvec3(0.0),
                glm::dvec3(0.0, 1.0, 0.0));
            return glm::mat4(view * makeXyzObjectMatrix(rotationX, rotationY));
        }
    } // namespace

    void WebglLoaderXyzRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateXyzScenario(options);
        device = inDevice;
        const std::filesystem::path assetPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "xyz" / "helix_201.xyz";
        const eastl::vector<uint8_t> assetBytes = readXyzAssetBytes(assetPath);
        if (calculateXyzSha256(assetBytes) != XyzAssetSha256)
        {
            throw std::invalid_argument(
                "models/xyz/helix_201.xyz differs from the pinned Three r185 asset.");
        }
        geometryCenter = buildXyzBillboards(
            parseXyzPositions(assetBytes), vertices, indices);

        timeSeconds = double(options.targetFrame) / 60.0;
        const double rotationXDouble = timeSeconds * 0.2;
        const double rotationYDouble = timeSeconds * 0.5;
        rotationX = float(rotationXDouble);
        rotationY = float(rotationYDouble);
        modelView = makeXyzModelView(rotationXDouble, rotationYDouble);
        projection = makeXyzProjection(options.width, options.height);
    }

    void WebglLoaderXyzRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderXyzRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computeXyzRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_loader_xyz capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_loader_xyz could not access the graphics queue.");
        }
        graphicsQueue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        writeSemanticSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglLoaderXyzRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglLoaderXyzRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareXyzOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_loader_xyz RGBA output.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write webgl_loader_xyz RGBA output.");
        }
    }

    void WebglLoaderXyzRuntimeAdapter::writeCaptureMetadata(
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
        prepareXyzOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_loader_xyz metadata output.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_loader_xyz\",\n"
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

    void WebglLoaderXyzRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareXyzOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open webgl_loader_xyz snapshot output.");
        }
        const char *canonicalState = options.scenarioId == "initial-loader"
            ? "helix-201-white-square-points-time-zero"
            : options.scenarioId == "canonical-loader"
                ? "201-centered-uncolored-xyz-points-one-draw"
                : "fixed-step-two-seconds-x-y-rotation";
        output << std::fixed << std::setprecision(9);
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_loader_xyz\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"canonicalState\": \"" << canonicalState << "\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"pointCount\": " << XyzPointCount << ",\n"
               << "  \"expandedVertexCount\": " << XyzVertexCount << ",\n"
               << "  \"expandedIndexCount\": " << XyzIndexCount << ",\n"
               << "  \"vertexStrideBytes\": 20,\n"
               << "  \"standaloneGeometryBufferCount\": 2,\n"
               << "  \"sourcePrimitiveTopology\": \"points\",\n"
               << "  \"expandedPrimitiveTopology\": \"triangle-list\",\n"
               << "  \"logicalScenePassCount\": 1,\n"
               << "  \"logicalDrawCommandCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"coverageSampleCount\": 1,\n"
               << "  \"physicalSceneDrawCommandCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"assetPath\": \"models/xyz/helix_201.xyz\",\n"
               << "  \"assetSha256\": \"" << XyzAssetSha256 << "\",\n"
               << "  \"geometryHasColor\": false,\n"
               << "  \"geometryCenter\": [" << geometryCenter.x << ", "
               << geometryCenter.y << ", " << geometryCenter.z << "],\n"
               << "  \"pointsMaterialSize\": 0.100000000,\n"
               << "  \"perspectiveSizeScale\": 250.000000000,\n"
               << "  \"timeSeconds\": " << timeSeconds << ",\n"
               << "  \"rotationX\": " << rotationX << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"cameraFovDegrees\": 50.000000000,\n"
               << "  \"cameraNear\": 0.100000000,\n"
               << "  \"cameraFar\": 100.000000000,\n"
               << "  \"cameraPosition\": [10.000000000, 7.000000000, 10.000000000],\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"point-billboards\","
                  "\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"sceneRoots\": [\n"
               << "    {\"id\":\"scene\",\"renderSetCount\":0,\"renderSetId\":null,"
                  "\"renderSetType\":null,\"renderableObjectCount\":1,\"entityCount\":0,"
                  "\"entities\":[],\"drawCommandCount\":1,\"directDrawFallback\":false,"
                  "\"scenePasses\":[{\"name\":\"point-billboards\","
                  "\"renderClass\":\"WebglLoaderXyzPointPass\",\"renderSetId\":null,"
                  "\"renderSetBindingCount\":0,\"drawMode\":\"explicit-indexed\","
                  "\"invocationCount\":1,\"drawCommandCount\":1,"
                  "\"usesStandaloneGeometry\":true,\"usesExplicitDrawCount\":true}]}\n"
               << "  ],\n"
               << "  \"screenPasses\": [],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }

    void WebglLoaderXyzRuntimeAdapter::writeSemanticSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.semanticSnapshotPath.empty()
            || options.scenarioId != "canonical-loader")
        {
            return;
        }
        const std::filesystem::path outputPath(options.semanticSnapshotPath.c_str());
        prepareXyzOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_loader_xyz semantic snapshot output.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_loader_xyz\",\n"
               << "  \"scenarioId\": \"canonical-loader\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"kind\": \"loader-snapshot\",\n"
               << "  \"canonicalState\": \"201-centered-uncolored-xyz-points-one-draw\",\n"
               << "  \"result\": {\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"sceneRootCount\": 1,\n"
               << "    \"canonicalSceneSha256\": \"" << XyzAssetSha256 << "\"\n"
               << "  }\n"
               << "}\n";
        if (!output)
        {
            throw std::runtime_error(
                "Could not write webgl_loader_xyz semantic snapshot output.");
        }
    }
} // namespace GVM::ThreeSamples
