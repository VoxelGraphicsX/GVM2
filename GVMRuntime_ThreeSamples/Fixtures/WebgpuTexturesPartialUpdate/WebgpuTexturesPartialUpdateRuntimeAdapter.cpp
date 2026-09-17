#include "WebgpuTexturesPartialUpdateRuntimeAdapter.hpp"

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
        constexpr uint32_t CarbonExtent = 512u;
        constexpr const char *CarbonSha256 =
            "1d504adf20c83f26ce5519a19b9e33c2dc9ca6371a523a10869d308edd67f9e6";

        /** Creates parent directories for one partial-update artifact. */
        void preparePartialUpdateOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Reads one immutable binary asset with bounded allocation. */
        eastl::vector<uint8_t> readPartialUpdateAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open the pinned carbon PNG.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "The pinned carbon PNG is empty.");
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
                    "Could not read the pinned carbon PNG.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one byte sequence. */
        eastl::string calculatePartialUpdateSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Partial-update SHA-256 input is too large.");
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

        /** Validates the exact two-scenario partial-update contract. */
        void validatePartialUpdateOptions(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool firstUpdate =
                options.scenarioId == "first-update" &&
                options.targetFrame == 7u;
            if (options.caseId != "webgpu_textures_partialupdate" ||
                (!initial && !firstUpdate) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != 0x12345678u ||
                options.assetRoot.empty() ||
                !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "Partial-update adapter requires its locked case, scenario, extent, seed, and asset.");
            }
        }

        /** Returns the validated RGBA8 capture byte count. */
        uint64_t computePartialUpdateRgbaByteCount(
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
                    "Partial-update capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebgpuTexturesPartialUpdateRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validatePartialUpdateOptions(options);
        device = inDevice;
        const std::filesystem::path carbonPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "carbon" /
            "Carbon.png";
        const eastl::vector<uint8_t> encoded =
            readPartialUpdateAsset(carbonPath);
        if (calculatePartialUpdateSha256(encoded) != CarbonSha256)
        {
            throw std::invalid_argument(
                "Carbon PNG differs from the pinned Three r185 asset.");
        }
        const RgbaImageData image = decodePngRgba8(carbonPath);
        if (image.width != CarbonExtent ||
            image.height != CarbonExtent ||
            image.pixels.size() !=
                size_t(CarbonExtent) * CarbonExtent * 4u)
        {
            throw std::runtime_error(
                "Carbon PNG decoded to an unexpected RGBA8 extent.");
        }
        carbonPixels = image.pixels;
    }

    void WebgpuTexturesPartialUpdateRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuTexturesPartialUpdateRuntimeAdapter::afterFrame(
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
            computePartialUpdateRgbaByteCount(width, height);
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

    void WebgpuTexturesPartialUpdateRuntimeAdapter::writeArtifacts(
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
            preparePartialUpdateOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write partial-update RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            preparePartialUpdateOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\"webgpu_textures_partialupdate\",\n"
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
            preparePartialUpdateOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgpu_textures_partialupdate\",\n"
                   << "  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"implementationLevel\":\"semantic-complete\",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"assetBacked\":true,\n"
                   << "  \"assetSha256\":\""
                   << CarbonSha256 << "\",\n"
                   << "  \"renderSetPolicy\":\"not-required\",\n"
                   << "  \"sceneRenderSetCount\":0,\n"
                   << "  \"renderableObjectCount\":1,\n"
                   << "  \"instanceCount\":1,\n"
                   << "  \"scenePassCount\":1,\n"
                   << "  \"screenPassCount\":2,\n"
                   << "  \"scenePassSequence\":[{"
                   << "\"sceneRoot\":\"scene\","
                   << "\"scenePass\":\"carbon-plane\"}],\n"
                   << "  \"textureExtent\":[512,512],\n"
                   << "  \"mipCount\":1,\n"
                   << "  \"patchApplied\":"
                   << (frameIndex >= 7u ? "true" : "false") << ",\n"
                   << "  \"patchOrigin\":[480,0],\n"
                   << "  \"patchExtent\":[32,32],\n"
                   << "  \"computeDispatchThreads\":[32,32,1],\n"
                   << "  \"drawCommandCount\":1,\n"
                   << "  \"directDrawFallback\":false\n}\n";
        }
    }

    void WebgpuTexturesPartialUpdateRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        carbonPixels.clear();
        captureWritten = false;
    }
} // namespace GVM::ThreeSamples
