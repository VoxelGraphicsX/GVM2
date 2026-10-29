#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Captures deterministic r185 shader-case frames and their structural contracts. */
    class Phase1ShaderCasesRuntimeAdapter final
    {
    public:
        /** Validates the case/scenario contract and retains the generated device for readback. */
        void initialize(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy device,
            const ThreeSampleHostOptions &options);

        /** Preserves the renderer-owned fixed-step state before one deterministic frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected DSL output and writes metadata plus a structural snapshot. */
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
        /** Writes the exact tightly packed RGBA8 payload returned by the GPU queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic dimensions, frame identity, and format metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the RenderSet-free topology and canonical state for the selected case. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
