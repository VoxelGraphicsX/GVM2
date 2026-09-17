#include "TextureAnisotropyRuntimeAdapter.hpp"

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
        constexpr uint32_t CrateExtent = 256u;
        constexpr uint32_t CrateMipCount = 9u;
        constexpr const char *CrateSha256 =
            "a890f0a89eadc083cb39bfbe597c1395d7acf47a19f673b5643d4a9c174ea52f";
        constexpr const char *WebglReplaySha256 =
            "0203538cfefd3a1061d277f17b1117e7b34b04348495ccfa78e59551e2b799e7";
        constexpr const char *WebgpuReplaySha256 =
            "cfd8c91416741069adf21b0e359034c4f83f7264234d2a64f001cb7ebb809b8a";

        /** Creates parent directories for one anisotropy gate artifact. */
        void prepareAnisotropyOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Reads one immutable asset or replay file into bounded bytes. */
        eastl::vector<uint8_t> readAnisotropyInput(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open a pinned anisotropy input.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "A pinned anisotropy input is empty.");
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
                    "Could not read a pinned anisotropy input.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded input. */
        eastl::string calculateAnisotropySha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(
                    std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Anisotropy SHA-256 input is too large.");
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
        uint64_t computeAnisotropyRgbaByteCount(
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
                    "Anisotropy capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns the expected replay identity for one supported case. */
        const char *anisotropyReplaySha256(
            const eastl::string &caseId)
        {
            return caseId ==
                       "webgl_materials_texture_anisotropy"
                       ? WebglReplaySha256
                       : WebgpuReplaySha256;
        }
    } // namespace

    void TextureAnisotropyRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool webgl =
            options.caseId ==
            "webgl_materials_texture_anisotropy";
        const bool webgpu =
            options.caseId ==
            "webgpu_textures_anisotropy";
        if ((!webgl && !webgpu) ||
            options.randomSeed != 0x12345678u ||
            options.assetRoot.empty() ||
            options.width != 800u ||
            options.height != 500u)
        {
            throw std::invalid_argument(
                "Anisotropy adapter requires a locked case, seed, extent, and asset root.");
        }

        const bool webglInitial =
            webgl &&
            options.scenarioId == "initial-loader" &&
            options.targetFrame == 0u;
        const bool webglEased =
            webgl &&
            options.scenarioId == "camera-eased" &&
            options.targetFrame == 60u;
        const bool webglMouse =
            webgl &&
            options.scenarioId == "mouse-camera" &&
            options.targetFrame == 61u;
        const bool webgpuInitial =
            webgpu &&
            options.scenarioId == "initial-comparison" &&
            options.targetFrame == 0u;
        const bool webgpuPointer =
            webgpu &&
            options.scenarioId == "grazing-pointer-view" &&
            options.targetFrame == 1u;
        if (!webglInitial && !webglEased && !webglMouse &&
            !webgpuInitial && !webgpuPointer)
        {
            throw std::invalid_argument(
                "Anisotropy scenario differs from the Manifest.");
        }

        hasReplay = webglMouse || webgpuPointer;
        clearGapToBackground = webgpu || options.targetFrame > 0u;
        if (hasReplay)
        {
            if (options.inputReplayPath.empty() ||
                calculateAnisotropySha256(
                    readAnisotropyInput(std::filesystem::path(
                        options.inputReplayPath.c_str()))) !=
                    anisotropyReplaySha256(options.caseId))
            {
                throw std::invalid_argument(
                    "Anisotropy replay differs from its locked identity.");
            }
            if (webglMouse)
            {
                targetMouseX = 220.0f;
                targetMouseY = 130.0f;
            }
            else
            {
                targetMouseX = 200.0f;
                targetMouseY = 110.0f;
            }
        }
        else if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Only anisotropy input scenarios accept replay input.");
        }

        const std::filesystem::path cratePath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "crate.gif";
        const eastl::vector<uint8_t> encoded =
            readAnisotropyInput(cratePath);
        if (calculateAnisotropySha256(encoded) !=
            CrateSha256)
        {
            throw std::invalid_argument(
                "Crate GIF differs from the pinned Three r185 asset.");
        }
        const RgbaImageData crate = decodeGifRgba8(cratePath);
        if (crate.width != CrateExtent ||
            crate.height != CrateExtent)
        {
            throw std::runtime_error(
                "Crate GIF decoded to an unexpected extent.");
        }
        const eastl::vector<RgbaImageData> mipChain =
            buildSrgbMipChain(crate);
        if (mipChain.size() != CrateMipCount)
        {
            throw std::runtime_error(
                "Crate GIF did not produce nine explicit mips.");
        }
        textureWidth = crate.width;
        textureHeight = crate.height;
        mipPixels.clear();
        mipPixels.reserve(mipChain.size());
        for (const RgbaImageData &mip : mipChain)
        {
            mipPixels.push_back(mip.pixels);
        }
        device = inDevice;
    }

    void TextureAnisotropyRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void TextureAnisotropyRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten ||
            frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            computeAnisotropyRgbaByteCount(width, height);
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

    void TextureAnisotropyRuntimeAdapter::writeArtifacts(
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
            prepareAnisotropyOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write anisotropy RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareAnisotropyOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"" << options.caseId.c_str()
                << "\",\n  \"scenarioId\":\""
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
                << "  \"inputReplay\":";
            if (hasReplay)
            {
                output
                    << "{\"schemaVersion\":1,"
                    << "\"caseId\":\"" << options.caseId.c_str()
                    << "\",\"scenarioId\":\""
                    << options.scenarioId.c_str()
                    << "\",\"captureFrame\":" << frameIndex
                    << ",\"sha256\":\""
                    << anisotropyReplaySha256(options.caseId)
                    << "\",\"target\":\""
                    << (options.caseId ==
                                "webgl_materials_texture_anisotropy"
                            ? "canvas[style*=\\\"position: relative\\\"]"
                            : "canvas:not([class])")
                    << "\","
                    << "\"eventCount\":1,\"lastEventFrame\":0}";
            }
            else
            {
                output << "null";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const bool webgl =
                options.caseId ==
                "webgl_materials_texture_anisotropy";
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareAnisotropyOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"" << options.caseId.c_str()
                << "\",\n  \"scenarioId\":\""
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
                << "{\"sceneRoot\":\""
                << (webgl ? "scene1" : "sceneLeft")
                << "\",\"scenePass\":\""
                << (webgl
                        ? "left-max-anisotropy"
                        : "left-max-anisotropy")
                << "\",\"entityOrdinal\":0},{\"sceneRoot\":\""
                << (webgl ? "scene2" : "sceneRight")
                << "\",\"scenePass\":\""
                << (webgl
                        ? "right-anisotropy-one"
                        : "right-one-anisotropy")
                << "\",\"entityOrdinal\":0}],\n"
                << "  \"textureExtent\":[256,256],\n"
                << "  \"textureMipCount\":9,\n"
                << "  \"samplerAnisotropy\":[16,1],\n"
                << "  \"cameraTargetMouse\":["
                << targetMouseX << "," << targetMouseY << "]\n"
                << "}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareAnisotropyOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"" << options.caseId.c_str()
                << "\",\n  \"scenarioId\":\""
                << options.scenarioId.c_str()
                << "\",\n  \"frame\":" << frameIndex
                << ",\n  \"kind\":\"asset-snapshot\",\n"
                << "  \"canonicalState\":\"crate-gif-sha256-"
                << CrateSha256 << "\",\n"
                << "  \"result\":{"
                << "\"assetSha256\":\"" << CrateSha256 << "\","
                << "\"decodedExtent\":[256,256],"
                << "\"explicitMipCount\":9,"
                << "\"renderableObjectCount\":2,"
                << "\"sceneRootCount\":2,"
                << "\"canonicalSceneSha256\":"
                << "\"87f97f2adcc4cc2c4ca03cfe54453072e6ac046aa60f16367b095e44e471df09\""
                << "}\n}\n";
        }
    }

    void TextureAnisotropyRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        mipPixels.clear();
    }
} // namespace GVM::ThreeSamples
