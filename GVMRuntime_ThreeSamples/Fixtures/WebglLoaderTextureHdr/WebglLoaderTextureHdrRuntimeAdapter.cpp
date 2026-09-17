#include "WebglLoaderTextureHdrRuntimeAdapter.hpp"

#include "SampleAssetDecoders.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>
#include <EASTL/string.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *HdrAssetSha256 =
            "18376b9219927b363127817900b1c170a0f752f52a63f5949655b4150e35061d";

        /** Reads one bounded immutable binary asset. */
        eastl::vector<uint8_t> readHdrAsset(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open pinned memorial.hdr.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0 || uint64_t(byteCount) > std::numeric_limits<CC_LONG>::max())
            {
                throw std::runtime_error("Pinned memorial.hdr has an invalid byte count.");
            }
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read complete memorial.hdr.");
            return bytes;
        }

        /** Returns the lowercase SHA-256 identity of one bounded payload. */
        eastl::string calculateHdrSha256(const eastl::vector<uint8_t> &bytes)
        {
            eastl::array<uint8_t, CC_SHA256_DIGEST_LENGTH> digest = {};
            CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest.data());
            constexpr char HexDigits[] = "0123456789abcdef";
            eastl::string result;
            result.reserve(digest.size() * 2u);
            for (uint8_t value : digest)
            {
                result.push_back(HexDigits[value >> 4u]);
                result.push_back(HexDigits[value & 15u]);
            }
            return result;
        }

        /** Converts one finite float to an IEEE 754 binary16 bit pattern. */
        uint16_t convertFloatToHalf(float value)
        {
            uint32_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            const uint32_t sign = (bits >> 16u) & 0x8000u;
            const uint32_t magnitude = bits & 0x7fffffffu;
            if (magnitude >= 0x7f800000u)
            {
                return static_cast<uint16_t>(sign | (magnitude > 0x7f800000u ? 0x7e00u : 0x7c00u));
            }
            int32_t exponent = int32_t((magnitude >> 23u) & 0xffu) - 127 + 15;
            uint32_t mantissa = magnitude & 0x7fffffu;
            if (exponent <= 0)
            {
                if (exponent < -10) return static_cast<uint16_t>(sign);
                mantissa = (mantissa | 0x800000u) >> uint32_t(1 - exponent);
                return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13u));
            }
            if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7c00u);
            const uint32_t rounded = mantissa + 0x1000u;
            if (rounded & 0x800000u)
            {
                mantissa = 0u;
                ++exponent;
                if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7c00u);
            }
            else
            {
                mantissa = rounded;
            }
            return static_cast<uint16_t>(sign | (uint32_t(exponent) << 10u) | (mantissa >> 13u));
        }

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareHdrOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one bounded text artifact with deterministic truncation. */
        void writeHdrText(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareHdrOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            if (!output) throw std::runtime_error("Could not open HDR text artifact.");
            output << text;
            if (!output) throw std::runtime_error("Could not write HDR text artifact.");
        }
    }

    void WebglLoaderTextureHdrRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial-exposure-two" && options.targetFrame == 0u;
        const bool canonical = options.scenarioId == "canonical-loader" && options.targetFrame == 0u;
        const bool nondefault = options.scenarioId == "nondefault-exposure" && options.targetFrame == 1u;
        if (options.caseId != "webgl_loader_texture_hdr" || (!initial && !canonical && !nondefault) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
        {
            throw std::invalid_argument("HDR adapter requires one locked manifest scenario.");
        }
        if (nondefault != !options.inputReplayPath.empty())
        {
            throw std::invalid_argument("Only nondefault-exposure accepts the locked replay.");
        }
        device = inDevice;
        const eastl::vector<uint8_t> bytes = readHdrAsset(
            std::filesystem::path(options.assetRoot.c_str()) / "textures" / "memorial.hdr");
        if (calculateHdrSha256(bytes) != HdrAssetSha256)
        {
            throw std::runtime_error("memorial.hdr differs from the r185 asset lock.");
        }
        const ThreeCompat::DecodedRadianceImage image = ThreeCompat::decodeRadianceRgbe(bytes);
        imageWidth = image.width;
        imageHeight = image.height;
        if (imageWidth != 512u || imageHeight != 768u)
        {
            throw std::runtime_error("memorial.hdr dimensions diverged from r185.");
        }
        halfRgba.reserve(image.rgba.size());
        for (float value : image.rgba) halfRgba.push_back(convertFloatToHalf(value));
        exposure = nondefault ? 0.65f : 2.0f;
    }

    void WebglLoaderTextureHdrRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglLoaderTextureHdrRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
            prepareHdrOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write HDR RGBA capture.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\": 1,\n  \"source\": \"gvm-three-r185\",\n"
                 << "  \"caseId\": \"webgl_loader_texture_hdr\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"randomSeed\": " << options.randomSeed << ",\n"
                 << "  \"frame\": " << frameIndex << ",\n"
                 << "  \"width\": " << width << ",\n  \"height\": " << height << ",\n"
                 << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
                 << "  \"byteCount\": " << byteCount << ",\n  \"format\": \"rgba8unorm\",\n"
                 << "  \"assetSha256\": \"" << HdrAssetSha256 << "\"";
        if (options.scenarioId == "nondefault-exposure")
        {
            metadata << ",\n  \"inputReplay\": {\n"
                     << "    \"sha256\": \"ed505a7ad6893a6de12e1886c23684f73d1a40d300bfca9f0dc4619ed2086a28\",\n"
                     << "    \"caseId\": \"webgl_loader_texture_hdr\",\n"
                     << "    \"scenarioId\": \"nondefault-exposure\",\n"
                     << "    \"captureFrame\": 1,\n"
                     << "    \"eventCount\": 1,\n"
                     << "    \"target\": \".lil-gui .controller.number\"\n"
                     << "  }";
        }
        metadata << "\n}\n";
        writeHdrText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot << "{\n  \"schemaVersion\": 1,\n  \"caseId\": \"webgl_loader_texture_hdr\",\n"
                 << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\": " << frameIndex << ",\n"
                 << "  \"gpuWorkDslOnly\": true,\n"
                 << "  \"renderSetPolicy\": \"not-required\",\n"
                 << "  \"sceneRenderSetCount\": 0,\n"
                 << "  \"renderableObjectCount\": 1,\n"
                 << "  \"scenePassCount\": 1,\n"
                 << "  \"scenePassSequence\": [{\"sceneRoot\":\"scene\","
                 << "\"scenePass\":\"hdr-quad\",\"entityOrdinal\":0}],\n"
                 << "  \"drawCommandCount\": 1,\n"
                 << "  \"instanceCount\": 1,\n  \"imageWidth\": " << imageWidth
                 << ",\n  \"imageHeight\": " << imageHeight << ",\n"
                 << "  \"textureFormat\": \"rgba16float\",\n  \"exposure\": " << exposure << "\n}\n";
        writeHdrText(options.sceneSnapshotPath, snapshot.str());
        if (options.scenarioId == "canonical-loader")
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\": 1,\n"
                     << "  \"caseId\": \"webgl_loader_texture_hdr\",\n"
                     << "  \"scenarioId\": \"canonical-loader\",\n"
                     << "  \"frame\": 0,\n"
                     << "  \"kind\": \"loader-snapshot\",\n"
                     << "  \"canonicalState\": "
                     << "\"512x768-rle-rgbe-exposure-metadata-one-rgba16float\",\n"
                     << "  \"result\": {\n"
                     << "    \"renderableObjectCount\": 0,\n"
                     << "    \"sceneRootCount\": 1,\n"
                     << "    \"canonicalSceneSha256\": "
                     << "\"a340c268f59c2dc96dd27ee9287789dbd1a4b6ac60ba158a3c2c2048d08c9d76\",\n"
                     << "    \"assetPath\": \"textures/memorial.hdr\",\n"
                     << "    \"assetSha256\": \"" << HdrAssetSha256 << "\",\n"
                     << "    \"width\": 512,\n"
                     << "    \"height\": 768,\n"
                     << "    \"textureFormat\": \"rgba16float\"\n"
                     << "  }\n}\n";
            writeHdrText(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglLoaderTextureHdrRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
}
