#pragma once

#include "ExrImageDecoder.hpp"
#include "Phase1LoaderTextureExrSimpleData.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the locked memorial EXR and exposure scenarios to the generated renderer. */
    class WebglLoaderTextureExrRuntimeAdapter final
    {
    public:
        /** Decodes the linear image and uploads one canonical display plane. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(
                vertices,
                indices,
                image.pixels,
                image.width,
                image.height,
                uniforms);
        }

        /** Keeps the selected exposure immutable during warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the final target and writes capture and loader evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing teardown after renderer destruction starts. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one manifest scenario and decodes the exact pinned EXR payload. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLoaderTextureExrVertex> vertices;
        eastl::vector<uint> indices;
        RgbaHalfImageData image;
        WebglLoaderTextureExrUniforms uniforms;
        bool captureWritten = false;
    };
}
