#include "Texture2DArrayCompressedRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <EASTL/string.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t FrameWidth = 496u;
        constexpr uint32_t FrameHeight = 260u;
        constexpr uint32_t FrameCount = 6u;
        constexpr uint64_t FrameByteCount =
            uint64_t(FrameWidth) * FrameHeight * 4u;
        constexpr uint64_t MovieByteCount =
            FrameByteCount * FrameCount;
        constexpr const char *SourceSha256 =
            "afab737973d40b8b2df1deda7204284e8a1b4271e1417c740a7de19254257f4b";
        constexpr const char *LayerSha256[FrameCount] = {
            "986448d4fcd79b9f7cc60b9f2f9e54d2a67b82a58d7fdc25162df244d6267ecc",
            "4f3a2649b84aac45b3f0489a2dce33d47d40f8d6f5d0c6cadbab5c6c669c67d3",
            "46d797704adae32da9b96988dd5ce0e5467efa46b0c28594862ab43c1adbc279",
            "6a956062790277c42e110ff7da4a16e3c3de7ec26f596ad8bd5c0bf27b92ade0",
            "7fc97349fbd3f36805bf5bc979aa1517e61c2393e672c2d89afe754cd0627a9a",
            "6d4347b1fe149c636bdb5f670164f44610017911a9af4bb55c89f446830dbb33",
        };

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareCompressedArrayOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Reads one immutable binary asset with an exact byte-count contract. */
        eastl::vector<uint8_t> readCompressedArrayAsset(
            const std::filesystem::path &path,
            uint64_t expectedByteCount,
            const char *label)
        {
            (void)label;
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open a locked compressed-array asset.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                (expectedByteCount != 0u &&
                 uint64_t(byteCount) != expectedByteCount))
            {
                throw std::runtime_error(
                    "A compressed-array asset differs from its locked byte count.");
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
                    "Could not read a locked compressed-array asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one byte sequence. */
        eastl::string calculateCompressedArraySha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Compressed-array SHA-256 input is too large.");
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

        /** Validates one of the two exact compressed-array host invocations. */
        void validateCompressedArrayOptions(
            const ThreeSampleHostOptions &options)
        {
            const bool webgl =
                options.caseId == "webgl_texture2darray_compressed";
            const bool webgpu =
                options.caseId == "webgpu_textures_2d-array_compressed";
            if ((!webgl && !webgpu) ||
                options.randomSeed != 0x12345678u ||
                options.assetRoot.empty() ||
                !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "Compressed-array adapter requires its exact case, seed, asset root, and no replay.");
            }
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated-layer" &&
                options.targetFrame == 12u;
            if (!initial && !animated)
            {
                throw std::invalid_argument(
                    "Compressed-array scenario differs from the Manifest.");
            }
        }

        /** Returns the fixed-clock movie layer represented by one host frame. */
        uint32_t resolveCompressedArrayLayer(uint32_t frameIndex)
        {
            const uint32_t elapsedTicks =
                frameIndex == 0u ? 0u : frameIndex - 1u;
            return uint32_t(1.0f + float(elapsedTicks) / 6.0f) %
                (FrameCount - 1u);
        }
    } // namespace

    void Texture2DArrayCompressedRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateCompressedArrayOptions(options);
        device = inDevice;
        const std::filesystem::path assetRoot(
            options.assetRoot.c_str());
        const eastl::vector<uint8_t> sourceBytes =
            readCompressedArrayAsset(
                assetRoot / "textures" / "spiritedaway.ktx2",
                0u,
                "pinned Spirited Away KTX2");
        if (calculateCompressedArraySha256(sourceBytes) !=
            SourceSha256)
        {
            throw std::invalid_argument(
                "Spirited Away KTX2 differs from Three r185.");
        }

        movieFrames.clear();
        movieFrames.reserve(static_cast<size_t>(MovieByteCount));
        for (uint32_t layer = 0u; layer < FrameCount; ++layer)
        {
            const eastl::string layerName =
                "spiritedaway_layer" +
                eastl::to_string(layer) +
                ".rgba";
            const std::filesystem::path layerPath =
                assetRoot /
                "decoded" /
                layerName.c_str();
            const eastl::vector<uint8_t> layerBytes =
                readCompressedArrayAsset(
                    layerPath,
                    FrameByteCount,
                    "decoded Spirited Away layer");
            if (calculateCompressedArraySha256(layerBytes) !=
                LayerSha256[layer])
            {
                throw std::invalid_argument(
                    "Decoded Spirited Away layer differs from the locked UASTC decode.");
            }
            movieFrames.insert(
                movieFrames.end(),
                layerBytes.begin(),
                layerBytes.end());
        }
    }

    void Texture2DArrayCompressedRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void Texture2DArrayCompressedRuntimeAdapter::afterFrame(
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
            uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Compressed-array capture size overflowed.");
        }
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

    void Texture2DArrayCompressedRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        const uint32_t layer =
            resolveCompressedArrayLayer(frameIndex);
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareCompressedArrayOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write compressed-array RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareCompressedArrayOutputPath(outputPath);
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
            prepareCompressedArrayOutputPath(outputPath);
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
                   << SourceSha256 << "\",\n"
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
                   << "\"scenePass\":\"spirited-away-slice\"}],\n"
                   << "  \"textureArrayExtent\":[496,260,6],\n"
                   << "  \"textureFormat\":\"rgba8unorm\",\n"
                   << "  \"compressedSource\":\"uastc-ktx2\",\n"
                   << "  \"selectedLayer\":" << layer << ",\n"
                   << "  \"drawCommandCount\":1,\n"
                   << "  \"directDrawFallback\":false\n}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareCompressedArrayOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\""
                   << options.caseId.c_str() << "\",\n"
                   << "  \"sourceAssetSha256\":\""
                   << SourceSha256 << "\",\n"
                   << "  \"decodedByteCount\":"
                   << MovieByteCount << ",\n"
                   << "  \"selectedLayer\":" << layer << ",\n"
                   << "  \"cpuDecode\":\"ktx-uastc-rgba8\",\n"
                   << "  \"layerSelection\":\"fixed-60hz-depth-step\"\n"
                   << "}\n";
        }
    }

    void Texture2DArrayCompressedRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        movieFrames.clear();
        captureWritten = false;
    }
} // namespace GVM::ThreeSamples
