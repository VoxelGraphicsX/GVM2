#include "WebglBuffergeometryAttributesIntegerRuntimeAdapter.hpp"

#include "GifImageDecoder.hpp"
#include "TexturedBoxSampleData.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>

#include <cmath>
#include <cstddef>
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
        constexpr uint32_t AttributesIntegerRandomSeed = 0x18500001u;
        constexpr uint32_t AttributesIntegerTriangleCount = 10000u;
        constexpr uint32_t AttributesIntegerVertexCount = AttributesIntegerTriangleCount * 3u;
        constexpr uint32_t UpstreamModuleRandomDrawCount = 76u;
        constexpr uint32_t UpstreamObjectUuidRandomDrawCount = 12u;
        constexpr uint32_t UpstreamPreVertexRandomDrawCount =
            UpstreamModuleRandomDrawCount + UpstreamObjectUuidRandomDrawCount;
        constexpr uint32_t ExpectedFinalGeometryRandomState = 2464553194u;
        constexpr uint32_t ExpectedFinalPageRandomState = 3968022076u;
        constexpr uint32_t CrateExtent = 256u;
        constexpr uint32_t FloorExtent = 512u;
        constexpr uint32_t GrassExtent = 2048u;
        constexpr uint32_t CrateMipCount = 9u;
        constexpr uint32_t FloorMipCount = 10u;
        constexpr uint32_t GrassMipCount = 12u;
        constexpr double ReferenceEpochSeconds = 1700000000.0;
        constexpr const char *CrateAssetSha256 =
            "a890f0a89eadc083cb39bfbe597c1395d7acf47a19f673b5643d4a9c174ea52f";
        constexpr const char *FloorAssetSha256 =
            "d7547036c6221a840b80ce11138dc2c1a26df2630419217234c479b00e50a119";
        constexpr const char *GrassAssetSha256 =
            "23bd506b94e40a9de435375c2be6280eb088dc637fb64c331e325c7472f47177";

        static_assert(sizeof(WebglBuffergeometryAttributesIntegerVertex) == 24u);
        static_assert(offsetof(WebglBuffergeometryAttributesIntegerVertex, position) == 0u);
        static_assert(offsetof(WebglBuffergeometryAttributesIntegerVertex, texCoord) == 12u);
        static_assert(offsetof(WebglBuffergeometryAttributesIntegerVertex, textureIndex) == 20u);

        /** Creates parent directories for one explicitly requested integer-attribute artifact. */
        void prepareAttributesIntegerOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes tightly packed RGBA8 storage while rejecting integer overflow. */
        uint64_t computeAttributesIntegerRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_buffergeometry_attributes_integer RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Validates the two manifest-locked scenarios, seed, extent, and capture frames. */
        void validateAttributesIntegerScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_buffergeometry_attributes_integer")
            {
                throw std::invalid_argument(
                    "Integer-attribute adapter requires case-id webgl_buffergeometry_attributes_integer.");
            }
            const bool initial = options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 60u;
            if (!initial && !animated)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_attributes_integer requires initial/frame 0 or animated/frame 60.");
            }
            if (options.width != 800u || options.height != 500u)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_attributes_integer requires the locked 800x500 extent.");
            }
            if (options.randomSeed != AttributesIntegerRandomSeed)
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_attributes_integer requires random seed 0x18500001.");
            }
            if (options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "webgl_buffergeometry_attributes_integer requires explicit --asset-root.");
            }
        }

        /** Reads one bounded immutable asset into exact bytes for SHA-256 validation. */
        eastl::vector<uint8_t> readAttributesIntegerAssetBytes(
            const std::filesystem::path &inputPath,
            const char *label)
        {
            std::ifstream input(inputPath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open " + std::string(label) + ": " + inputPath.string());
            }
            const std::streamoff end = input.tellg();
            if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error(std::string(label) + " has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(end));
            input.read(reinterpret_cast<char *>(bytes.data()), end);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the complete " + std::string(label) + ".");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte sequence. */
        eastl::string calculateAttributesIntegerSha256(
            const eastl::vector<uint8_t> &bytes)
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

        /** Validates one asset against the locked SHA-256 identity. */
        void validateAttributesIntegerAsset(
            const std::filesystem::path &assetPath,
            const char *label,
            const char *expectedSha256)
        {
            const eastl::vector<uint8_t> bytes =
                readAttributesIntegerAssetBytes(assetPath, label);
            if (calculateAttributesIntegerSha256(bytes) != expectedSha256)
            {
                throw std::invalid_argument(
                    std::string(label) + " differs from the pinned Three r185 asset.");
            }
        }

        /** Reads one validated RGBA8 channel from a mip-chain source image. */
        uint8_t readAttributesIntegerImageChannel(
            const RgbaImageData &image,
            uint32_t x,
            uint32_t y,
            uint32_t channel)
        {
            constexpr uint32_t ChannelCount = 4u;
            const size_t offset =
                (static_cast<size_t>(y) * image.width + x) * ChannelCount + channel;
            return image.pixels[offset];
        }

        /** Builds one encoded-space 2x2 mip matching RGBA8 automatic mip filtering. */
        RgbaImageData buildNextAttributesIntegerUnormMip(const RgbaImageData &source)
        {
            constexpr uint32_t ChannelCount = 4u;
            RgbaImageData target;
            target.width = eastl::max(source.width / 2u, 1u);
            target.height = eastl::max(source.height / 2u, 1u);
            target.pixels.resize(
                static_cast<size_t>(target.width) * target.height * ChannelCount);
            for (uint32_t targetY = 0u; targetY < target.height; ++targetY)
            {
                for (uint32_t targetX = 0u; targetX < target.width; ++targetX)
                {
                    const uint32_t sourceX0 = eastl::min(targetX * 2u, source.width - 1u);
                    const uint32_t sourceY0 = eastl::min(targetY * 2u, source.height - 1u);
                    const uint32_t sourceX1 = eastl::min(sourceX0 + 1u, source.width - 1u);
                    const uint32_t sourceY1 = eastl::min(sourceY0 + 1u, source.height - 1u);
                    const size_t targetOffset =
                        (static_cast<size_t>(targetY) * target.width + targetX) * ChannelCount;
                    for (uint32_t channel = 0u; channel < ChannelCount; ++channel)
                    {
                        const uint32_t sum =
                            readAttributesIntegerImageChannel(
                                source, sourceX0, sourceY0, channel) +
                            readAttributesIntegerImageChannel(
                                source, sourceX1, sourceY0, channel) +
                            readAttributesIntegerImageChannel(
                                source, sourceX0, sourceY1, channel) +
                            readAttributesIntegerImageChannel(
                                source, sourceX1, sourceY1, channel);
                        target.pixels[targetOffset + channel] =
                            static_cast<uint8_t>((sum + 2u) / 4u);
                    }
                }
            }
            return target;
        }

        /** Builds the complete encoded-space RGBA8 mip chain for one authored texture. */
        eastl::vector<RgbaImageData> buildAttributesIntegerUnormMipChain(
            const RgbaImageData &baseImage)
        {
            constexpr uint32_t ChannelCount = 4u;
            const size_t expectedByteCount =
                static_cast<size_t>(baseImage.width) * baseImage.height * ChannelCount;
            if (baseImage.width == 0u || baseImage.height == 0u ||
                baseImage.pixels.size() != expectedByteCount)
            {
                throw std::invalid_argument(
                    "Integer-attribute mip generation requires tightly packed RGBA8 input.");
            }
            eastl::vector<RgbaImageData> levels;
            levels.push_back(baseImage);
            while (levels.back().width > 1u || levels.back().height > 1u)
            {
                levels.push_back(buildNextAttributesIntegerUnormMip(levels.back()));
            }
            return levels;
        }

        /** Copies one image mip chain into the generated renderer's byte-vector ABI. */
        eastl::vector<eastl::vector<uint8_t>> copyAttributesIntegerMipBytes(
            const eastl::vector<RgbaImageData> &levels)
        {
            eastl::vector<eastl::vector<uint8_t>> result;
            result.reserve(levels.size());
            for (const RgbaImageData &level : levels)
            {
                result.push_back(level.pixels);
            }
            return result;
        }

        /** Returns one exact JavaScript Math.random value as a binary64 number. */
        double nextAttributesIntegerRandomDouble(
            ThreeCompat::DeterministicRandom &random)
        {
            return double(random.nextUint32() >> 8u) / 16777216.0;
        }

        /** Builds the exact Float32BufferAttribute vertex stream and returns its final state. */
        uint32_t buildAttributesIntegerVertices(
            eastl::vector<WebglBuffergeometryAttributesIntegerVertex> &vertices)
        {
            ThreeCompat::DeterministicRandom random(AttributesIntegerRandomSeed);
            for (uint32_t drawIndex = 0u;
                 drawIndex < UpstreamPreVertexRandomDrawCount;
                 ++drawIndex)
            {
                (void)random.nextUint32();
            }

            vertices.clear();
            vertices.reserve(AttributesIntegerVertexCount);
            for (uint32_t triangleIndex = 0u;
                 triangleIndex < AttributesIntegerTriangleCount;
                 ++triangleIndex)
            {
                const double centerX = nextAttributesIntegerRandomDouble(random) * 800.0 - 400.0;
                const double centerY = nextAttributesIntegerRandomDouble(random) * 800.0 - 400.0;
                const double centerZ = nextAttributesIntegerRandomDouble(random) * 800.0 - 400.0;
                WebglBuffergeometryAttributesIntegerVertex triangle[3];
                for (uint32_t vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
                {
                    const double positionX =
                        centerX + nextAttributesIntegerRandomDouble(random) * 50.0 - 25.0;
                    const double positionY =
                        centerY + nextAttributesIntegerRandomDouble(random) * 50.0 - 25.0;
                    const double positionZ =
                        centerZ + nextAttributesIntegerRandomDouble(random) * 50.0 - 25.0;
                    triangle[vertexIndex].position = float3(
                        static_cast<float>(positionX),
                        static_cast<float>(positionY),
                        static_cast<float>(positionZ));
                    triangle[vertexIndex].textureIndex =
                        static_cast<int>(triangleIndex % 3u);
                }
                triangle[0].texCoord = float2(0.0f, 0.0f);
                triangle[1].texCoord = float2(0.5f, 1.0f);
                triangle[2].texCoord = float2(1.0f, 0.0f);
                vertices.push_back(triangle[0]);
                vertices.push_back(triangle[1]);
                vertices.push_back(triangle[2]);
            }
            return random.getState();
        }

        /** Builds the target frame's exact Date.now-based Three Euler transform. */
        glm::mat4 makeAttributesIntegerModelViewProjection(
            const ThreeSampleHostOptions &options,
            double &timeSeconds,
            double &rotationX,
            double &rotationY)
        {
            timeSeconds = ReferenceEpochSeconds + double(options.targetFrame) / 60.0;
            rotationX = timeSeconds * 0.25;
            rotationY = timeSeconds * 0.5;
            glm::mat4 view(1.0f);
            view[3u][2u] = -2500.0f;
            glm::mat4 projection = makeThreePerspectiveProjection(
                options.width,
                options.height,
                27.0,
                1.0,
                3500.0);
            projection[1u][1u] = -projection[1u][1u];
            return projection * view * makeThreeEulerXyRotation(rotationX, rotationY);
        }

        /** Decodes, validates, and mipmaps one GIF or JPEG asset. */
        eastl::vector<eastl::vector<uint8_t>> loadAttributesIntegerTexture(
            const std::filesystem::path &assetPath,
            const char *label,
            const char *expectedSha256,
            uint32_t expectedExtent,
            uint32_t expectedMipCount,
            bool gif,
            uint32_t &width,
            uint32_t &height)
        {
            validateAttributesIntegerAsset(assetPath, label, expectedSha256);
            const RgbaImageData decoded = gif
                ? decodeGifRgba8(assetPath)
                : decodeJpegRgba8(assetPath);
            if (decoded.width != expectedExtent || decoded.height != expectedExtent)
            {
                throw std::runtime_error(
                    std::string(label) + " has an unexpected decoded extent.");
            }
            const eastl::vector<RgbaImageData> mipChain =
                buildAttributesIntegerUnormMipChain(decoded);
            if (mipChain.size() != expectedMipCount)
            {
                throw std::runtime_error(
                    std::string(label) + " has an unexpected mip count.");
            }
            width = decoded.width;
            height = decoded.height;
            return copyAttributesIntegerMipBytes(mipChain);
        }
    } // namespace

    void WebglBuffergeometryAttributesIntegerRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateAttributesIntegerScenario(options);
        device = inDevice;
        finalGeometryRandomState = buildAttributesIntegerVertices(vertices);
        if (finalGeometryRandomState != ExpectedFinalGeometryRandomState)
        {
            throw std::runtime_error(
                "webgl_buffergeometry_attributes_integer generated an unexpected random stream.");
        }
        modelViewProjection = makeAttributesIntegerModelViewProjection(
            options,
            timeSeconds,
            rotationX,
            rotationY);

        const std::filesystem::path assetRoot(options.assetRoot.c_str());
        crateMips = loadAttributesIntegerTexture(
            assetRoot / "textures" / "crate.gif",
            "textures/crate.gif",
            CrateAssetSha256,
            CrateExtent,
            CrateMipCount,
            true,
            crateWidth,
            crateHeight);
        floorMips = loadAttributesIntegerTexture(
            assetRoot / "textures" / "floors" / "FloorsCheckerboard_S_Diffuse.jpg",
            "textures/floors/FloorsCheckerboard_S_Diffuse.jpg",
            FloorAssetSha256,
            FloorExtent,
            FloorMipCount,
            false,
            floorWidth,
            floorHeight);
        grassMips = loadAttributesIntegerTexture(
            assetRoot / "textures" / "terrain" / "grasslight-big.jpg",
            "textures/terrain/grasslight-big.jpg",
            GrassAssetSha256,
            GrassExtent,
            GrassMipCount,
            false,
            grassWidth,
            grassHeight);
    }

    void WebglBuffergeometryAttributesIntegerRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglBuffergeometryAttributesIntegerRuntimeAdapter::afterFrame(
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
            computeAttributesIntegerRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgl_buffergeometry_attributes_integer capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "webgl_buffergeometry_attributes_integer could not access the graphics queue.");
        }
        graphicsQueue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglBuffergeometryAttributesIntegerRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglBuffergeometryAttributesIntegerRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareAttributesIntegerOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_attributes_integer RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Could not write complete webgl_buffergeometry_attributes_integer RGBA capture.");
        }
    }

    void WebglBuffergeometryAttributesIntegerRuntimeAdapter::writeCaptureMetadata(
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
        prepareAttributesIntegerOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_attributes_integer metadata path.");
        }
        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_buffergeometry_attributes_integer\",\n"
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

    void WebglBuffergeometryAttributesIntegerRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareAttributesIntegerOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open webgl_buffergeometry_attributes_integer snapshot path.");
        }
        output << std::fixed << std::setprecision(8)
               << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_buffergeometry_attributes_integer\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"canonicalState\": \""
               << (frameIndex == 0u
                       ? "seed-0x18500001-time-zero-three-texture-selection"
                       : "seed-0x18500001-fixed-step-60hz-one-second")
               << "\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"logicalScenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"logicalDrawCommandCount\": 1,\n"
               << "  \"computePassCount\": 0,\n"
               << "  \"rasterSampleCount\": 1,\n"
               << "  \"explicitVertexCount\": 30000,\n"
               << "  \"submittedVertexCount\": 30000,\n"
               << "  \"vertexStrideBytes\": 24,\n"
               << "  \"standaloneGeometryBufferCount\": 1,\n"
               << "  \"primitiveTopology\": \"triangle-list\",\n"
               << "  \"sourceIntegerAttributeType\": \"int16\",\n"
               << "  \"gpuIntegerAttributeType\": \"sint32\",\n"
               << "  \"integerVaryingInterpolation\": \"flat\",\n"
               << "  \"textureCount\": 3,\n"
               << "  \"textureMipCounts\": [9, 10, 12],\n"
               << "  \"seed\": " << AttributesIntegerRandomSeed << ",\n"
               << "  \"upstreamPreVertexRandomDrawCount\": "
               << UpstreamPreVertexRandomDrawCount << ",\n"
               << "  \"finalGeometryRandomState\": "
               << finalGeometryRandomState << ",\n"
               << "  \"finalPageRandomState\": "
               << ExpectedFinalPageRandomState << ",\n"
               << "  \"timeSeconds\": " << timeSeconds << ",\n"
               << "  \"rotationX\": " << rotationX << ",\n"
               << "  \"rotationY\": " << rotationY << ",\n"
               << "  \"cameraFovDegrees\": 27,\n"
               << "  \"cameraNear\": 1,\n"
               << "  \"cameraFar\": 3500,\n"
               << "  \"cameraPositionZ\": 2500,\n"
               << "  \"assetSha256\": {\n"
               << "    \"textures/crate.gif\": \"" << CrateAssetSha256 << "\",\n"
               << "    \"textures/floors/FloorsCheckerboard_S_Diffuse.jpg\": \""
               << FloorAssetSha256 << "\",\n"
               << "    \"textures/terrain/grasslight-big.jpg\": \""
               << GrassAssetSha256 << "\"\n"
               << "  },\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main-textured\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"sceneRoots\": [{\n"
               << "    \"id\": \"scene\",\n"
               << "    \"renderSetCount\": 0,\n"
               << "    \"renderSetId\": null,\n"
               << "    \"renderSetType\": null,\n"
               << "    \"renderableObjectCount\": 1,\n"
               << "    \"entityCount\": 0,\n"
               << "    \"entities\": [],\n"
               << "    \"drawCommandCount\": 4,\n"
               << "    \"logicalDrawCommandCount\": 1,\n"
               << "    \"directDrawFallback\": false,\n"
               << "    \"scenePasses\": [{\n"
               << "      \"name\": \"main-textured\",\n"
               << "      \"renderClass\": \"WebglBuffergeometryAttributesIntegerMainPass\",\n"
               << "      \"renderSetId\": null,\n"
               << "      \"renderSetBindingCount\": 0,\n"
               << "      \"drawMode\": \"explicit-nonindexed\",\n"
               << "      \"invocationCount\": 4,\n"
               << "      \"logicalInvocationCount\": 1,\n"
               << "      \"drawCommandCount\": 4,\n"
               << "      \"usesStandaloneGeometry\": true,\n"
               << "      \"usesExplicitDrawCount\": true\n"
               << "    }]\n"
               << "  }],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
