#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects deterministic RGBE decoding and exposure state to the generated HDR renderer. */
    class WebglLoaderTextureHdrRuntimeAdapter final
    {
    public:
        /** Decodes the locked asset and uploads its half-float texels through generated methods. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(halfRgba, imageWidth, imageHeight, exposure);
        }

        /** Keeps the manifest-selected exposure immutable during host warm-up frames. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the final target and emits capture plus loader metadata. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction starts. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and prepares the decoded half-float image. */
        void initializeResources(GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<uint16_t> halfRgba;
        uint32_t imageWidth = 0u;
        uint32_t imageHeight = 0u;
        float exposure = 2.0f;
        bool captureWritten = false;
    };
}
