#pragma once

#include "GifImageDecoder.hpp"
#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglMaterialsNormalmapData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the pinned Lee Perry Smith assets to the complete normal-map pipeline. */
    class WebglMaterialsNormalmapRuntimeAdapter final
    {
    public:
        /** Decodes geometry and textures, then uploads one canonical scenario. */
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
                diffuseMips,
                diffuse.width,
                diffuse.height,
                specularMips,
                specular.width,
                specular.height,
                normalMips,
                normal.width,
                normal.height,
                uniforms);
        }

        /** Keeps the deterministic target-frame material and camera immutable. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures final RGBA8 plus structural, loader, and asset evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases decoded CPU-side geometry and image storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and prepares all exact CPU inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMaterialsNormalmapVertex> vertices;
        eastl::vector<uint> indices;
        RgbaImageData diffuse;
        RgbaImageData specular;
        RgbaImageData normal;
        eastl::vector<eastl::vector<uint8_t>> diffuseMips;
        eastl::vector<eastl::vector<uint8_t>> specularMips;
        eastl::vector<eastl::vector<uint8_t>> normalMips;
        WebglMaterialsNormalmapUniforms uniforms;
        eastl::string replaySha256;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
