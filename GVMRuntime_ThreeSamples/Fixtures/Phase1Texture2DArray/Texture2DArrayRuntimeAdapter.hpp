#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

namespace GVM::ThreeSamples
{
    /** Connects either dedicated r185 texture-array renderer to the shared host. */
    class Texture2DArrayRuntimeAdapter final
    {
    public:
        /** Validates and inflates the pinned R8 volume before DSL texture upload. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureVolume(voxels);
        }

        /** Leaves the fixed animation clock to the dedicated DSL renderer. */
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

        /** Clears decoded CPU volume storage after generated resource shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the Manifest contract, ZIP identity, and volume payload. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes capture metadata plus ordinary-RenderClass scene evidence. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<uint8_t> voxels;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
