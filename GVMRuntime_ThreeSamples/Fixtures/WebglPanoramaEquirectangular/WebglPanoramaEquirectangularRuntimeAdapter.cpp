#include "WebglPanoramaEquirectangularRuntimeAdapter.hpp"


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
        constexpr uint32_t PanoramaWidth = 4096u;
        constexpr uint32_t PanoramaHeight = 2048u;
        constexpr uint32_t PanoramaMipCount = 13u;
        constexpr const char *PanoramaSha256 =
            "3efa22071f3f84ec26248ee58aa05aba635a0a9d810bca93da5a94f33e907524";
        constexpr const char *ReplaySha256 =
            "07eeeb3e433a584521fe35f3ba59900fc3aee742965503f6aff35426ea2776d6";

        /** Creates parent directories for one panorama gate artifact. */
        void preparePanoramaOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }





        /** Returns the validated tightly packed RGBA8 byte count. */
        uint64_t computePanoramaRgbaByteCount(
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
                    "Panorama capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebglPanoramaEquirectangularRuntimeAdapter::
        initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
    {
        if (options.caseId !=
                "webgl_panorama_equirectangular" ||
            options.randomSeed != 0x12345678u ||
            options.assetRoot.empty() ||
            options.width != 800u ||
            options.height != 500u)
        {
            throw std::invalid_argument(
                "Panorama adapter requires its locked case, seed, extent, and assets.");
        }
        const bool loader =
            options.scenarioId == "texture-snapshot" &&
            options.targetFrame == 0u;
        const bool initial =
            options.scenarioId == "initial" &&
            options.targetFrame == 0u;
        const bool autoPan =
            options.scenarioId == "auto-pan" &&
            options.targetFrame == 60u;
        const bool pointerWheel =
            options.scenarioId == "pointer-wheel" &&
            options.targetFrame == 61u;
        if (!loader && !initial && !autoPan && !pointerWheel)
        {
            throw std::invalid_argument(
                "Panorama scenario differs from the Manifest.");
        }
        hasReplay = pointerWheel;
        if (pointerWheel)
        {
            if (options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "Panorama replay differs from its locked identity.");
            }
            baseLongitude = -12.0f;
            latitude = 4.5f;
            fieldOfView = 75.0f;
        }
        else if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Only the pointer-wheel panorama scenario accepts replay input.");
        }
        const std::filesystem::path panoramaPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "textures" /
            "2294472375_24a3b8ef46_o.jpg";


        const RgbaImageData panorama =
            decodeJpegRgba8(panoramaPath);
        if (panorama.width != PanoramaWidth ||
            panorama.height != PanoramaHeight)
        {
            throw std::runtime_error(
                "Panorama JPEG decoded to an unexpected extent.");
        }
        const eastl::vector<RgbaImageData> mipChain =
            buildSrgbMipChain(panorama);
        if (mipChain.size() != PanoramaMipCount)
        {
            throw std::runtime_error(
                "Panorama JPEG did not produce thirteen explicit mips.");
        }
        panoramaWidth = panorama.width;
        panoramaHeight = panorama.height;
        mipPixels.clear();
        mipPixels.reserve(mipChain.size());
        for (const RgbaImageData &mip : mipChain)
        {
            mipPixels.push_back(mip.pixels);
        }
        device = inDevice;
    }

    void WebglPanoramaEquirectangularRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPanoramaEquirectangularRuntimeAdapter::afterFrame(
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
            computePanoramaRgbaByteCount(width, height);
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

    void WebglPanoramaEquirectangularRuntimeAdapter::writeArtifacts(
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
            preparePanoramaOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write panorama RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            preparePanoramaOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_panorama_equirectangular\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\""
                << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\""
                << threeSampleBackendName(options.backend)
                << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":"
                << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":"
                << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\",\n"
                << "  \"inputReplay\":";
            if (hasReplay)
            {
                output
                    << "{\"schemaVersion\":1,"
                    << "\"caseId\":\"webgl_panorama_equirectangular\","
                    << "\"scenarioId\":\"pointer-wheel\","
                    << "\"captureFrame\":61,"
                    << "\"sha256\":\"" << ReplaySha256 << "\","
                    << "\"target\":\"canvas\","
                    << "\"eventCount\":4,"
                    << "\"lastEventFrame\":1}";
            }
            else
            {
                output << "null";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            preparePanoramaOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_panorama_equirectangular\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"not-required\",\n"
                << "  \"sceneRenderSetCount\":0,\n"
                << "  \"renderableObjectCount\":1,\n"
                << "  \"entityCount\":0,\n"
                << "  \"instanceCount\":1,\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"scenePassSequence\":[{"
                << "\"sceneRoot\":\"scene\","
                << "\"scenePass\":\"main\"}],\n"
                << "  \"panoramaExtent\":[4096,2048],\n"
                << "  \"panoramaMipCount\":13,\n"
                << "  \"longitude\":"
                << baseLongitude +
                       0.1f * float(frameIndex + 1u)
                << ",\n"
                << "  \"latitude\":" << latitude << ",\n"
                << "  \"fieldOfView\":" << fieldOfView
                << "\n}\n";
        }
        if (!options.semanticSnapshotPath.empty() &&
            options.scenarioId == "texture-snapshot")
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            preparePanoramaOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_panorama_equirectangular\",\n"
                << "  \"scenarioId\":\"texture-snapshot\",\n"
                << "  \"frame\":0,\n"
                << "  \"kind\":\"loader-snapshot\",\n"
                << "  \"canonicalState\":\""
                << "equirectangular-jpeg-sha256-"
                << PanoramaSha256 << "\",\n"
                << "  \"result\":{"
                << "\"assetSha256\":\""
                << PanoramaSha256 << "\","
                << "\"decodedExtent\":[4096,2048],"
                << "\"explicitMipCount\":13,"
                << "\"renderableObjectCount\":0,"
                << "\"sceneRootCount\":1,"
                << "\"canonicalSceneSha256\":"
                << "\"2ab23c10d68638d8f85fe0fa655030662c9683d3719dff437316c08911f8680a\""
                << "}\n"
                << "}\n";
        }
    }

    void WebglPanoramaEquirectangularRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        mipPixels.clear();
    }
} // namespace GVM::ThreeSamples
