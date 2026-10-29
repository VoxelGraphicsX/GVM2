#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglMorphtargetsHorseData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the pinned Horse GLB to its dedicated ordinary DSL renderer. */
    class WebglMorphtargetsHorseRuntimeAdapter final
    {
    public:
        /** Decodes the fixed GLB and uploads one deterministic camera/clip snapshot. */
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
                morphPositions,
                uniforms);
        }

        /** Leaves the already evaluated fixed snapshot immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes RGBA8, loader semantics, and ordinary-draw scene evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases decoded GLB arrays after generated renderer teardown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and prepares exact CPU-side inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMorphtargetsHorseVertex> vertices;
        eastl::vector<uint> indices;
        eastl::vector<float4> morphPositions;
        WebglMorphtargetsHorseUniforms uniforms;
        eastl::string glbSha256;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
