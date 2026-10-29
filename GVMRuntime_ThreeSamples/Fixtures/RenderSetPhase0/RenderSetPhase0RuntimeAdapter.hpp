#pragma once

#include "RenderSetPhase0FixtureController.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the generated RenderSet Phase 0 renderer to deterministic host mutation and capture. */
    class RenderSetPhase0RuntimeAdapter final
    {
    public:
        /** Stores the generated device and allocates the fixture's initial RenderSet entities. */
        void initialize(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy device,
            const ThreeSampleHostOptions &options);

        /** Applies the fixture's remove-and-reallocate command before its deterministic mutation frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads and writes the selected deterministic RGBA8 frame after DSL rendering completes. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing adapter teardown after every requested frame has completed. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Writes exact RGBA8 bytes returned by the test-only readback queue operation. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic metadata that describes the raw RGBA8 capture. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        GVM::Core::DeviceProxy device;
        RenderSetPhase0FixtureController controller;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
