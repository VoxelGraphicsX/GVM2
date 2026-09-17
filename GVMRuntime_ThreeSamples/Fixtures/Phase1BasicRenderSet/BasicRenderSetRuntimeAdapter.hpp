#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /**
     * Uploads the deterministic structural fixture declared by one dedicated
     * RenderSet shard and records its capture contract.  The fixture is kept
     * intentionally separate from the eventual sample-specific asset and
     * algorithm implementation; its only purpose is to prove the generated
     * entity/component ABI without making a scaffold look like a strict pass.
     */
    class BasicRenderSetRuntimeAdapter final
    {
    public:
        /** Validates the selected case and allocates its initial RenderSet entities. */
        void initialize(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy device,
            const ThreeSampleHostOptions &options);

        /** Advances the fixed-step scene state without issuing GPU commands. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the DSL target and writes capture metadata and Scene snapshot. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases adapter-owned state after the generated renderer is destroyed. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Writes exact tightly packed RGBA8 bytes to the requested capture path. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes the standard capture metadata schema. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes one-RenderSet structural evidence for the selected shard. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        bool initialized = false;
        bool captureWritten = false;
        uint32_t entityCount = 0u;
        uint32_t instancedEntityCount = 0u;
        uint32_t instanceCount = 1u;
    };
} // namespace GVM::ThreeSamples
