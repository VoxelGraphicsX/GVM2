#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects locked procedural GUI replays to the generated fullscreen DSL renderer. */
    class WebglPostprocessingProceduralRuntimeAdapter final
    {
    public:
        /** Validates the selected scenario and configures its replay-derived noise mode. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeScenario(inDevice, options);
            renderer.configureNoiseMode(noiseDimension);
        }

        /** Leaves the immutable procedural mode unchanged during deterministic warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected RGBA8 output and writes replay-aware capture artifacts. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing teardown after the generated renderer is destroyed. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Locks the case, frame, replay identity, and renderer mode before GPU execution. */
        void initializeScenario(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes the exact tightly packed RGBA8 bytes returned by the graphics queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes dimensions, selected noise mode, and canonical replay identity. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the RenderSet-free fullscreen topology and locked procedural state. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::string noiseMode;
        eastl::string canonicalState;
        eastl::string replaySha256;
        eastl::string replayScenarioId;
        eastl::string replayTarget;
        uint32_t noiseDimension = 3u;
        uint32_t replaySchemaVersion = 0u;
        uint32_t replayCaptureFrame = 0u;
        uint32_t replayEventCount = 0u;
        bool usesInputReplay = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
