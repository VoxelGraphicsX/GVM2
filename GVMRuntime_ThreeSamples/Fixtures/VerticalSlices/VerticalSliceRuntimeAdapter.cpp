#include "VerticalSliceRuntimeAdapter.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        /** Creates parent directories for one explicitly requested capture artifact. */
        void prepareVerticalSliceOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes the tightly packed RGBA8 storage size while rejecting overflow. */
        uint64_t computeVerticalSliceRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("Vertical slice RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns true when the explicit case and scenario select the ordinary RenderClass slice. */
        bool isSimpleRenderClassSlice(const ThreeSampleHostOptions &options)
        {
            return options.caseId == "simple-render-class" && options.scenarioId == "single-object";
        }

        /** Returns true when the explicit case and scenario select the fullscreen postprocess slice. */
        bool isFullscreenPostprocessSlice(const ThreeSampleHostOptions &options)
        {
            return options.caseId == "fullscreen-postprocess" &&
                   options.scenarioId == "offscreen-screen-pass";
        }
    } // namespace

    void VerticalSliceRuntimeAdapter::initialize(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        if (options.frameCount == 0u)
        {
            throw std::invalid_argument("Vertical slice runtime adapter requires at least one frame.");
        }
        if (!isSimpleRenderClassSlice(options) && !isFullscreenPostprocessSlice(options))
        {
            throw std::invalid_argument(
                "Vertical slice runtime adapter requires simple-render-class/single-object or "
                "fullscreen-postprocess/offscreen-screen-pass.");
        }
        device = inDevice;
    }

    void VerticalSliceRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void VerticalSliceRuntimeAdapter::afterFrame(
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

        const uint64_t byteCount = computeVerticalSliceRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("Vertical slice RGBA8 capture exceeds host addressable storage.");
        }

        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error("Vertical slice runtime adapter could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();

        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options);
        captureWritten = true;
    }

    void VerticalSliceRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void VerticalSliceRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }

        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareVerticalSliceOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Vertical slice runtime adapter could not open the RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error("Vertical slice runtime adapter could not write the complete RGBA capture.");
        }
    }

    void VerticalSliceRuntimeAdapter::writeCaptureMetadata(
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
        prepareVerticalSliceOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Vertical slice runtime adapter could not open the metadata output path.");
        }

        output << "{\n"
               << "  \"caseId\": \"" << options.caseId.c_str() << "\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
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

    void VerticalSliceRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }

        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareVerticalSliceOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Vertical slice runtime adapter could not open the snapshot output path.");
        }

        if (isSimpleRenderClassSlice(options))
        {
            output << "{\n"
                   << "  \"caseId\": \"simple-render-class\",\n"
                   << "  \"sceneRenderSetCount\": 0,\n"
                   << "  \"renderableObjectCount\": 1,\n"
                   << "  \"instanceCount\": 1,\n"
                   << "  \"containsHierarchy\": false,\n"
                   << "  \"materialCount\": 1,\n"
                   << "  \"sceneRenderClassCount\": 1,\n"
                   << "  \"scenePassCount\": 1,\n"
                   << "  \"screenPassCount\": 0,\n"
                   << "  \"drawCommandCount\": 1,\n"
                   << "  \"explicitVertexCount\": 3,\n"
                   << "  \"gpuWorkDslOnly\": true\n"
                   << "}\n";
            return;
        }

        output << "{\n"
               << "  \"caseId\": \"fullscreen-postprocess\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"screenPassRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"sceneRenderClassCount\": 1,\n"
               << "  \"screenRenderClassCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 1,\n"
               << "  \"drawCommandCount\": 2,\n"
               << "  \"sceneOffscreenOutput\": true,\n"
               << "  \"fullscreenTriangle\": true,\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
