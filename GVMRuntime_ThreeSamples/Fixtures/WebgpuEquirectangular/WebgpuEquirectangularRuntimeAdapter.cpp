#include "WebgpuEquirectangularRuntimeAdapter.hpp"

#include "ThreeCompat.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <EASTL/array.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t PanoramaWidth = 4096u;
        constexpr uint32_t PanoramaHeight = 2048u;
        constexpr uint32_t PanoramaMipCount = 13u;
        constexpr const char *PanoramaSha256 =
            "3efa22071f3f84ec26248ee58aa05aba635a0a9d810bca93da5a94f33e907524";
        constexpr const char *ReplaySha256 =
            "c8d708be0972f251595eadc7a1fe8085de863c5ea0eeac2e3cac7f3b7de828ba";

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareEquirectangularOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Computes one bounded tightly packed RGBA8 capture size. */
        uint64_t computeEquirectangularRgbaByteCount(
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
                    "Equirectangular capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Reads one asset file into deterministic immutable bytes. */
        eastl::vector<uint8_t> readEquirectangularAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open the pinned equirectangular JPEG.");
            }
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0)
            {
                throw std::runtime_error(
                    "The pinned equirectangular JPEG is empty.");
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
                    "Could not read the pinned equirectangular JPEG.");
            }
            return bytes;
        }

        /** Returns the lowercase SHA-256 digest of one bounded byte sequence. */
        eastl::string calculateEquirectangularSha256(
            const eastl::vector<uint8_t> &bytes)
        {
            if (bytes.size() >
                static_cast<size_t>(std::numeric_limits<CC_LONG>::max()))
            {
                throw std::overflow_error(
                    "Equirectangular SHA-256 input is too large.");
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

        /** Validates the exact three-scenario Manifest contract. */
        void validateEquirectangularOptions(
            const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgpu_equirectangular" ||
                options.randomSeed != 0x12345678u ||
                options.assetRoot.empty())
            {
                throw std::invalid_argument(
                    "Equirectangular adapter requires its case, seed, and asset root.");
            }
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u &&
                options.inputReplayPath.empty();
            const bool autoRotate =
                options.scenarioId == "auto-rotate" &&
                options.targetFrame == 120u &&
                options.inputReplayPath.empty();
            const bool adjustedOrbit =
                options.scenarioId == "adjusted-orbit" &&
                options.targetFrame == 121u &&
                !options.inputReplayPath.empty();
            if (!initial && !autoRotate && !adjustedOrbit)
            {
                throw std::invalid_argument(
                    "Equirectangular scenario differs from the Manifest.");
            }
        }

        /** Returns the locked manual OrbitControls offsets for the replay. */
        void resolveEquirectangularScenario(
            const ThreeSampleHostOptions &options,
            float &intensity,
            float &thetaOffset,
            float &phiOffset)
        {
            intensity = 1.0f;
            thetaOffset = 0.0f;
            phiOffset = 0.0f;
            if (options.scenarioId != "adjusted-orbit")
            {
                return;
            }
            const std::filesystem::path replayPath(
                options.inputReplayPath.c_str());
            const eastl::vector<uint8_t> replayBytes =
                readEquirectangularAsset(replayPath);
            if (calculateEquirectangularSha256(replayBytes) != ReplaySha256)
            {
                throw std::invalid_argument(
                    "Equirectangular replay differs from its locked identity.");
            }
            intensity = 0.55f;
            thetaOffset = 0.12566370614359174f;
            phiOffset = 0.06283185307179587f;
        }
    } // namespace

    void WebgpuEquirectangularRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateEquirectangularOptions(options);
        device = inDevice;
        resolveEquirectangularScenario(
            options,
            backgroundIntensity,
            manualThetaOffset,
            manualPhiOffset);
        const std::filesystem::path panoramaPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "2294472375_24a3b8ef46_o.jpg";
        const eastl::vector<uint8_t> assetBytes =
            readEquirectangularAsset(panoramaPath);
        if (calculateEquirectangularSha256(assetBytes) != PanoramaSha256)
        {
            throw std::invalid_argument(
                "Equirectangular JPEG differs from the pinned Three r185 asset.");
        }
        const RgbaImageData panorama =
            decodeJpegRgba8(panoramaPath);
        if (panorama.width != PanoramaWidth ||
            panorama.height != PanoramaHeight)
        {
            throw std::runtime_error(
                "Equirectangular JPEG decoded to an unexpected extent.");
        }
        const eastl::vector<RgbaImageData> mipChain =
            buildSrgbMipChain(panorama);
        if (mipChain.size() != PanoramaMipCount)
        {
            throw std::runtime_error(
                "Equirectangular JPEG did not produce thirteen explicit mips.");
        }
        panoramaWidth = panorama.width;
        panoramaHeight = panorama.height;
        mipPixels.clear();
        mipPixels.reserve(mipChain.size());
        for (const RgbaImageData &mip : mipChain)
        {
            mipPixels.push_back(mip.pixels);
        }
    }

    void WebgpuEquirectangularRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuEquirectangularRuntimeAdapter::afterFrame(
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
            computeEquirectangularRgbaByteCount(width, height);
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

    void WebgpuEquirectangularRuntimeAdapter::writeArtifacts(
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
            prepareEquirectangularOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write equirectangular RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareEquirectangularOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n  \"schemaVersion\":1,\n"
                   << "  \"source\":\"gvm-three-r185\",\n"
                   << "  \"caseId\":\"webgpu_equirectangular\",\n"
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
                   << "  \"format\":\"rgba8unorm\",\n"
                   << "  \"byteCount\":" << rgba.size() << ",\n"
                   << "  \"inputReplay\":";
            if (options.scenarioId == "adjusted-orbit")
            {
                output
                    << "{\n"
                    << "    \"sha256\":\"" << ReplaySha256 << "\",\n"
                    << "    \"caseId\":\"webgpu_equirectangular\",\n"
                    << "    \"scenarioId\":\"adjusted-orbit\",\n"
                    << "    \"captureFrame\":121,\n"
                    << "    \"eventCount\":3,\n"
                    << "    \"target\":\"canvas:not([class])\"\n"
                    << "  }\n}\n";
            }
            else
            {
                output << "null\n}\n";
            }
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareEquirectangularOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n"
                   << "  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgpu_equirectangular\",\n"
                   << "  \"scenarioId\":\""
                   << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\":" << frameIndex << ",\n"
                   << "  \"gpuWorkDslOnly\":true,\n"
                   << "  \"renderSetPolicy\":\"not-required\",\n"
                   << "  \"sceneRenderSetCount\":0,\n"
                   << "  \"renderableObjectCount\":0,\n"
                   << "  \"entityCount\":0,\n"
                   << "  \"instanceCount\":1,\n"
                   << "  \"drawCommandCount\":1,\n"
                   << "  \"scenePassCount\":0,\n"
                   << "  \"screenPassCount\":1,\n"
                   << "  \"panoramaExtent\":[4096,2048],\n"
                   << "  \"panoramaMipCount\":13,\n"
                   << "  \"backgroundIntensity\":"
                   << backgroundIntensity << "\n}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareEquirectangularOutputPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << "{\n"
                   << "  \"schemaVersion\":1,\n"
                   << "  \"caseId\":\"webgpu_equirectangular\",\n"
                   << "  \"assetSha256\":\""
                   << PanoramaSha256 << "\",\n"
                   << "  \"explicitMipCount\":13,\n"
                   << "  \"cameraModel\":\"OrbitControls-fixed-step\",\n"
                   << "  \"backgroundIntensity\":"
                   << backgroundIntensity << ",\n"
                   << "  \"manualThetaOffset\":"
                   << manualThetaOffset << ",\n"
                   << "  \"manualPhiOffset\":"
                   << manualPhiOffset << "\n}\n";
        }
    }

    void WebgpuEquirectangularRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        mipPixels.clear();
        captureWritten = false;
    }
} // namespace GVM::ThreeSamples
