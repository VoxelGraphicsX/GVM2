#pragma once

#include "ExrImageDecoder.hpp"
#include "GifImageDecoder.hpp"
#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglMaterialsMatcapData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the pinned Lee Perry Smith assets to the dedicated matcap renderer. */
    class WebglMaterialsMatcapRuntimeAdapter final
    {
    public:
        /** Decodes all assets and uploads the selected canonical material state. */
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
                exrMatcap.pixels,
                exrMatcap.width,
                exrMatcap.height,
                droppedMatcapMips,
                droppedMatcap.width,
                droppedMatcap.height,
                normalMips,
                normalMap.width,
                normalMap.height,
                uniforms);
        }

        /** Keeps this event-driven upstream sample immutable during warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes loader, asset, and single-draw evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases decoded CPU assets after generated renderer teardown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and prepares exact CPU-side scene inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMaterialsMatcapVertex> vertices;
        eastl::vector<uint> indices;
        RgbaHalfImageData exrMatcap;
        RgbaImageData droppedMatcap;
        RgbaImageData normalMap;
        eastl::vector<eastl::vector<uint8_t>> droppedMatcapMips;
        eastl::vector<eastl::vector<uint8_t>> normalMips;
        WebglMaterialsMatcapUniforms uniforms;
        eastl::string glbSha256;
        eastl::string exrSha256;
        eastl::string normalSha256;
        eastl::string decodedNormalSha256;
        eastl::string decodedNormalFlippedSha256;
        eastl::string droppedSha256;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
