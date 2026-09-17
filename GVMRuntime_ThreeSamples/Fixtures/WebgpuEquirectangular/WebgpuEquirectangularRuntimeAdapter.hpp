#pragma once

#include "GifImageDecoder.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

namespace GVM::ThreeSamples
{
    /** Connects the dedicated equirectangular background renderer to the host. */
    class WebgpuEquirectangularRuntimeAdapter final
    {
    public:
        /** Decodes the pinned JPEG and uploads its complete explicit mip chain. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(
                mipPixels,
                panoramaWidth,
                panoramaHeight,
                backgroundIntensity,
                manualThetaOffset,
                manualPhiOffset);
        }

        /** Leaves fixed-step orbit advancement to the dedicated DSL renderer. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the requested frame and writes strict structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Clears decoded CPU image storage after generated resource shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, asset identity, replay, and decoded mips. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8 capture, metadata, and the screen-only Scene snapshot. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<eastl::vector<uint8_t>> mipPixels;
        uint32_t panoramaWidth = 0u;
        uint32_t panoramaHeight = 0u;
        float backgroundIntensity = 1.0f;
        float manualThetaOffset = 0.0f;
        float manualPhiOffset = 0.0f;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
