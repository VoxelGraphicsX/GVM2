#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects RenderClass vertical slices to deterministic readback and structural snapshots. */
    class VerticalSliceRuntimeAdapter final
    {
    public:
        /** Validates the requested slice and retains the generated device used for test readback. */
        void initialize(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy device,
            const ThreeSampleHostOptions &options);

        /** Performs the intentionally empty CPU-side preparation step before each static frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures the selected DSL-rendered RGBA8 frame and writes its structural snapshot. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing teardown after the generated renderer finishes. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Writes the exact RGBA8 bytes returned by the test-only readback operation. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic metadata for one vertical-slice RGBA8 capture. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the RenderClass and pass topology contract for the selected vertical slice. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options) const;

        GVM::Core::DeviceProxy device;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
