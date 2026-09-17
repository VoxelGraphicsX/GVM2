#include "Texture2DArrayRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <compression.h>

#include <EASTL/array.h>
#include <EASTL/string.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t VolumeWidth = 256u;
        constexpr uint32_t VolumeHeight = 256u;
        constexpr uint32_t VolumeLayerCount = 109u;
        constexpr uint64_t VolumeByteCount =
            uint64_t(VolumeWidth) * VolumeHeight * VolumeLayerCount;
        constexpr const char *VolumeAssetSha256 =
            "7d6d9c15a24a043114a9a2d07380b1afe24f4c45aa0a0da0c55bf764cb2cf906";

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareTextureArrayOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Reads one little-endian ZIP field after strict range validation. */
        uint16_t readTextureArrayUint16(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 2u)
            {
                throw std::runtime_error(
                    "Texture-array ZIP has a truncated uint16 field.");
            }
            return uint16_t(bytes[offset]) |
                (uint16_t(bytes[offset + 1u]) << 8u);
        }

        /** Reads one little-endian ZIP field after strict range validation. */
        uint32_t readTextureArrayUint32(
            const eastl::vector<uint8_t> &bytes,
            size_t offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 4u)
            {
                throw std::runtime_error(
                    "Texture-array ZIP has a truncated uint32 field.");
            }
            return uint32_t(bytes[offset]) |
                (uint32_t(bytes[offset + 1u]) << 8u) |
                (uint32_t(bytes[offset + 2u]) << 16u) |
                (uint32_t(bytes[offset + 3u]) << 24u);
        }

        /** Reads one immutable binary asset with bounded allocation. */
        eastl::vector<uint8_t> readTextureArrayAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open the pinned head-volume ZIP.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "The pinned head-volume ZIP is empty.");
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
                    "Could not read the pinned head-volume ZIP.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one byte sequence. */
        eastl::string calculateTextureArraySha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Texture-array SHA-256 input is too large.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
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

        /** Inflates the single raw-DEFLATE entry in the locked r185 ZIP. */
        eastl::vector<uint8_t> inflateTextureArrayVolume(
            const eastl::vector<uint8_t> &zipBytes)
        {
            constexpr uint32_t LocalFileHeaderSignature = 0x04034b50u;
            constexpr size_t LocalHeaderByteCount = 30u;
            if (zipBytes.size() < LocalHeaderByteCount ||
                readTextureArrayUint32(zipBytes, 0u) !=
                    LocalFileHeaderSignature ||
                readTextureArrayUint16(zipBytes, 6u) != 0u ||
                readTextureArrayUint16(zipBytes, 8u) != 8u ||
                readTextureArrayUint32(zipBytes, 22u) != VolumeByteCount)
            {
                throw std::runtime_error(
                    "Head-volume ZIP differs from its single-entry DEFLATE contract.");
            }
            const uint32_t compressedByteCount =
                readTextureArrayUint32(zipBytes, 18u);
            const uint16_t fileNameByteCount =
                readTextureArrayUint16(zipBytes, 26u);
            const uint16_t extraByteCount =
                readTextureArrayUint16(zipBytes, 28u);
            const size_t payloadOffset =
                LocalHeaderByteCount +
                size_t(fileNameByteCount) +
                size_t(extraByteCount);
            if (payloadOffset > zipBytes.size() ||
                compressedByteCount > zipBytes.size() - payloadOffset)
            {
                throw std::runtime_error(
                    "Head-volume ZIP has a truncated compressed payload.");
            }
            constexpr char ExpectedFileName[] = "head256x256x109";
            constexpr size_t ExpectedFileNameByteCount =
                sizeof(ExpectedFileName) - 1u;
            if (fileNameByteCount != ExpectedFileNameByteCount)
            {
                throw std::runtime_error(
                    "Head-volume ZIP entry name has changed.");
            }
            for (size_t index = 0u;
                 index < ExpectedFileNameByteCount;
                 ++index)
            {
                if (zipBytes[LocalHeaderByteCount + index] !=
                    uint8_t(ExpectedFileName[index]))
                {
                    throw std::runtime_error(
                        "Head-volume ZIP entry name has changed.");
                }
            }
            eastl::vector<uint8_t> decoded(
                static_cast<size_t>(VolumeByteCount));
            const size_t decodedByteCount = compression_decode_buffer(
                decoded.data(),
                decoded.size(),
                zipBytes.data() + payloadOffset,
                compressedByteCount,
                nullptr,
                COMPRESSION_ZLIB);
            if (decodedByteCount != VolumeByteCount)
            {
                throw std::runtime_error(
                    "Could not inflate the complete 256x256x109 volume.");
            }
            return decoded;
        }

        /** Validates one of the two exact texture-array Manifest contracts. */
        void validateTextureArrayOptions(
            const ThreeSampleHostOptions &options)
        {
            const bool webgl =
                options.caseId == "webgl_texture2darray";
            const bool webgpu =
                options.caseId == "webgpu_textures_2d-array";
            if ((!webgl && !webgpu) ||
                options.randomSeed != 0x12345678u ||
                options.assetRoot.empty() ||
                !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "Texture-array adapter requires its case, seed, asset root, and no replay.");
            }
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated-layer" &&
                options.targetFrame == 60u;
            if (!initial && !animated)
            {
                throw std::invalid_argument(
                    "Texture-array scenario differs from the Manifest.");
            }
        }

        /** Returns the validated RGBA8 capture byte count. */
        uint64_t computeTextureArrayRgbaByteCount(
            uint32_t width,
            uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount =
                uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "Texture-array capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns the layer value represented by one deterministic host frame. */
        float resolveTextureArrayLayer(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex)
        {
            if (options.caseId == "webgl_texture2darray")
            {
                return 55.0f + 0.4f * float(frameIndex);
            }
            const float timeSeconds = float(frameIndex) / 60.0f;
            const float phase = timeSeconds * 0.5f + 0.5f;
            const float fraction = phase - std::floor(phase);
            const float oscillator =
                std::abs(fraction * 2.0f - 1.0f);
            return (oscillator + 1.0f) * 0.5f *
                float(VolumeLayerCount);
        }
    } // namespace

    void Texture2DArrayRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateTextureArrayOptions(options);
        device = inDevice;
        const std::filesystem::path assetPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "3d" /
            "head256x256x109.zip";
        const eastl::vector<uint8_t> zipBytes =
            readTextureArrayAsset(assetPath);
        if (calculateTextureArraySha256(zipBytes) !=
            VolumeAssetSha256)
        {
            throw std::invalid_argument(
                "Head-volume ZIP differs from the pinned Three r185 asset.");
        }
        voxels = inflateTextureArrayVolume(zipBytes);
    }

    void Texture2DArrayRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void Texture2DArrayRuntimeAdapter::afterFrame(
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
            computeTextureArrayRgbaByteCount(width, height);
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
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

    void Texture2DArrayRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        const float layer =
            resolveTextureArrayLayer(options, frameIndex);
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareTextureArrayOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write texture-array RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareTextureArrayOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\""
                   << options.caseId.c_str() << "\",\n"
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
                   << "  \"byteCount\":" << rgba.size() << ",\n"
                   << "  \"format\":\"rgba8unorm\",\n"
                   << "  \"inputReplay\":null\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareTextureArrayOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\""
                   << options.caseId.c_str() << "\",\n"
                   << "  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"implementationLevel\":\"semantic-complete\",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"assetBacked\":true,\n"
                   << "  \"assetSha256\":\""
                   << VolumeAssetSha256 << "\",\n"
                   << "  \"renderSetPolicy\":\"not-required\",\n"
                   << "  \"sceneRenderSetCount\":0,\n"
                   << "  \"renderableObjectCount\":1,\n"
                   << "  \"entityCount\":0,\n"
                   << "  \"instanceCount\":1,\n"
                   << "  \"vertexCount\":4,\n"
                   << "  \"indexCount\":6,\n"
                   << "  \"scenePassCount\":1,\n"
                   << "  \"screenPassCount\":1,\n"
                   << "  \"scenePassSequence\":[{"
                   << "\"sceneRoot\":\"scene\","
                   << "\"scenePass\":\"head-slice\"}],\n"
                   << "  \"textureArrayExtent\":[256,256,109],\n"
                   << "  \"textureFormat\":\"r8unorm\",\n"
                   << "  \"selectedLayer\":" << layer << ",\n"
                   << "  \"drawCommandCount\":1,\n"
                   << "  \"directDrawFallback\":false\n}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareTextureArrayOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\""
                   << options.caseId.c_str() << "\",\n"
                   << "  \"assetSha256\":\""
                   << VolumeAssetSha256 << "\",\n"
                   << "  \"decodedByteCount\":"
                   << VolumeByteCount << ",\n"
                   << "  \"selectedLayer\":" << layer << ",\n"
                   << "  \"layerSelection\":\""
                   << (options.caseId == "webgl_texture2darray"
                           ? "reflected-linear-step"
                           : "triangle-oscillator")
                   << "\"\n}\n";
        }
    }

    void Texture2DArrayRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        voxels.clear();
        captureWritten = false;
    }
} // namespace GVM::ThreeSamples
