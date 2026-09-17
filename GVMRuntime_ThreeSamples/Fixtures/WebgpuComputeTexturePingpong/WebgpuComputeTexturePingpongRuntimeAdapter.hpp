#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebgpuComputeTexturePingpongData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

namespace GVM::ThreeSamples
{
    /** Connects the exact r185 ping-pong compute renderer to the test host. */
    class WebgpuComputeTexturePingpongRuntimeAdapter final
    {
    public:
        /** Uploads one ordinary plane and validates the selected scenario. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configurePlane(vertices, indices);
        }

        /** Leaves phase advancement to the deterministic DSL renderer. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the target frame and writes strict structural evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Clears CPU plane data after generated resource destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the fixed scenario and builds its ordinary plane. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8 capture, metadata, and no-RenderSet evidence. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<PingpongPlaneVertex> vertices;
        eastl::vector<uint32_t> indices;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
