#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the three locked r185 compute-reduction scenarios to the dedicated DSL renderer. */
    class WebgpuComputeReduceRuntimeAdapter final
    {
    public:
        /** Validates one deterministic scenario and selects both independent reduction algorithms. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureOutput(options.width, options.height);
            renderer.configureScenario(
                leftAlgorithm,
                rightAlgorithm,
                executeReduction);
        }

        /** Leaves the capture-time reduction and display state immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 plus both Scene and reduction-algorithm evidence artifacts. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases the retained device reference and replay identity. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates dimensions, frame, seed, and replay identity for one Manifest scenario. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::string replaySha256;
        uint32_t leftAlgorithm = 0u;
        uint32_t rightAlgorithm = 4u;
        bool executeReduction = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
