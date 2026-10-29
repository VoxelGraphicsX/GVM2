#pragma once

#include "GifImageDecoder.hpp"
#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglShaderLavaData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects pinned lava assets and fixed-frame Torus state to the dedicated DSL renderer. */
    class WebglShaderLavaRuntimeAdapter final
    {
    public:
        /** Generates Torus geometry, decodes explicit mips, and uploads one canonical scene. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, indices,
                                    cloudMips, cloudWidth, cloudHeight,
                                    lavaMips, lavaWidth, lavaHeight, uniforms);
        }

        /** Leaves the already evaluated fixed-frame lava state immutable. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Writes final RGBA8 plus asset, pass, and loader evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex, GVM::RHI::Texture readbackTexture,
                        uint32_t width, uint32_t height);

        /** Releases decoded image mips and generated CPU geometry. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and prepares exact deterministic inputs. */
        void initializeResources(GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglShaderLavaVertex> vertices;
        eastl::vector<uint> indices;
        eastl::vector<eastl::vector<uint8_t>> cloudMips;
        eastl::vector<eastl::vector<uint8_t>> lavaMips;
        uint32_t cloudWidth = 0u;
        uint32_t cloudHeight = 0u;
        uint32_t lavaWidth = 0u;
        uint32_t lavaHeight = 0u;
        WebglShaderLavaUniforms uniforms;
        bool captureWritten = false;
    };
}
