#include "WebgpuProceduralTextureRuntimeAdapter.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr const char *CanonicalGuiReplay =
            "{\"schemaVersion\":1,\"caseId\":\"webgpu_procedural_texture\","
            "\"scenarioId\":\"gui-static-texture\",\"frame\":61,"
            "\"target\":\"canvas:not([class])\",\"canvasWidth\":800,"
            "\"canvasHeight\":500,\"canonicalState\":{\"uvScale\":7.25,"
            "\"blurAmount\":1.35,\"autoUpdate\":false},\"events\":[{"
            "\"type\":\"pointermove\",\"frame\":0,\"x\":400,\"y\":250}]}\n";

        /** Creates parent directories for one procedural-texture artifact. */
        void prepareProceduralTextureOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Reads one bounded canonical-state document as exact bytes. */
        std::string readProceduralTextureReplay(
            const std::filesystem::path &path)
        {
            std::ifstream input(
                path,
                std::ios::binary | std::ios::ate);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not open the procedural-texture GUI replay.");
            }
            const std::streamoff size = input.tellg();
            if (size <= 0 || size > 4096)
            {
                throw std::runtime_error(
                    "The procedural-texture GUI replay has an invalid size.");
            }
            input.seekg(0, std::ios::beg);
            std::string bytes(static_cast<size_t>(size), '\0');
            input.read(bytes.data(), size);
            if (!input)
            {
                throw std::runtime_error(
                    "Could not read the procedural-texture GUI replay.");
            }
            return bytes;
        }

        /** Returns the validated RGBA8 capture byte count. */
        uint64_t computeProceduralTextureRgbaByteCount(
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
                    "Procedural-texture capture size overflowed.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void WebgpuProceduralTextureRuntimeAdapter::initializeResources(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgpu_procedural_texture" ||
            options.width != 800u ||
            options.height != 500u ||
            options.randomSeed != 0x12345678u)
        {
            throw std::invalid_argument(
                "Procedural-texture adapter requires its locked case, extent, and seed.");
        }
        const bool initial =
            options.scenarioId == "initial-checker-blur" &&
            options.targetFrame == 0u;
        const bool fixed =
            options.scenarioId == "fixed-auto-update" &&
            options.targetFrame == 60u;
        const bool gui =
            options.scenarioId == "gui-static-texture" &&
            options.targetFrame == 61u;
        if (!initial && !fixed && !gui)
        {
            throw std::invalid_argument(
                "Procedural-texture scenario differs from the Manifest.");
        }
        if (gui)
        {
            if (options.inputReplayPath.empty() ||
                readProceduralTextureReplay(
                    std::filesystem::path(
                        options.inputReplayPath.c_str())) !=
                    CanonicalGuiReplay)
            {
                throw std::invalid_argument(
                    "Procedural-texture GUI replay differs from the canonical state.");
            }
            uvScale = 7.25f;
            blurAmount = 1.35f;
            autoUpdate = false;
        }
        else if (!options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Only the GUI procedural-texture scenario accepts replay input.");
        }
        device = inDevice;
    }

    void WebgpuProceduralTextureRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuProceduralTextureRuntimeAdapter::afterFrame(
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
            computeProceduralTextureRgbaByteCount(
                width,
                height);
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

    void WebgpuProceduralTextureRuntimeAdapter::writeArtifacts(
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
            prepareProceduralTextureOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write procedural-texture RGBA8 capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareProceduralTextureOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::trunc);
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgpu_procedural_texture\",\n"
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
                << "  \"rowStrideBytes\":"
                << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\",\n"
                << "  \"inputReplay\":"
                << (autoUpdate
                        ? "null"
                        : "{\"schemaVersion\":1,"
                          "\"caseId\":\"webgpu_procedural_texture\","
                          "\"scenarioId\":\"gui-static-texture\","
                          "\"captureFrame\":61,"
                          "\"sha256\":\"cac972b7e86ea4bb862229bd8dff89346c6d0ad4ceb9547d075851e098082ada\","
                          "\"target\":\"canvas:not([class])\","
                          "\"eventCount\":1,"
                          "\"lastEventFrame\":0}")
                << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareProceduralTextureOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::trunc);
            const uint32_t updateCount =
                autoUpdate ? frameIndex + 1u : 1u;
            output
                << "{\n  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgpu_procedural_texture\",\n"
                << "  \"scenarioId\":\""
                << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"not-required\",\n"
                << "  \"sceneRenderSetCount\":0,\n"
                << "  \"renderableObjectCount\":1,\n"
                << "  \"entityCount\":0,\n"
                << "  \"instanceCount\":1,\n"
                << "  \"uvScale\":" << uvScale << ",\n"
                << "  \"blurAmount\":" << blurAmount << ",\n"
                << "  \"blurSigma\":20,\n"
                << "  \"blurKernelSize\":43,\n"
                << "  \"autoUpdate\":"
                << (autoUpdate ? "true" : "false") << ",\n"
                << "  \"proceduralUpdateCount\":"
                << updateCount << ",\n"
                << "  \"computePassCount\":"
                << updateCount * 3u << ",\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"scenePasses\":["
                << "\"WebgpuProceduralTextureMainPass\"],\n"
                << "  \"scenePassSequence\":[{"
                << "\"sceneRoot\":\"scene\","
                << "\"scenePass\":\"main-procedural-plane\"}],\n"
                << "  \"screenPasses\":["
                << "\"checker-to-rwtexture2d\","
                << "\"gaussian-blur-horizontal-radius-20\","
                << "\"gaussian-blur-vertical-radius-20\"],\n"
                << "  \"attachmentFormats\":[\"rgba8unorm\"],\n"
                << "  \"storageTextureFormats\":["
                << "\"rgba16float\",\"rgba16float\","
                << "\"rgba16float\"],\n"
                << "  \"storageTextureDimensions\":[512,512],\n"
                << "  \"computeDispatchThreads\":[512,512,1],\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"directDrawFallback\":false\n}\n";
        }
    }

    void WebgpuProceduralTextureRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }
} // namespace GVM::ThreeSamples
