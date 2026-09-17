#pragma once

#include "GifImageDecoder.hpp"
#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglMaterialsNormalmapObjectSpaceData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the pinned Nefertiti GLB to its dedicated object-space normal renderer. */
    class WebglMaterialsNormalmapObjectSpaceRuntimeAdapter final
    {
    public:
        /** Decodes the mesh and embedded images, then uploads the selected camera state. */
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
                baseColorMips,
                objectNormalMips,
                baseColor.width,
                baseColor.height,
                uniforms);
        }

        /** Keeps the target-frame camera immutable during host warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes dual-pass and loader evidence. */
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
        eastl::vector<WebglMaterialsNormalmapObjectSpaceVertex> vertices;
        eastl::vector<uint> indices;
        RgbaImageData baseColor;
        RgbaImageData objectNormal;
        eastl::vector<eastl::vector<uint8_t>> baseColorMips;
        eastl::vector<eastl::vector<uint8_t>> objectNormalMips;
        WebglMaterialsNormalmapObjectSpaceUniforms uniforms;
        eastl::string baseColorPixelsSha256;
        eastl::string objectNormalPixelsSha256;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
