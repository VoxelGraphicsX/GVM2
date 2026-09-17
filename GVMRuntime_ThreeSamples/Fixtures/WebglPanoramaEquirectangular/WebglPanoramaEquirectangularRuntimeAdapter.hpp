#pragma once

#include "GifImageDecoder.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

namespace GVM::ThreeSamples
{
    /** Connects the exact r185 panorama material to the shared sample host. */
    class WebglPanoramaEquirectangularRuntimeAdapter final
    {
    public:
        /** Decodes the pinned JPEG and configures the selected camera scenario. */
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
                baseLongitude,
                latitude,
                fieldOfView);
        }

        /** Leaves the exact fixed-step longitude update to the DSL renderer. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the target frame and writes formal gate evidence. */
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
        /** Validates options, replay identity, JPEG identity, and mip count. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8, metadata, structural, and loader semantic artifacts. */
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
        float baseLongitude = 0.0f;
        float latitude = 0.0f;
        float fieldOfView = 75.0f;
        bool hasReplay = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
