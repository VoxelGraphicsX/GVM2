#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

namespace GVM::ThreeSamples
{
    /** Connects the dedicated procedural-texture DSL chain to the shared host. */
    class WebgpuProceduralTextureRuntimeAdapter final
    {
    public:
        /** Validates one Manifest scenario and configures its exact GUI state. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScenario(
                uvScale,
                blurAmount,
                autoUpdate);
        }

        /** Leaves deterministic update cadence to the DSL renderer. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the target frame and writes strict structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases adapter-owned capture state after generated shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates locked options and optional canonical GUI replay bytes. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8 capture, metadata, and no-RenderSet scene evidence. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        float uvScale = 4.0f;
        float blurAmount = 0.5f;
        bool autoUpdate = true;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
