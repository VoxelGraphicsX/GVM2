#include "WebgpuTexturegradRuntimeAdapter.hpp"

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
        constexpr uint32_t TextureExtent = 1024u;
        constexpr uint32_t TextureMipCount = 11u;
        constexpr const char *TextureSha256 =
            "909d9a1eb2a5d5de9d221a5e8de4e9119d409decddf522d48896bd51523d354d";

        /** Creates parent directories for one texture-gradient artifact. */
        void prepareTexturegradOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Reads one immutable texture file into bounded bytes. */
        eastl::vector<uint8_t> readTexturegradAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open the pinned texture-gradient asset.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "The pinned texture-gradient asset is empty.");
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
                    "Could not read the pinned texture-gradient asset.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded input. */
        eastl::string calculateTexturegradSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(
                    std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Texture-gradient SHA-256 input is too large.");
            }
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH>
                digest = {};
            CC_SHA256(
                bytes.data(),
                static_cast<CC_LONG>(bytes.size()),
                digest.data());
            constexpr char HexDigits[] =
                "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (const uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 0x0fu]);
            }
            return result;
        }

        /** Returns validated tightly packed RGBA8 storage size. */
        uint64_t computeTexturegradRgbaByteCount(
            uint32_t width,
            uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount =
                uint64_t(width) * uint64_t(height);
            if (pixelCount >
                std::numeric_limits<uint64_t>::max() /
                    BytesPerPixel)
            {
                throw std::overflow_error(
                    "Texture-gradient capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebgpuTexturegradRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgpu_texturegrad" ||
            options.randomSeed != 0x12345678u ||
            options.assetRoot.empty() ||
            options.width != 800u ||
            options.height != 500u)
        {
            throw std::invalid_argument(
                "Texture-gradient adapter requires its locked case, seed, extent, and assets.");
        }
        const bool initial =
            options.scenarioId == "initial-comparison" &&
            options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated-comparison" &&
            options.targetFrame == 60u;
        if ((!initial && !animated) ||
            !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Texture-gradient scenario differs from the Manifest.");
        }
        const std::filesystem::path texturePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "uv_grid_opengl.jpg";
        const eastl::vector<uint8_t> encoded =
            readTexturegradAsset(texturePath);
        if (calculateTexturegradSha256(encoded) != TextureSha256)
        {
            throw std::invalid_argument(
                "UV grid JPEG differs from the pinned Three r185 asset.");
        }
        const RgbaImageData texture =
            decodeJpegRgba8(texturePath);
        if (texture.width != TextureExtent ||
            texture.height != TextureExtent)
        {
            throw std::runtime_error(
                "UV grid JPEG decoded to an unexpected extent.");
        }
        const eastl::vector<RgbaImageData> mipChain =
            buildSrgbMipChain(texture);
        if (mipChain.size() != TextureMipCount)
        {
            throw std::runtime_error(
                "UV grid JPEG did not produce eleven explicit mips.");
        }
        textureWidth = texture.width;
        textureHeight = texture.height;
        mipPixels.clear();
        mipPixels.reserve(mipChain.size());
        for (const RgbaImageData &mip : mipChain)
        {
            mipPixels.push_back(mip.pixels);
        }
        device = inDevice;
    }

    void WebgpuTexturegradRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuTexturegradRuntimeAdapter::afterFrame(
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
            computeTexturegradRgbaByteCount(width, height);
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

    void WebgpuTexturegradRuntimeAdapter::writeArtifacts(
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
            prepareTexturegradOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write texture-gradient RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareTexturegradOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgpu_texturegrad\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n  \"pipeline\":\""
                << options.pipeline.c_str()
                << "\",\n  \"backend\":\""
                << threeSampleBackendName(options.backend)
                << "\",\n  \"frame\":" << frameIndex
                << ",\n  \"randomSeed\":" << options.randomSeed
                << ",\n  \"width\":" << width
                << ",\n  \"height\":" << height
                << ",\n  \"rowStrideBytes\":"
                << uint64_t(width) * 4u
                << ",\n  \"byteCount\":" << rgba.size()
                << ",\n  \"format\":\"rgba8unorm\",\n"
                << "  \"inputReplay\":null\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareTexturegradOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgpu_texturegrad\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n  \"frame\":" << frameIndex
                << ",\n  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"not-required\",\n"
                << "  \"sceneRenderSetCount\":0,\n"
                << "  \"sceneRootCount\":2,\n"
                << "  \"renderableObjectCount\":2,\n"
                << "  \"entityCount\":0,\n"
                << "  \"instanceCount\":1,\n"
                << "  \"drawCommandCount\":2,\n"
                << "  \"scenePassCount\":2,\n"
                << "  \"scenePassSequence\":["
                << "{\"sceneRoot\":\"webgpu-scene\","
                << "\"scenePass\":\"webgpu-grad\","
                << "\"entityOrdinal\":0},"
                << "{\"sceneRoot\":\"webgl-scene\","
                << "\"scenePass\":\"webgl-grad\","
                << "\"entityOrdinal\":0}],\n"
                << "  \"textureExtent\":[1024,1024],\n"
                << "  \"textureMipCount\":11,\n"
                << "  \"timeSeconds\":"
                << float(frameIndex) / 60.0f << "\n}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareTexturegradOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgpu_texturegrad\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n  \"frame\":" << frameIndex
                << ",\n  \"kind\":\"asset-snapshot\",\n"
                << "  \"canonicalState\":\"uv-grid-jpeg-sha256-"
                << TextureSha256 << "\",\n"
                << "  \"result\":{"
                << "\"assetSha256\":\"" << TextureSha256 << "\","
                << "\"decodedExtent\":[1024,1024],"
                << "\"explicitMipCount\":11,"
                << "\"renderableObjectCount\":2,"
                << "\"sceneRootCount\":2,"
                << "\"canonicalSceneSha256\":"
                << "\"71db9c6bf7aca2878e00f1523a2a197561cf3b11cc80becda64de34e25817dd9\""
                << "}\n}\n";
        }
    }

    void WebgpuTexturegradRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        mipPixels.clear();
    }
} // namespace GVM::ThreeSamples
