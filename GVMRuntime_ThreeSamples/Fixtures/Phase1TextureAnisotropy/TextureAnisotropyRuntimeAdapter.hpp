#pragma once

#include "GifImageDecoder.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

namespace GVM::ThreeSamples
{
    /** Connects either dedicated r185 anisotropy renderer to the sample host. */
    class TextureAnisotropyRuntimeAdapter final
    {
    public:
        /** Decodes the pinned crate mip chain and applies the selected input state. */
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
                textureHeight,
                targetMouseX,
                targetMouseY,
                clearGapToBackground);
        }

        /** Leaves exact per-frame camera easing to the selected DSL renderer. */
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
        /** Validates case, scenario, replay, asset identity, and mip count. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8, metadata, structural, and loader snapshots. */
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
        float targetMouseX = 0.0f;
        float targetMouseY = 0.0f;
        bool clearGapToBackground = false;
        bool hasReplay = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
