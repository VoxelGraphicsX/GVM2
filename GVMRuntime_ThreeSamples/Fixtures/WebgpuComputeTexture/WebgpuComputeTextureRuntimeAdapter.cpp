#include "WebgpuComputeTextureRuntimeAdapter.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t RequiredOutputWidth = 800u;
        constexpr uint32_t RequiredOutputHeight = 500u;

        /** Creates parent directories for one explicitly requested output artifact. */
        void prepareWebgpuComputeTextureOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes the tightly packed RGBA8 byte count while rejecting overflow. */
        uint64_t computeWebgpuComputeTextureRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error(
                    "webgpu_compute_texture RGBA8 capture size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns whether the explicit identifiers select the locked initial scenario. */
        bool isWebgpuComputeTextureInitialScenario(const ThreeSampleHostOptions &options)
        {
            return options.caseId == "webgpu_compute_texture" &&
                   options.scenarioId == "initial";
        }
    } // namespace

    void WebgpuComputeTextureRuntimeAdapter::initialize(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        if (!isWebgpuComputeTextureInitialScenario(options))
        {
            throw std::invalid_argument(
                "WebgpuComputeTexture requires webgpu_compute_texture/initial.");
        }
        if (options.frameCount != 1u || options.targetFrame != 0u)
        {
            throw std::invalid_argument(
                "WebgpuComputeTexture initial scenario requires exactly frame zero.");
        }
        if (options.width != RequiredOutputWidth || options.height != RequiredOutputHeight)
        {
            throw std::invalid_argument(
                "WebgpuComputeTexture comparison requires the fixed 800x500 output extent.");
        }
        device = inDevice;
    }

    void WebgpuComputeTextureRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebgpuComputeTextureRuntimeAdapter::afterFrame(
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

        const uint64_t byteCount = computeWebgpuComputeTextureRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "webgpu_compute_texture RGBA8 capture exceeds host addressable storage.");
        }

        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "WebgpuComputeTexture runtime adapter could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();

        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options);
        captureWritten = true;
    }

    void WebgpuComputeTextureRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void WebgpuComputeTextureRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }

        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        prepareWebgpuComputeTextureOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "WebgpuComputeTexture runtime adapter could not open the RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "WebgpuComputeTexture runtime adapter could not write the complete RGBA capture.");
        }
    }

    void WebgpuComputeTextureRuntimeAdapter::writeCaptureMetadata(
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
        prepareWebgpuComputeTextureOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "WebgpuComputeTexture runtime adapter could not open the metadata output path.");
        }

        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"source\": \"gvm-three-r185\",\n"
               << "  \"caseId\": \"webgpu_compute_texture\",\n"
               << "  \"scenarioId\": \"initial\",\n"
               << "  \"pipeline\": \"" << options.pipeline.c_str() << "\",\n"
               << "  \"backend\": \"" << threeSampleBackendName(options.backend) << "\",\n"
               << "  \"randomSeed\": " << options.randomSeed << ",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"width\": " << width << ",\n"
               << "  \"height\": " << height << ",\n"
               << "  \"rowStrideBytes\": " << uint64_t(width) * 4u << ",\n"
               << "  \"byteCount\": " << byteCount << ",\n"
               << "  \"format\": \"rgba8unorm\"\n"
               << "}\n";
    }

    void WebgpuComputeTextureRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }

        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        prepareWebgpuComputeTextureOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "WebgpuComputeTexture runtime adapter could not open the snapshot output path.");
        }

        output << "{\n"
               << "  \"schemaVersion\": 1,\n"
               << "  \"caseId\": \"webgpu_compute_texture\",\n"
               << "  \"scenarioId\": \"initial\",\n"
               << "  \"frame\": " << options.targetFrame << ",\n"
               << "  \"upstreamRevision\": \"r185\",\n"
               << "  \"upstreamCommit\": \"2431a09f46f34c560bc8e44b33be0e567723d5b9\",\n"
               << "  \"renderSetPolicy\": \"not-required\",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"containsInstancing\": false,\n"
               << "  \"containsHierarchy\": false,\n"
               << "  \"materialCount\": 1,\n"
               << "  \"storageTextureFormat\": \"rgba8unorm\",\n"
               << "  \"storageTextureWidth\": 512,\n"
               << "  \"storageTextureHeight\": 512,\n"
               << "  \"computeInvocationCount\": 262144,\n"
               << "  \"computePassCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"drawVertexCount\": 6,\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"compute-texture-display\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
