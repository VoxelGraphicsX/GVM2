#pragma once

#include "GifImageDecoder.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

namespace GVM::ThreeSamples
{
    /** Connects the dedicated partial-update renderer to the shared host. */
    class WebgpuTexturesPartialUpdateRuntimeAdapter final
    {
    public:
        /** Validates and decodes the pinned carbon texture before DSL upload. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureCarbonTexture(carbonPixels);
        }

        /** Leaves the fixed update cadence to the dedicated DSL renderer. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected frame and writes strict structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Clears decoded CPU texture storage after generated shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the exact Manifest scenario and pinned PNG identity. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8 capture, metadata, and ordinary-scene evidence. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<uint8_t> carbonPixels;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
