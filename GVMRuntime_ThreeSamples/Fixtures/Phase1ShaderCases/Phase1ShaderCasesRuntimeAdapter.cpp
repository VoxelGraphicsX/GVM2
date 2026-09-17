#include "Phase1ShaderCasesRuntimeAdapter.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        /** Creates parent directories for one explicitly requested shader-case artifact. */
        void preparePhase1ShaderCaseOutputPath(const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(outputPath.parent_path());
            }
        }

        /** Computes the tightly packed RGBA8 byte count while rejecting overflow. */
        uint64_t computePhase1ShaderCaseRgbaByteCount(uint32_t width, uint32_t height)
        {
            constexpr uint64_t BytesPerPixel = 4u;
            const uint64_t pixelCount = uint64_t(width) * uint64_t(height);
            if (pixelCount > std::numeric_limits<uint64_t>::max() / BytesPerPixel)
            {
                throw std::overflow_error("Phase 1 shader-case RGBA8 size overflowed uint64_t.");
            }
            return pixelCount * BytesPerPixel;
        }

        /** Returns true when the options select one locked webgl_shader scenario. */
        bool isWebglShaderScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_shader")
            {
                return false;
            }
            return (options.scenarioId == "initial" && options.targetFrame == 0u) ||
                   (options.scenarioId == "animated" && options.targetFrame == 60u);
        }

        /** Returns true when the options select one locked attribute-free geometry scenario. */
        bool isWebglBuffergeometryAttributesNoneScenario(const ThreeSampleHostOptions &options)
        {
            if (options.caseId != "webgl_buffergeometry_attributes_none")
            {
                return false;
            }
            return (options.scenarioId == "initial" && options.targetFrame == 0u) ||
                   (options.scenarioId == "rotated" && options.targetFrame == 60u);
        }
    } // namespace

    void Phase1ShaderCasesRuntimeAdapter::initialize(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        if (options.width != 800u || options.height != 500u)
        {
            throw std::invalid_argument(
                "Phase 1 shader cases require the locked 800x500 capture extent.");
        }
        if (options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument(
                "Phase 1 shader cases require the shared default deterministic random seed.");
        }
        if (!isWebglShaderScenario(options) &&
            !isWebglBuffergeometryAttributesNoneScenario(options))
        {
            throw std::invalid_argument(
                "Phase 1 shader runtime adapter received an unlocked case, scenario, or frame.");
        }
        device = inDevice;
    }

    void Phase1ShaderCasesRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void Phase1ShaderCasesRuntimeAdapter::afterFrame(
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

        const uint64_t byteCount = computePhase1ShaderCaseRgbaByteCount(width, height);
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Phase 1 shader-case RGBA8 capture exceeds host addressable storage.");
        }

        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto graphicsQueue = device->graphicsQueue(0);
        if (!graphicsQueue)
        {
            throw std::runtime_error(
                "Phase 1 shader runtime adapter could not access the graphics queue.");
        }
        graphicsQueue
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();

        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void Phase1ShaderCasesRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
    }

    void Phase1ShaderCasesRuntimeAdapter::writeRgbaCapture(
        const ThreeSampleHostOptions &options,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty())
        {
            return;
        }

        const std::filesystem::path outputPath(options.captureRgbaPath.c_str());
        preparePhase1ShaderCaseOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Phase 1 shader runtime adapter could not open the RGBA output path.");
        }
        output.write(
            reinterpret_cast<const char *>(rgba.data()),
            static_cast<std::streamsize>(rgba.size()));
        if (!output)
        {
            throw std::runtime_error(
                "Phase 1 shader runtime adapter could not write the complete RGBA capture.");
        }
    }

    void Phase1ShaderCasesRuntimeAdapter::writeCaptureMetadata(
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
        preparePhase1ShaderCaseOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Phase 1 shader runtime adapter could not open the metadata output path.");
        }
        output << "{\n"
               << "  \"caseId\": \"" << options.caseId.c_str() << "\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
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

    void Phase1ShaderCasesRuntimeAdapter::writeStructuralSnapshot(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty())
        {
            return;
        }

        const std::filesystem::path outputPath(options.sceneSnapshotPath.c_str());
        preparePhase1ShaderCaseOutputPath(outputPath);
        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Phase 1 shader runtime adapter could not open the snapshot output path.");
        }
        output << std::fixed << std::setprecision(8);

        if (isWebglShaderScenario(options))
        {
            output << "{\n"
                   << "  \"caseId\": \"webgl_shader\",\n"
                   << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
                   << "  \"frame\": " << frameIndex << ",\n"
                   << "  \"sceneRenderSetCount\": 0,\n"
                   << "  \"renderableObjectCount\": 1,\n"
                   << "  \"instanceCount\": 1,\n"
                   << "  \"screenPassCount\": 1,\n"
                   << "  \"drawCommandCount\": 1,\n"
                   << "  \"explicitVertexCount\": 3,\n"
                   << "  \"timeSeconds\": " << (double(frameIndex) / 60.0) << ",\n"
                   << "  \"fixedStepSeconds\": " << (1.0 / 60.0) << ",\n"
                   << "  \"gpuWorkDslOnly\": true\n"
                   << "}\n";
            return;
        }

        const double timeSeconds = double(frameIndex) / 60.0;
        output << "{\n"
               << "  \"caseId\": \"webgl_buffergeometry_attributes_none\",\n"
               << "  \"scenarioId\": \"" << options.scenarioId.c_str() << "\",\n"
               << "  \"frame\": " << frameIndex << ",\n"
               << "  \"sceneRenderSetCount\": 0,\n"
               << "  \"renderableObjectCount\": 1,\n"
               << "  \"instanceCount\": 1,\n"
               << "  \"scenePassCount\": 1,\n"
               << "  \"logicalScenePassCount\": 1,\n"
               << "  \"screenPassCount\": 0,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"logicalDrawCommandCount\": 1,\n"
               << "  \"computePassCount\": 0,\n"
               << "  \"rasterSampleCount\": 1,\n"
               << "  \"explicitVertexCount\": 30000,\n"
               << "  \"submittedVertexCount\": 30000,\n"
               << "  \"standaloneGeometryBufferCount\": 0,\n"
               << "  \"seed\": 42,\n"
               << "  \"timeSeconds\": " << timeSeconds << ",\n"
               << "  \"rotationX\": " << timeSeconds * 0.25 << ",\n"
               << "  \"rotationY\": " << timeSeconds * 0.50 << ",\n"
               << "  \"fixedStepSeconds\": " << (1.0 / 60.0) << ",\n"
               << "  \"doubleSided\": true,\n"
               << "  \"scenePassSequence\": [\n"
               << "    {\"sceneRoot\":\"scene\",\"scenePass\":\"main\",\"entityOrdinal\":0}\n"
               << "  ],\n"
               << "  \"gpuWorkDslOnly\": true\n"
               << "}\n";
    }
} // namespace GVM::ThreeSamples
