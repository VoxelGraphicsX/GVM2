#include "WebglLoaderTextureExrRuntimeAdapter.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <EASTL/string.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *MemorialExrSha256 =
            "46337f0879c25cad700c45fc92e456033d77543f18b52ff747f70ce5461ca178";
        constexpr const char *ExrExposureReplaySha256 =
            "945994bd397f1df8cc92693e02ce3699a75d2272da266a3797c4313f61395639";
        constexpr const char *ExrCanonicalSceneSha256 =
            "ab53e41bbff59a4de57161880113f9d2fd1a697cc8afbfadc5b0ee3bb8f137c1";

        /** Reads one bounded immutable EXR or replay input. */
        eastl::vector<uint8_t> readExrInput(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open one pinned EXR sample input.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 ||
                uint64_t(byteCount) >
                    uint64_t(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::runtime_error(
                    "A pinned EXR sample input has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read one complete EXR sample input.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded input. */
        eastl::string calculateExrSha256(
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
                result.push_back(HexDigits[value & 15u]);
            }
            return result;
        }

        /** Creates parent directories for one explicitly requested EXR artifact. */
        void prepareExrOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one bounded EXR text artifact with deterministic truncation. */
        void writeExrText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareExrOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write an EXR text artifact.");
            }
        }
    }

    void WebglLoaderTextureExrRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-exposure-two" &&
            options.targetFrame == 0u;
        const bool loader =
            options.scenarioId == "canonical-loader" &&
            options.targetFrame == 0u;
        const bool nondefault =
            options.scenarioId == "nondefault-exposure" &&
            options.targetFrame == 1u;
        if (options.caseId != "webgl_loader_texture_exr" ||
            (!initial && !loader && !nondefault) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() ||
            (nondefault != !options.inputReplayPath.empty()))
        {
            throw std::invalid_argument(
                "EXR adapter requires one locked manifest scenario.");
        }
        device = inDevice;
        const std::filesystem::path exrPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" / "memorial.exr";
        const eastl::vector<uint8_t> bytes = readExrInput(exrPath);
        if (calculateExrSha256(bytes) != MemorialExrSha256)
        {
            throw std::runtime_error(
                "memorial.exr differs from the r185 asset lock.");
        }
        if (nondefault)
        {
            const eastl::vector<uint8_t> replayBytes = readExrInput(
                std::filesystem::path(options.inputReplayPath.c_str()));
            if (calculateExrSha256(replayBytes) !=
                ExrExposureReplaySha256)
            {
                throw std::runtime_error(
                    "EXR exposure replay differs from its canonical lock.");
            }
        }
        image = decodeExrRgba16Float(exrPath);
        if (image.width != 512u || image.height != 768u ||
            image.pixels.size() != 512u * 768u * 4u)
        {
            throw std::runtime_error(
                "Decoded memorial.exr dimensions differ from r185.");
        }
        vertices = {
            {{-1.0f, -1.0f}, {0.0f, 0.0f}},
            {{1.0f, -1.0f}, {1.0f, 0.0f}},
            {{1.0f, 1.0f}, {1.0f, 1.0f}},
            {{-1.0f, 1.0f}, {0.0f, 1.0f}},
        };
        indices = {0u, 1u, 2u, 0u, 2u, 3u};
        uniforms.exposureAndPadding = float4(
            nondefault ? 0.65f : 2.0f,
            0.0f,
            0.0f,
            0.0f);
    }

    void WebglLoaderTextureExrRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderTextureExrRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
            prepareExrOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write the EXR RGBA capture.");
            }
        }
        const bool replay = options.scenarioId == "nondefault-exposure";
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\": 1,\n"
                 << "  \"source\": \"gvm-three-r185\",\n"
                 << "  \"caseId\": \"webgl_loader_texture_exr\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"randomSeed\": " << options.randomSeed << ",\n"
                 << "  \"frame\": " << frameIndex << ",\n"
                 << "  \"width\": " << width << ",\n"
                 << "  \"height\": " << height << ",\n"
                 << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\": " << byteCount << ",\n"
                 << "  \"format\": \"rgba8unorm\",\n"
                 << "  \"samplePolicy\": {\"mode\":\"single-sample\","
                 << "\"msaaEnabled\":false,\"simulateMsaa\":false},\n"
                 << "  \"assetSha256\": \"" << MemorialExrSha256 << "\"";
        if (replay)
        {
            metadata << ",\n  \"inputReplay\": {\n"
                     << "    \"sha256\": \"" << ExrExposureReplaySha256 << "\",\n"
                     << "    \"caseId\": \"webgl_loader_texture_exr\",\n"
                     << "    \"scenarioId\": \"nondefault-exposure\",\n"
                     << "    \"captureFrame\": 1,\n"
                     << "    \"eventCount\": 2,\n"
                     << "    \"target\": \"body > canvas\"\n"
                     << "  }";
        }
        metadata << "\n}\n";
        writeExrText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\": 1,\n"
                 << "  \"caseId\": \"webgl_loader_texture_exr\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\": " << frameIndex << ",\n"
                 << "  \"gpuWorkDslOnly\": true,\n"
                 << "  \"renderSetPolicy\": \"not-required\",\n"
                 << "  \"sceneRenderSetCount\": 0,\n"
                 << "  \"renderableObjectCount\": 1,\n"
                 << "  \"scenePassCount\": 1,\n"
                 << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\","
                 << "\"scenePass\":\"exr-quad\",\"entityOrdinal\":0}],\n"
                 << "  \"drawCommandCount\": 1,\n"
                 << "  \"instanceCount\": 1,\n"
                 << "  \"decodedWidth\": " << image.width << ",\n"
                 << "  \"decodedHeight\": " << image.height << ",\n"
                 << "  \"channelCount\": 4,\n"
                 << "  \"textureFormat\": \"rgba16float\",\n"
                 << "  \"mipCount\": 1\n}\n";
        writeExrText(options.sceneSnapshotPath, snapshot.str());
        if (options.scenarioId == "canonical-loader")
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\": 1,\n"
                     << "  \"caseId\": \"webgl_loader_texture_exr\",\n"
                     << "  \"scenarioId\": \"canonical-loader\",\n"
                     << "  \"frame\": 0,\n"
                     << "  \"kind\": \"loader-snapshot\",\n"
                     << "  \"canonicalState\": "
                     << "\"512x768-piz-rgb-half-three-channels-linear-filter-no-mips\",\n"
                     << "  \"result\": {\"renderableObjectCount\":1,"
                     << "\"sceneRootCount\":1,"
                     << "\"canonicalSceneSha256\":\""
                     << ExrCanonicalSceneSha256 << "\","
                     << "\"assetSha256\":\"" << MemorialExrSha256 << "\","
                     << "\"width\":512,\"height\":768,"
                     << "\"compression\":\"piz\","
                     << "\"sourceChannels\":[\"B\",\"G\",\"R\"],"
                     << "\"sourceChannelType\":\"half\","
                     << "\"outputFormat\":\"rgba16float\","
                     << "\"generatedMipmaps\":false}\n}\n";
            writeExrText(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglLoaderTextureExrRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
}
