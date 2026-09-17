#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglGeometryColorsLookuptableData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the pinned pressure JSON and deterministic LUT state to its DSL renderer. */
    class WebglGeometryColorsLookuptableRuntimeAdapter final
    {
    public:
        /** Decodes geometry, computes Three-compatible normals/colors, and uploads one scene. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, uniforms);
        }

        /** Leaves the static lookup-table snapshot unchanged during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 plus deterministic asset, scene, and loader evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU-side pressure geometry after generated renderer teardown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and builds its exact CPU-side inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglGeometryColorsLookuptableVertex> vertices;
        WebglGeometryColorsLookuptableUniforms uniforms;
        eastl::string pressureSha256;
        bool captureWritten = false;
    };
}
