#pragma once

#include "GifImageDecoder.hpp"
#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglMaterialsBumpmapData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the locked Lee Perry Smith GLB and height map to its dedicated DSL renderer. */
    class WebglMaterialsBumpmapRuntimeAdapter final
    {
    public:
        /** Decodes the locked assets and uploads one immutable canonical Scene state. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureOutput(options.width, options.height);
            renderer.configureScene(
                vertices,
                indices,
                heightMips,
                heightImage.width,
                heightImage.height,
                uniforms);
        }

        /** Keeps the pre-evaluated target-frame camera and material state immutable. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and emits loader, pass, and single-sample evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all decoded CPU-side asset storage after renderer teardown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and prepares exact CPU-side Scene inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMaterialsBumpmapVertex> vertices;
        eastl::vector<uint> indices;
        RgbaImageData heightImage;
        eastl::vector<eastl::vector<uint8_t>> heightMips;
        WebglMaterialsBumpmapUniforms uniforms;
        bool captureWritten = false;
    };
}
