#include "WebglPointsBillboardsRuntimeAdapter.hpp"

#include <EASTL/algorithm.h>

#include <glm/gtc/matrix_transform.hpp>

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
        constexpr uint32_t PointCount = 10000u;
        constexpr uint32_t VertexCount = 40000u;
        constexpr uint32_t IndexCount = 60000u;
        constexpr uint32_t TextureExtent = 32u;
        constexpr uint32_t TextureMipCount = 6u;
        constexpr uint32_t ExpectedFinalRandomState = 345525395u;
        constexpr double Pi = 3.14159265358979323846;
        constexpr const char *ReplaySha256 = "aed6e0b80a918c65928c83be85b3dd3c0abc4a413d594c743c698a525d5d1ac8";

        /** Creates parent directories for one explicitly requested output artifact. */
        void prepareBillboardOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes one bounded tightly packed RGBA8 capture size. */
        uint64_t computeBillboardRgbaByteCount(uint32_t width, uint32_t height)
        {
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / 4u)
            {
                throw std::overflow_error("Billboard RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * 4u;
        }

        /** Reads one little-endian uint32 from a bounded generated texture asset. */
        uint32_t readBillboardUint32(const eastl::vector<uint8_t> &bytes, size_t offset)
        {
            if (offset + 4u > bytes.size()) throw std::runtime_error("Billboard texture asset is truncated.");
            return uint32_t(bytes[offset]) |
                   (uint32_t(bytes[offset + 1u]) << 8u) |
                   (uint32_t(bytes[offset + 2u]) << 16u) |
                   (uint32_t(bytes[offset + 3u]) << 24u);
        }

        /** Loads the decoded, vertically flipped 32x32 browser texture bytes. */
        eastl::vector<uint8_t> loadBillboardTexture(const std::filesystem::path &assetPath)
        {
            std::ifstream input(assetPath, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open generated point texture asset.");
            const std::streamoff byteCount = input.tellg();
            input.seekg(0, std::ios::beg);
            if (byteCount != std::streamoff(16u + TextureExtent * TextureExtent * 4u))
            {
                throw std::runtime_error("Generated point texture asset has an invalid byte count.");
            }
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input || std::memcmp(bytes.data(), "DISC185\0", 8u) != 0 ||
                readBillboardUint32(bytes, 8u) != 1u || readBillboardUint32(bytes, 12u) != TextureExtent)
            {
                throw std::runtime_error("Generated point texture asset header is invalid.");
            }
            return eastl::vector<uint8_t>(bytes.begin() + 16u, bytes.end());
        }

        /** Builds one raw-UNORM 2x2 mip using browser-compatible nearest integer rounding. */
        eastl::vector<uint8_t> buildNextBillboardMip(const eastl::vector<uint8_t> &source,
                                                     uint32_t sourceExtent)
        {
            const uint32_t targetExtent = sourceExtent / 2u;
            eastl::vector<uint8_t> target(static_cast<size_t>(targetExtent) * targetExtent * 4u);
            for (uint32_t y = 0u; y < targetExtent; ++y)
            {
                for (uint32_t x = 0u; x < targetExtent; ++x)
                {
                    for (uint32_t channel = 0u; channel < 4u; ++channel)
                    {
                        uint32_t sum = 0u;
                        for (uint32_t sampleY = 0u; sampleY < 2u; ++sampleY)
                        {
                            for (uint32_t sampleX = 0u; sampleX < 2u; ++sampleX)
                            {
                                const size_t sourceOffset =
                                    (static_cast<size_t>(y * 2u + sampleY) * sourceExtent +
                                     x * 2u + sampleX) * 4u + channel;
                                sum += source[sourceOffset];
                            }
                        }
                        target[(static_cast<size_t>(y) * targetExtent + x) * 4u + channel] =
                            static_cast<uint8_t>((sum + 2u) / 4u);
                    }
                }
            }
            return target;
        }

        /** Builds all six explicit point-texture mips from the decoded base level. */
        eastl::vector<eastl::vector<uint8_t>> buildBillboardMips(eastl::vector<uint8_t> base)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(TextureMipCount);
            result.push_back(eastl::move(base));
            uint32_t sourceExtent = TextureExtent;
            while (sourceExtent > 1u)
            {
                result.push_back(buildNextBillboardMip(result.back(), sourceExtent));
                sourceExtent /= 2u;
            }
            if (result.size() != TextureMipCount) throw std::logic_error("Billboard mip count is invalid.");
            return result;
        }

        /** Advances the shared xorshift stream by an exact browser-constructor draw count. */
        void advanceBillboardRandom(ThreeCompat::DeterministicRandom &random, uint32_t drawCount)
        {
            for (uint32_t draw = 0u; draw < drawCount; ++draw) (void)random.nextUint32();
        }

        /** Builds the exact 10,000 Float32 point centers and their expanded quads. */
        uint32_t buildBillboardGeometry(eastl::vector<WebglPointsBillboardsVertex> &vertices,
                                        eastl::vector<uint32_t> &indices)
        {
            ThreeCompat::DeterministicRandom random(DefaultThreeRandomSeed);
            advanceBillboardRandom(random, 96u);
            vertices.clear();
            indices.clear();
            vertices.reserve(VertexCount);
            indices.reserve(IndexCount);
            constexpr float Corners[4u][2u] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
            for (uint32_t pointIndex = 0u; pointIndex < PointCount; ++pointIndex)
            {
                const float x = static_cast<float>(2000.0 * double(random.nextFloat()) - 1000.0);
                const float y = static_cast<float>(2000.0 * double(random.nextFloat()) - 1000.0);
                const float z = static_cast<float>(2000.0 * double(random.nextFloat()) - 1000.0);
                const uint32_t baseVertex = pointIndex * 4u;
                for (const auto &corner : Corners)
                {
                    vertices.push_back({.center = {x, y, z, 1.0f}, .corner = {corner[0], corner[1], 0.0f, 0.0f}});
                }
                const uint32_t pointIndices[6u] = {
                    baseVertex, baseVertex + 1u, baseVertex + 2u,
                    baseVertex + 2u, baseVertex + 1u, baseVertex + 3u};
                indices.insert(indices.end(), pointIndices, pointIndices + 6u);
            }
            advanceBillboardRandom(random, 44u);
            if (vertices.size() != VertexCount || indices.size() != IndexCount)
            {
                throw std::logic_error("Billboard geometry count differs from the locked plan.");
            }
            return random.getState();
        }

        /** Builds Three's perspective projection with generated-vertex Y compensation. */
        glm::mat4 makeBillboardProjection()
        {
            constexpr double FieldOfViewDegrees = 55.0;
            constexpr double Aspect = 800.0 / 500.0;
            constexpr double NearDistance = 2.0;
            constexpr double FarDistance = 2000.0;
            const double top = NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double height = top * 2.0;
            const double width = Aspect * height;
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] = static_cast<float>(2.0 * NearDistance / width);
            result[1u][1u] = static_cast<float>(-2.0 * NearDistance / height);
            result[2u][2u] = static_cast<float>(-FarDistance / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] = static_cast<float>(-FarDistance * NearDistance / depth);
            return result;
        }

        /** Evaluates one wrapped hue channel for Three's HSL conversion. */
        double evaluateBillboardHueChannel(double q, double p, double value)
        {
            value -= std::floor(value);
            if (value < 1.0 / 6.0) return q + (p - q) * 6.0 * value;
            if (value < 0.5) return p;
            if (value < 2.0 / 3.0) return q + (p - q) * 6.0 * (2.0 / 3.0 - value);
            return q;
        }

        /** Converts the animated HSL value into Three's linear working-space RGB. */
        glm::vec3 makeBillboardHsl(double hue)
        {
            hue -= std::floor(hue);
            constexpr double saturation = 0.5;
            constexpr double lightness = 0.5;
            const double p = lightness * (1.0 + saturation);
            const double q = 2.0 * lightness - p;
            return glm::vec3(static_cast<float>(evaluateBillboardHueChannel(q, p, hue + 1.0 / 3.0)),
                             static_cast<float>(evaluateBillboardHueChannel(q, p, hue)),
                             static_cast<float>(evaluateBillboardHueChannel(q, p, hue - 1.0 / 3.0)));
        }
    } // namespace

    void WebglPointsBillboardsRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated = options.scenarioId == "animated" && options.targetFrame == 60u;
        const bool attenuationOff = options.scenarioId == "size-attenuation-off" && options.targetFrame == 61u;
        if (options.caseId != "webgl_points_billboards" || (!initial && !animated && !attenuationOff))
        {
            throw std::invalid_argument("Billboard adapter requires one locked scenario/frame pair.");
        }
        if (options.width != 800u || options.height != 500u || options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument("Billboard adapter requires the locked extent and random seed.");
        }
        if (options.assetRoot.empty() || attenuationOff != !options.inputReplayPath.empty())
        {
            throw std::invalid_argument("Billboard adapter requires generated texture assets and only the locked GUI replay.");
        }
        device = inDevice;
        if (buildBillboardGeometry(vertices, indices) != ExpectedFinalRandomState)
        {
            throw std::runtime_error("Billboard random stream differs from the browser reference.");
        }
        textureMips = buildBillboardMips(loadBillboardTexture(
            std::filesystem::path(options.assetRoot.c_str()) / "generated" / "webgl_points_billboards_disc.bin"));
        projection = makeBillboardProjection();
        modelView = glm::mat4(glm::lookAtRH(glm::dvec3(0.0, 0.0, 1000.0),
                                            glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0)));
        materialColor = makeBillboardHsl(double(options.targetFrame) / 1200.0);
        sizeAttenuation = !attenuationOff;
    }

    void WebglPointsBillboardsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPointsBillboardsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = computeBillboardRgbaByteCount(width, height);
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglPointsBillboardsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglPointsBillboardsRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareBillboardOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
        if (!output) throw std::runtime_error("Could not write billboard RGBA capture.");
    }

    void WebglPointsBillboardsRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareBillboardOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        output << "{\n"
               << "  \"caseId\": \"webgl_points_billboards\",\n"
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
        if (options.scenarioId == "size-attenuation-off")
        {
            output << "  \"inputReplay\": {\"schemaVersion\":1,\"caseId\":\"webgl_points_billboards\",\"scenarioId\":\"size-attenuation-off\",\"captureFrame\":61,\"sha256\":\""
                   << ReplaySha256 << "\",\"target\":\".lil-gui .controller.boolean input\",\"eventCount\":1}\n";
        }
        else
        {
            output << "  \"inputReplay\": null\n";
        }
        output << "}\n";
    }

    void WebglPointsBillboardsRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareBillboardOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        output << "{\n"
               << "  \"caseId\": \"webgl_points_billboards\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"gpuWorkDslOnly\": true,\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"logicalPointCount\": " << PointCount << ",\n"
               << "  \"vertexCount\": " << VertexCount << ",\n"
               << "  \"indexCount\": " << IndexCount << ",\n"
               << "  \"textureMipCount\": " << TextureMipCount << ",\n"
               << "  \"sizeAttenuation\": " << (sizeAttenuation ? "true" : "false") << ",\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"directDrawFallback\": false,\n"
               << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\",\"scenePass\":\"main\",\"entityOrdinal\":0}]\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
