#include "RenderSetPhase0RuntimeAdapter.hpp"

#include <EASTL/vector.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        /** Creates parent directories for an explicitly requested fixture output path. */
        void prepareOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes a tightly packed RGBA8 byte count while rejecting integer overflow. */
        uint64_t computeRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > (std::numeric_limits<uint64_t>::max() / BytesPerPixel))
            {
                throw std::overflow_error("RenderSetPhase0 RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }
    } // namespace

    void RenderSetPhase0RuntimeAdapter::initialize(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        if (options.frameCount == 0u)
        {
            throw std::invalid_argument("RenderSetPhase0 runtime adapter requires at least one frame.");
        }
        if (options.caseId != "render-set-phase0" || options.scenarioId != "entity-lifecycle")
        {
            throw std::invalid_argument(
                "RenderSetPhase0 runtime adapter requires case-id render-set-phase0 and scenario-id entity-lifecycle.");
        }

        device = inDevice;
        controller.initialize(renderer);
    }

    void RenderSetPhase0RuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        const uint32_t mutationFrame = options.frameCount == 1u ? 0u : 1u;
        if (frameIndex == mutationFrame)
        {
            controller.beforeFrame(renderer, 1u);
        }
    }

    void RenderSetPhase0RuntimeAdapter::afterFrame(
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

        const uint64_t byteCount = computeRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("RenderSetPhase0 RGBA8 capture exceeds host addressable storage.");
        }

        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error("RenderSetPhase0 runtime adapter could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();

        writeRgbaCapture(options, frameIndex, width, height, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        if (!options.sceneSnapshotPath.empty())
        {
            controller.writeSnapshot(std::filesystem::path(options.sceneSnapshotPath.c_str()));
        }
        captureWritten = true;
    }

    void RenderSetPhase0RuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void RenderSetPhase0RuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        (void)frameIndex;
        (void)width;
        (void)height;
        if (options.captureRgbaPath.empty())
        {
            return;
        }

        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("RenderSetPhase0 runtime adapter could not open the RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("RenderSetPhase0 runtime adapter could not write the complete RGBA capture.");
        }
    }

    void RenderSetPhase0RuntimeAdapter::writeCaptureMetadata(
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
        prepareOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("RenderSetPhase0 runtime adapter could not open the capture metadata path.");
        }

        output << "{\n"
               << "  \"caseId\": \"render-set-phase0\",\n"
               << "  \"scenarioId\": \"entity-lifecycle\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
