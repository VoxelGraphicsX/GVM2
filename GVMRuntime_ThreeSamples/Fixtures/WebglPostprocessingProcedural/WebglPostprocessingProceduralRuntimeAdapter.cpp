#include "WebglPostprocessingProceduralRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"


#include <EASTL/array.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t RequiredOutputWidth = 800u;
        constexpr uint32_t RequiredOutputHeight = 500u;
        constexpr const char *ReplayTarget = ".lil-gui .controller.option select";
        constexpr const char *Noise1dReplaySha256 =
            "c8e6b41d87dcc55a45b78475e82ba792145089f12ce6f3c577ad8ff3d2ba747f";
        constexpr const char *Noise2dReplaySha256 =
            "e2d1250651642ec5f3abb815aa313a624a52bca500591cfa207d0f739d2570c5";

        /** Creates parent directories for one explicitly requested output artifact. */
        void prepareProceduralOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes the tightly packed RGBA8 byte count while rejecting overflow. */
        uint64_t computeProceduralRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgl_postprocessing_procedural RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }






    } // namespace

    void WebglPostprocessingProceduralRuntimeAdapter::initializeScenario(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.caseId != "webgl_postprocessing_procedural")
        {
            throw std::invalid_argument(
                "Procedural runtime adapter requires case-id webgl_postprocessing_procedural.");
        }
        if (options.width != RequiredOutputWidth || options.height != RequiredOutputHeight)
        {
            throw std::invalid_argument(
                "webgl_postprocessing_procedural requires the locked 800x500 extent.");
        }
        if (options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument(
                "webgl_postprocessing_procedural requires the shared deterministic seed.");
        }

        const bool initial3d =
            options.scenarioId == "initial-3d" && options.targetFrame == 0u;
        const bool noise1d =
            options.scenarioId == "noise-1d" && options.targetFrame == 1u;
        const bool noise2d =
            options.scenarioId == "noise-2d" && options.targetFrame == 1u;
        if (!initial3d && !noise1d && !noise2d)
        {
            throw std::invalid_argument(
                "webgl_postprocessing_procedural requires initial-3d/frame 0, "
                "noise-1d/frame 1, or noise-2d/frame 1.");
        }
        if (options.frameCount != options.targetFrame + 1u)
        {
            throw std::invalid_argument(
                "Procedural scenarios require inclusive execution through the selected frame.");
        }
        if (initial3d && !options.inputReplayPath.empty())
        {
            throw std::invalid_argument("initial-3d must not consume an input replay.");
        }
        if (!initial3d && options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "Non-default procedural scenarios require the canonical --input-replay.");
        }

        device = inDevice;
        if (initial3d)
        {
            noiseMode = "noiseRandom3D";
            canonicalState = "noise-random-three-channel";
            noiseDimension = 3u;
            return;
        }



        noiseMode = noise1d ? "noiseRandom1D" : "noiseRandom2D";
        canonicalState = noise1d
            ? "noise-random-one-channel"
            : "noise-random-two-channel-blue-black-mix";
        noiseDimension = noise1d ? 1u : 2u;
        replaySchemaVersion = 1u;
        replayCaptureFrame = 1u;
        replayEventCount = 2u;
        replayScenarioId = options.scenarioId;
        replayTarget = ReplayTarget;
        usesInputReplay = true;
    }

    void WebglPostprocessingProceduralRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPostprocessingProceduralRuntimeAdapter::afterFrame(
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

        const uint64_t byteCount = computeProceduralRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Procedural RGBA8 capture exceeds host addressable storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "Procedural runtime adapter could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();

        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglPostprocessingProceduralRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebglPostprocessingProceduralRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareProceduralOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open procedural RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Could not write the complete procedural RGBA capture.");
        }
    }

    void WebglPostprocessingProceduralRuntimeAdapter::writeCaptureMetadata(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.captureMetadataPath.c_str());
        prepareProceduralOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open procedural metadata output path.");
        }

        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgl_postprocessing_procedural\",\n"
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
               << "  \"noiseMode\": \"" << noiseMode.c_str() << "\",\n"
               << "  \"noiseDimension\": " << noiseDimension << ",\n"
               << "  \"canonicalState\": \"" << canonicalState.c_str() << "\",\n"
               << "  \"inputReplay\": ";
        if (!usesInputReplay)
        {
            output << "null\n";
        }
        else
        {
            output << "{\n"
                   << "    \"schemaVersion\": " << replaySchemaVersion << ",\n"
                   << "    \"sha256\": \"" << replaySha256.c_str() << "\",\n"
                   << "    \"caseId\": \"webgl_postprocessing_procedural\",\n"
                   << "    \"scenarioId\": \"" << replayScenarioId.c_str() << "\",\n"
                   << "    \"captureFrame\": " << replayCaptureFrame << ",\n"
                   << "    \"eventCount\": " << replayEventCount << ",\n"
                   << "    \"target\": \"" << replayTarget.c_str() << "\"\n"
                   << "  }\n";
        }
        output << "}\n";
    }

    void WebglPostprocessingProceduralRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }
        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareProceduralOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not open procedural structural snapshot path.");
        }

        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgl_postprocessing_procedural\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"upstreamCommit\": \"2431a09f46f34c560bc8e44b33be0e567723d5b9\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 0,\n"
               << "  \"instanceCount\": 0,\n"
               << "  \"scenePassCount\": 0,\n"
               << "  \"screenPassCount\": 3,\n"
               << "  \"drawCommandCount\": 3,\n"
               << "  \"explicitVertexCount\": 15,\n"
               << "  \"noiseMode\": \"" << noiseMode.c_str() << "\",\n"
               << "  \"noiseDimension\": " << noiseDimension << ",\n"
               << "  \"canonicalState\": \"" << canonicalState.c_str() << "\",\n"
               << "  \"inputReplayEventCount\": " << replayEventCount << ",\n"
               << "  \"inputReplay\": ";
        if (!usesInputReplay)
        {
            output << "null,\n";
        }
        else
        {
            output << "{\n"
                   << "    \"schemaVersion\": " << replaySchemaVersion << ",\n"
                   << "    \"sha256\": \"" << replaySha256.c_str() << "\",\n"
                   << "    \"caseId\": \"webgl_postprocessing_procedural\",\n"
                   << "    \"scenarioId\": \"" << replayScenarioId.c_str() << "\",\n"
                   << "    \"captureFrame\": " << replayCaptureFrame << ",\n"
                   << "    \"eventCount\": " << replayEventCount << ",\n"
                   << "    \"target\": \"" << replayTarget.c_str() << "\"\n"
                   << "  },\n";
        }
        output << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
