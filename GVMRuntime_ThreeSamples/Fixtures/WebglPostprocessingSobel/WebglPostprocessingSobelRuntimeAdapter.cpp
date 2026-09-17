#include "WebglPostprocessingSobelRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr double Pi = 3.1415926535897932384626433832795;
        constexpr uint32_t TubularSegments = 256u;
        constexpr uint32_t RadialSegments = 32u;
        constexpr uint32_t ExpectedVertexCount =
            (TubularSegments + 1u) * (RadialSegments + 1u);
        constexpr uint32_t ExpectedIndexCount =
            TubularSegments * RadialSegments * 6u;
        constexpr const char *DisabledReplaySha256 =
            "769e432194f7047ac906dd0ef607542bf8dada1b80deb45faeea9aa9b178cb76";
        constexpr const char *OrbitReplaySha256 =
            "a6c977403217c3ae680778975a6e07b7eb9b75d0b40cd8214c8bc8ba4952710e";

        /** Stores one double-precision vector used by Three geometry math. */
        struct SobelDoubleVector
        {
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
        };

        /** Creates parent directories for one requested output artifact. */
        void prepareSobelOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Reads one immutable replay payload into bounded bytes. */
        eastl::vector<uint8_t> readSobelReplay(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open the Sobel input replay.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "The Sobel input replay is empty.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(
                static_cast<size_t>(byteCount));
            input.read(
                reinterpret_cast<char *>(bytes.data()),
                byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the Sobel input replay.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one replay payload. */
        eastl::string calculateSobelSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Sobel replay is too large for SHA-256.");
            }
            uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest);
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(CC_SHA256_DIGEST_LENGTH * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Returns one TorusKnot center point from the exact r185 equation. */
        SobelDoubleVector calculateSobelCurvePosition(double u)
        {
            constexpr double Radius = 1.0;
            constexpr double P = 2.0;
            constexpr double Q = 3.0;
            const double cu = std::cos(u);
            const double su = std::sin(u);
            const double quOverP = Q / P * u;
            const double cs = std::cos(quOverP);
            return {
                Radius * (2.0 + cs) * 0.5 * cu,
                Radius * (2.0 + cs) * 0.5 * su,
                Radius * std::sin(quOverP) * 0.5,
            };
        }

        /** Returns the difference of two double-precision vectors. */
        SobelDoubleVector subtractSobelVector(
            const SobelDoubleVector &left,
            const SobelDoubleVector &right)
        {
            return {
                left.x - right.x,
                left.y - right.y,
                left.z - right.z,
            };
        }

        /** Returns the sum of two double-precision vectors. */
        SobelDoubleVector addSobelVector(
            const SobelDoubleVector &left,
            const SobelDoubleVector &right)
        {
            return {
                left.x + right.x,
                left.y + right.y,
                left.z + right.z,
            };
        }

        /** Returns the right-handed cross product of two vectors. */
        SobelDoubleVector crossSobelVector(
            const SobelDoubleVector &left,
            const SobelDoubleVector &right)
        {
            return {
                left.y * right.z - left.z * right.y,
                left.z * right.x - left.x * right.z,
                left.x * right.y - left.y * right.x,
            };
        }

        /** Returns one normalized double-precision vector. */
        SobelDoubleVector normalizeSobelVector(
            const SobelDoubleVector &value)
        {
            const double length = std::sqrt(
                value.x * value.x +
                value.y * value.y +
                value.z * value.z);
            if (length <= 0.0)
            {
                throw std::runtime_error(
                    "Sobel TorusKnot generated a zero-length basis.");
            }
            return {
                value.x / length,
                value.y / length,
                value.z / length,
            };
        }

        /** Builds Three r185's exact 8,481-vertex TorusKnotGeometry. */
        void buildSobelTorusKnot(
            eastl::vector<WebglPostprocessingSobelVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr double Tube = 0.3;
            constexpr double P = 2.0;
            vertices.clear();
            indices.clear();
            vertices.reserve(ExpectedVertexCount);
            indices.reserve(ExpectedIndexCount);
            for (uint32_t tubular = 0u;
                 tubular <= TubularSegments;
                 ++tubular)
            {
                const double u =
                    double(tubular) /
                    double(TubularSegments) *
                    P * Pi * 2.0;
                const SobelDoubleVector center =
                    calculateSobelCurvePosition(u);
                const SobelDoubleVector next =
                    calculateSobelCurvePosition(u + 0.01);
                const SobelDoubleVector tangent =
                    subtractSobelVector(next, center);
                SobelDoubleVector normalBasis =
                    addSobelVector(next, center);
                const SobelDoubleVector binormal =
                    normalizeSobelVector(
                        crossSobelVector(tangent, normalBasis));
                normalBasis =
                    normalizeSobelVector(
                        crossSobelVector(binormal, tangent));
                for (uint32_t radial = 0u;
                     radial <= RadialSegments;
                     ++radial)
                {
                    const double v =
                        double(radial) /
                        double(RadialSegments) *
                        Pi * 2.0;
                    const double cx = -Tube * std::cos(v);
                    const double cy = Tube * std::sin(v);
                    const SobelDoubleVector position = {
                        center.x +
                            cx * normalBasis.x +
                            cy * binormal.x,
                        center.y +
                            cx * normalBasis.y +
                            cy * binormal.y,
                        center.z +
                            cx * normalBasis.z +
                            cy * binormal.z,
                    };
                    const SobelDoubleVector normal =
                        normalizeSobelVector(
                            subtractSobelVector(position, center));
                    vertices.push_back({
                        {
                            static_cast<float>(position.x),
                            static_cast<float>(position.y),
                            static_cast<float>(position.z),
                            1.0f,
                        },
                        {
                            static_cast<float>(normal.x),
                            static_cast<float>(normal.y),
                            static_cast<float>(normal.z),
                            0.0f,
                        },
                    });
                }
            }
            for (uint32_t tubular = 1u;
                 tubular <= TubularSegments;
                 ++tubular)
            {
                for (uint32_t radial = 1u;
                     radial <= RadialSegments;
                     ++radial)
                {
                    const uint32_t a =
                        (RadialSegments + 1u) *
                            (tubular - 1u) +
                        radial - 1u;
                    const uint32_t b =
                        (RadialSegments + 1u) * tubular +
                        radial - 1u;
                    const uint32_t c =
                        (RadialSegments + 1u) * tubular +
                        radial;
                    const uint32_t d =
                        (RadialSegments + 1u) *
                            (tubular - 1u) +
                        radial;
                    indices.insert(
                        indices.end(),
                        {a, d, b, b, d, c});
                }
            }
            if (vertices.size() != ExpectedVertexCount ||
                indices.size() != ExpectedIndexCount)
            {
                throw std::runtime_error(
                    "Sobel TorusKnot counts differ from Three r185.");
            }
        }

        /** Builds Three's OpenGL perspective before DSL clip conversion. */
        glm::mat4 makeSobelProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfView = 70.0;
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 100.0;
            const double top =
                NearDistance *
                std::tan(FieldOfView * Pi / 360.0);
            const double projectionHeight = top * 2.0;
            const double projectionWidth =
                double(width) /
                double(height) *
                projectionHeight;
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(
                2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(
                2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(
                -(FarDistance + NearDistance) / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -2.0 * FarDistance * NearDistance / depth);
            return projection;
        }

        /** Converts one sRGB byte channel into Three's linear color space. */
        float sobelSrgbByteToLinear(uint32_t channel)
        {
            const double value = double(channel) / 255.0;
            return static_cast<float>(
                value <= 0.04045
                    ? value / 12.92
                    : std::pow((value + 0.055) / 1.055, 2.4));
        }

        /** Resolves and validates the immutable scenario and replay identity. */
        void resolveSobelScenario(
            const ThreeSampleHostOptions &options,
            bool &effectEnabled,
            glm::dvec3 &cameraPosition)
        {
            if (options.caseId != "webgl_postprocessing_sobel" ||
                options.randomSeed != 0x12345678u)
            {
                throw std::invalid_argument(
                    "Sobel adapter requires its case and fixed seed.");
            }
            effectEnabled = true;
            cameraPosition = glm::dvec3(0.0, 1.0, 3.0);
            if (options.scenarioId == "initial-enabled" &&
                options.targetFrame == 0u &&
                options.inputReplayPath.empty())
            {
                return;
            }
            const bool disabled =
                options.scenarioId == "effect-disabled" &&
                options.targetFrame == 1u;
            const bool orbit =
                options.scenarioId == "orbit-input" &&
                options.targetFrame == 1u;
            if ((!disabled && !orbit) ||
                options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "Sobel scenario differs from the Manifest.");
            }
            const eastl::vector<uint8_t> replay =
                readSobelReplay(std::filesystem::path(
                    options.inputReplayPath.c_str()));
            const eastl::string sha256 =
                calculateSobelSha256(replay);
            const char *expectedSha256 =
                disabled
                ? DisabledReplaySha256
                : OrbitReplaySha256;
            if (sha256 != expectedSha256)
            {
                throw std::invalid_argument(
                    "Sobel replay differs from its locked identity.");
            }
            if (disabled)
            {
                effectEnabled = false;
                return;
            }
            const double radius = std::sqrt(10.0);
            const double theta = -2.0 * Pi * 60.0 / 500.0;
            const double phi =
                std::acos(1.0 / radius) +
                2.0 * Pi * 30.0 / 500.0;
            cameraPosition = glm::dvec3(
                radius * std::sin(phi) * std::sin(theta),
                radius * std::cos(phi),
                radius * std::sin(phi) * std::cos(theta));
        }

        /** Computes one safe tightly packed RGBA8 capture size. */
        uint64_t computeSobelRgbaByteCount(
            uint32_t width,
            uint32_t height)
        {
            const uint64_t pixelCount =
                uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / 4u)
            {
                throw std::overflow_error(
                    "Sobel capture size overflowed.");
            }
            return pixelCount * 4u;
        }
    } // namespace

    void WebglPostprocessingSobelRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        device = inDevice;
        glm::dvec3 cameraPosition;
        resolveSobelScenario(
            options,
            effectEnabled,
            cameraPosition);
        buildSobelTorusKnot(vertices, indices);
        projectionMatrix =
            makeSobelProjection(options.width, options.height);
        modelViewMatrix = glm::mat4(glm::lookAtRH(
            cameraPosition,
            glm::dvec3(0.0),
            glm::dvec3(0.0, 1.0, 0.0)));
        ambientLight = sobelSrgbByteToLinear(0xe7u);
    }

    void WebglPostprocessingSobelRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPostprocessingSobelRuntimeAdapter::afterFrame(
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
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(
                computeSobelRgbaByteCount(width, height)));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        writeArtifacts(
            options,
            frameIndex,
            width,
            height,
            rgba);
        captureWritten = true;
    }

    void WebglPostprocessingSobelRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareSobelOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareSobelOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n"
                   << "  \"schemaVersion\":1,\n"
                   << "  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\"webgl_postprocessing_sobel\",\n"
                   << "  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n"
                   << "  \"pipeline\":\""
                   << options.pipeline.c_str() << "\",\n"
                   << "  \"backend\":\""
                   << threeSampleBackendName(options.backend) << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"randomSeed\":"
                   << options.randomSeed << ",\n"
                   << "  \"width\":" << width << ",\n"
                   << "  \"height\":" << height << ",\n"
                   << "  \"rowStrideBytes\":" << width * 4u << ",\n"
                   << "  \"format\":\"rgba8unorm\",\n"
                   << "  \"byteCount\":" << rgba.size() << ",\n"
                   << "  \"sampleCount\":1,\n"
                   << "  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"inputReplay\":";
            if (options.inputReplayPath.empty())
            {
                output << "null\n}\n";
            }
            else
            {
                const bool disabled =
                    options.scenarioId == "effect-disabled";
                output
                    << "{\n"
                    << "    \"sha256\":\""
                    << (disabled
                            ? DisabledReplaySha256
                            : OrbitReplaySha256)
                    << "\",\n"
                    << "    \"caseId\":\"webgl_postprocessing_sobel\",\n"
                    << "    \"scenarioId\":\""
                    << options.scenarioId.c_str() << "\",\n"
                    << "    \"captureFrame\":1,\n"
                    << "    \"eventCount\":"
                    << (disabled ? 1 : 3) << ",\n"
                    << "    \"target\":\"body canvas\"\n"
                    << "  }\n}\n";
            }
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareSobelOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n"
                   << "  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgl_postprocessing_sobel\",\n"
                   << "  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"renderSetPolicy\":\"not-required\",\n"
                   << "  \"sceneRenderSetCount\":0,\n"
                   << "  \"renderableObjectCount\":1,\n"
                   << "  \"entityCount\":1,\n"
                   << "  \"instanceCount\":1,\n"
                   << "  \"drawCommandCount\":"
                   << (effectEnabled ? 3 : 2) << ",\n"
                   << "  \"scenePassCount\":1,\n"
                   << "  \"screenPassCount\":"
                   << (effectEnabled ? 2 : 1) << ",\n"
                   << "  \"vertexCount\":8481,\n"
                   << "  \"indexCount\":49152,\n"
                   << "  \"effectEnabled\":"
                   << (effectEnabled ? "true" : "false")
                   << "\n}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareSobelOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n"
                   << "  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgl_postprocessing_sobel\",\n"
                   << "  \"geometry\":\"TorusKnotGeometry(1,0.3,256,32,2,3)\",\n"
                   << "  \"vertexCount\":8481,\n"
                   << "  \"indexCount\":49152,\n"
                   << "  \"scenePass\":\"WebglPostprocessingSobelMainPass\",\n"
                   << "  \"screenPasses\":["
                   << (effectEnabled
                           ? "\"luminance\",\"sobel\""
                           : "\"output-color-conversion\"")
                   << "]\n}\n";
        }
    }

    void WebglPostprocessingSobelRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        captureWritten = false;
    }
} // namespace GVM::ThreeSamples
