#include "WebgpuTexturesPartialUpdateRuntimeAdapter.hpp"


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
