#include "WebglMaterialsTexturePartialupdateRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

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
        constexpr uint32_t PartialupdateRandomSeed = 42u;
        constexpr uint32_t PartialupdateTextureExtent = 512u;
        constexpr uint32_t PartialupdatePatchExtent = 32u;
        constexpr uint32_t PartialupdatePatchCount = 9u;
        constexpr uint32_t PrePatchRandomDrawCount = 156u;
        constexpr uint32_t ExpectedInitialRandomState = 78731182u;
        constexpr uint32_t ExpectedFinalRandomState = 3665713169u;
        constexpr const char *CheckerboardAssetSha256 =
            "d7547036c6221a840b80ce11138dc2c1a26df2630419217234c479b00e50a119";

        static_assert(sizeof(WebglMaterialsTexturePartialupdateVertex) == 20u);
        static_assert(sizeof(WebglMaterialsTexturePartialupdatePatch) == 16u);

        /** Creates parent directories for one explicitly requested partial-update artifact. */
        void preparePartialupdateOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computePartialupdateRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_materials_texture_partialupdate RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Validates the two manifest scenarios, deterministic seed, extent, and target frames. */
        void validatePartialupdateScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_materials_texture_partialupdate")
            {
                throw std::invalid_argument(
                    "Partial-update adapter requires case-id webgl_materials_texture_partialupdate.");
            }
            const bool initial =
                options.scenarioId == "initial-loader" && options.targetFrame == 0u;
            const bool patched =
                options.scenarioId == "patched" && options.targetFrame == 60u;
            if (!initial && !patched)
            {
                throw std::invalid_argument(
                    "webgl_materials_texture_partialupdate requires initial-loader/frame 0 or patched/frame 60.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument(
                    "webgl_materials_texture_partialupdate requires the locked 800x500 extent.");
            }
            if (options.randomSeed != PartialupdateRandomSeed)
            {
                throw std::invalid_argument(
                    "webgl_materials_texture_partialupdate requires random seed 42.");
            }
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "webgl_materials_texture_partialupdate requires explicit --asset-root.");
            }
            if (!options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "webgl_materials_texture_partialupdate does not accept an input replay.");
            }
        }

        /** Reads one bounded immutable asset into exact bytes for SHA-256 validation. */
        eastl::vector<uint8_t> readPartialupdateAssetBytes(
            const std::filesystem::path &inputPath)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open Three r185 checkerboard asset: " + inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error("Three r185 checkerboard asset has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error("Could not read the complete Three r185 checkerboard asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte sequence. */
        eastl::string calculatePartialupdateSha256(const eastl::vector<uint8_t> &bytes)
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

        /** Returns one exact JavaScript Math.random value from the deterministic stream. */
        double nextPartialupdateRandomDouble(ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Converts one authored sRGB byte to Three's linear working-space scalar. */
        double convertPartialupdateSrgbByteToLinear(uint32_t encodedByte)
        {
            const double encoded = double(encodedByte) / 255.0;
            if (encoded < 0.04045)
            {
                return encoded * 0.0773993808;
            }
            return std::pow(encoded * 0.9478672986 + 0.0521327014, 2.4);
        }

        /** Converts one random hexadecimal channel into updateDataTexture's stored byte. */
        uint8_t makePartialupdateStoredColorByte(uint32_t encodedByte)
        {
            return static_cast<uint8_t>(
                std::floor(convertPartialupdateSrgbByteToLinear(encodedByte) * 255.0));
        }

        /** Builds the indexed PlaneGeometry payload used by the single Scene object. */
        void buildPartialupdatePlane(
            eastl::vector<WebglMaterialsTexturePartialupdateVertex> &vertices,
            eastl::vector<uint> &indices)
        {
            vertices = {
                {.position = float3(-1.0f, -1.0f, 0.0f), .texCoord = float2(0.0f, 0.0f)},
                {.position = float3(1.0f, -1.0f, 0.0f), .texCoord = float2(1.0f, 0.0f)},
                {.position = float3(1.0f, 1.0f, 0.0f), .texCoord = float2(1.0f, 1.0f)},
                {.position = float3(-1.0f, 1.0f, 0.0f), .texCoord = float2(0.0f, 1.0f)},
            };
            indices = {0u, 1u, 2u, 0u, 2u, 3u};
        }

        /** Builds all nine deterministic patch destinations and their raw sRGB source bank. */
        uint32_t buildPartialupdatePatches(
            eastl::vector<WebglMaterialsTexturePartialupdatePatch> &patches,
            eastl::vector<uint8_t> &patchTextureBytes,
            uint32_t &initialRandomState)
        {
            constexpr uint32_t ChannelCount = 4u;
            ThreeCompat::DeterministicRandom random(PartialupdateRandomSeed);
            for (uint32_t drawIndex = 0u; drawIndex < PrePatchRandomDrawCount; ++drawIndex)
            {
                (void)random.nextUint32();
            }
            initialRandomState = random.getState();

            patches.clear();
            patches.reserve(PartialupdatePatchCount);
            patchTextureBytes.clear();
            patchTextureBytes.resize(
                static_cast<size_t>(PartialupdatePatchExtent) *
                PartialupdatePatchExtent * PartialupdatePatchCount * ChannelCount);
            for (uint32_t patchIndex = 0u; patchIndex < PartialupdatePatchCount; ++patchIndex)
            {
                const uint32_t destinationX =
                    32u * (uint32_t(std::floor(nextPartialupdateRandomDouble(random) * 16.0)) + 1u) - 32u;
                const uint32_t destinationY =
                    32u * (uint32_t(std::floor(nextPartialupdateRandomDouble(random) * 16.0)) + 1u) - 32u;
                const uint32_t hexadecimal = static_cast<uint32_t>(
                    std::floor(nextPartialupdateRandomDouble(random) * 16777215.0));
                const uint8_t red = makePartialupdateStoredColorByte((hexadecimal >> 16u) & 255u);
                const uint8_t green = makePartialupdateStoredColorByte((hexadecimal >> 8u) & 255u);
                const uint8_t blue = makePartialupdateStoredColorByte(hexadecimal & 255u);
                patches.push_back({
                    .destinationAndSourceRow = uint4(
                        destinationX,
                        destinationY,
                        patchIndex * PartialupdatePatchExtent,
                        0u),
                });

                const size_t patchPixelCount =
                    static_cast<size_t>(PartialupdatePatchExtent) * PartialupdatePatchExtent;
                const size_t patchByteOffset = patchPixelCount * patchIndex * ChannelCount;
                for (size_t pixelIndex = 0u; pixelIndex < patchPixelCount; ++pixelIndex)
                {
                    const size_t byteOffset = patchByteOffset + pixelIndex * ChannelCount;
                    patchTextureBytes[byteOffset] = red;
                    patchTextureBytes[byteOffset + 1u] = green;
                    patchTextureBytes[byteOffset + 2u] = blue;
                    patchTextureBytes[byteOffset + 3u] = 1u;
                }
            }
            return random.getState();
        }

        /** Builds Three's perspective matrix in the DSL backend's clip-space convention. */
        glm::mat4 makePartialupdateModelViewProjection(uint32_t width, uint32_t height)
        {
            constexpr double FieldOfViewDegrees = 70.0;
            constexpr double NearDistance = 0.01;
            constexpr double FarDistance = 10.0;
            constexpr double Pi = 3.14159265358979323846;
            const double top =
                NearDistance * std::tan(FieldOfViewDegrees * Pi / 360.0);
            const double projectionHeight = 2.0 * top;
            const double projectionWidth =
                (double(width) / double(height)) * projectionHeight;
            const double projectionDepth = FarDistance - NearDistance;

            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(
                2.0 * NearDistance / projectionWidth);
            projection[1u][1u] = static_cast<float>(
                -2.0 * NearDistance / projectionHeight);
            projection[2u][2u] = static_cast<float>(-FarDistance / projectionDepth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                -FarDistance * NearDistance / projectionDepth);

            glm::mat4 view(1.0f);
            view[3u][2u] = -2.0f;
            return projection * view;
        }
    } // namespace

    void WebglMaterialsTexturePartialupdateRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validatePartialupdateScenario(options);
        device = inDevice;
        buildPartialupdatePlane(vertices, indices);
        finalRandomState = buildPartialupdatePatches(
            patches,
            patchTextureBytes,
            initialRandomState);
        if (initialRandomState != ExpectedInitialRandomState ||
            finalRandomState != ExpectedFinalRandomState)
        {
            throw std::runtime_error(
                "webgl_materials_texture_partialupdate deterministic random stream diverged from r185.");
        }

        const std::filesystem::path texturePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "floors" / "FloorsCheckerboard_S_Diffuse.jpg";
        const eastl::vector<uint8_t> assetBytes =
            readPartialupdateAssetBytes(texturePath);
        if (calculatePartialupdateSha256(assetBytes) != CheckerboardAssetSha256)
        {
            throw std::invalid_argument(
                "FloorsCheckerboard_S_Diffuse.jpg differs from the pinned Three r185 asset.");
        }
        const RgbaImageData checkerboard = decodeJpegRgba8(texturePath);
        if (checkerboard.width != PartialupdateTextureExtent ||
            checkerboard.height != PartialupdateTextureExtent)
        {
            throw std::runtime_error(
                "FloorsCheckerboard_S_Diffuse.jpg must decode to 512x512 RGBA8.");
        }
        baseTextureBytes = checkerboard.pixels;
        patchCount = options.scenarioId == "patched" ? PartialupdatePatchCount : 0u;
        modelViewProjection = makePartialupdateModelViewProjection(
            options.width,
            options.height);
    }

    void WebglMaterialsTexturePartialupdateRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMaterialsTexturePartialupdateRuntimeAdapter::afterFrame(
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
        const uint64_t byteCount = computePartialupdateRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_materials_texture_partialupdate capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_materials_texture_partialupdate could not access the graphics queue.");
        }
        graphicsQueue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglMaterialsTexturePartialupdateRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglMaterialsTexturePartialupdateRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        preparePartialupdateOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_materials_texture_partialupdate RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_materials_texture_partialupdate RGBA capture.");
        }
    }

    void WebglMaterialsTexturePartialupdateRuntimeAdapter::writeCaptureMetadata(
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
        preparePartialupdateOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_materials_texture_partialupdate metadata path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_materials_texture_partialupdate\",\n"
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

    void WebglMaterialsTexturePartialupdateRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        preparePartialupdateOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_materials_texture_partialupdate snapshot path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_materials_texture_partialupdate\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"canonicalState\": \""
               << (patchCount == 0u
                       ? "checkerboard-before-patches-seed-42"
                       : "seed-42-fixed-step-60hz-nine-color-patches")
               << "\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"logicalScenePassCount\": 1,\n"
               << "  \"screenPassCount\": 2,\n"
               << "  \"computePassCount\": 2,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"logicalDrawCommandCount\": 1,\n"
               << "  \"explicitIndexCount\": 6,\n"
               << "  \"planeVertexCount\": 4,\n"
               << "  \"standaloneGeometryBufferCount\": 2,\n"
               << "  \"primitiveTopology\": \"triangle-list\",\n"
               << "  \"baseTextureFormat\": \"rgba8unorm-srgb\",\n"
               << "  \"workingTextureFormat\": \"rgba16float\",\n"
               << "  \"baseTextureWidth\": 512,\n"
               << "  \"baseTextureHeight\": 512,\n"
               << "  \"generateMipmaps\": false,\n"
               << "  \"patchExtent\": 32,\n"
               << "  \"patchCount\": " << patchCount << ",\n"
               << "  \"seed\": " << PartialupdateRandomSeed << ",\n"
               << "  \"prePatchRandomDrawCount\": " << PrePatchRandomDrawCount << ",\n"
               << "  \"initialRandomState\": " << initialRandomState << ",\n"
               << "  \"finalRandomState\": " << finalRandomState << ",\n"
               << "  \"texturePath\": \"textures/floors/FloorsCheckerboard_S_Diffuse.jpg\",\n"
               << "  \"textureAssetSha256\": \"" << CheckerboardAssetSha256 << "\",\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"sceneRoots\": [{\n"
               << "    \"id\": \"scene\",\n"
               << "    \"renderSetCount\": 0,\n"
               << "    \"renderSetId\": null,\n"
               << "    \"renderSetType\": null,\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"entityCount\": 0,\n"
               << "    \"entities\": [],\n"
               << "    \"drawCommandCount\": 1,\n"
               << "    \"logicalDrawCommandCount\": 1,\n"
               << "    \"directDrawFallback\": false,\n"
               << "    \"scenePasses\": [{\n"
               << "      \"name\": \"main\",\n"
               << "      \"renderClass\": \"WebglMaterialsTexturePartialupdateMainPass\",\n"
               << "      \"renderSetId\": null,\n"
               << "      \"renderSetBindingCount\": 0,\n"
               << "      \"drawMode\": \"explicit-indexed\",\n"
               << "      \"invocationCount\": 1,\n"
               << "      \"logicalInvocationCount\": 1,\n"
               << "      \"drawCommandCount\": 1,\n"
               << "      \"usesStandaloneGeometry\": true,\n"
               << "      \"usesExplicitDrawCount\": true\n"
               << "    }]\n"
               << "  }],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
