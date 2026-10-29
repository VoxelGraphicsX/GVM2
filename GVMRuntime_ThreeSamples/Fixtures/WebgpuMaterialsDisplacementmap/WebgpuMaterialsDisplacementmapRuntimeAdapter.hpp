#pragma once

#include "GifImageDecoder.hpp"
#include "Host/ThreeSampleHostOptions.hpp"
#include "ThreeR185DfgLutData.hpp"
#include "WebgpuMaterialsDisplacementmapData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the locked ninja OBJ and authored physical maps to its dedicated DSL renderer. */
    class WebgpuMaterialsDisplacementmapRuntimeAdapter final
    {
    public:
        /** Decodes all pinned inputs and configures one immutable target-frame Scene state. */
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
                normalMips,
                aoMips,
                displacementMips,
                cubeMips,
                dfgLutPackedPixels,
                uniforms);
        }

        /** Keeps the pre-evaluated camera, light, and material state immutable. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads RGBA8 and writes strict loader, pass, asset, and sample evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all decoded CPU-side geometry and texture storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and prepares its exact CPU-owned inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuMaterialsDisplacementmapVertex> vertices;
        eastl::vector<uint> indices;
        eastl::vector<eastl::vector<uint8_t>> normalMips;
        eastl::vector<eastl::vector<uint8_t>> aoMips;
        eastl::vector<eastl::vector<uint8_t>> displacementMips;
        eastl::array<eastl::vector<eastl::vector<uint8_t>>, 6u> cubeMips;
        eastl::vector<uint> dfgLutPackedPixels;
        WebgpuMaterialsDisplacementmapUniforms uniforms;
        bool captureWritten = false;
    };
}
