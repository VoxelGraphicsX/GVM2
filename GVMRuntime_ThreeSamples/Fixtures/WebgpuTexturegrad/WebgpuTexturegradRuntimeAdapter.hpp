#pragma once

#include "GifImageDecoder.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

namespace GVM::ThreeSamples
{
    /** Connects the exact r185 texture-gradient comparison to the host. */
    class WebgpuTexturegradRuntimeAdapter final
    {
    public:
        /** Decodes the pinned UV grid and configures both dedicated Scene passes. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(
                mipPixels,
                textureWidth,
                textureHeight);
        }

        /** Leaves exact fixed-step time to the DSL renderer. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the target frame and writes strict gate artifacts. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Clears decoded CPU mip storage after generated shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates options, asset identity, decoded extent, and mip count. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8, metadata, and ordered two-Scene snapshots. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<eastl::vector<uint8_t>> mipPixels;
        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
