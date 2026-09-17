#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Captures the DSL-only webgpu_compute_texture frame and its structural contract. */
    class WebgpuComputeTextureRuntimeAdapter final
    {
    public:
        /** Validates the fixed r185 scenario and retains the device used only for readback. */
        void initialize(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy device,
            const ThreeSampleHostOptions &options);

        /** Performs no CPU-side scene or GPU preparation before the static frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected RGBA8 target and writes deterministic capture artifacts. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction begins. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Writes the exact tightly packed RGBA8 bytes returned by the readback queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes the explicit capture dimensions, backend, seed, and byte layout. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the no-RenderSet compute-plus-plane structural snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options) const;

        GVM::Core::DeviceProxy device;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
