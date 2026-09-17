#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebgpuTslRagingSeaData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the four locked r185 raging-sea scenarios to the dedicated DSL renderer. */
    class WebgpuTslRagingSeaRuntimeAdapter final
    {
    public:
        /** Builds exact PlaneGeometry and uploads the selected deterministic frame state. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureOutput(options.width, options.height);
            renderer.configureScene(
                vertices, indices, dfgLutPackedPixels, uniforms);
        }

        /** Keeps the target-frame uniforms immutable throughout deterministic warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures final RGBA8 and the complete ordinary-Scene contract evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU-side deterministic geometry storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and prepares its exact camera and material state. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuTslRagingSeaVertex> vertices;
        eastl::vector<uint> indices;
        eastl::vector<uint> dfgLutPackedPixels;
        WebgpuTslRagingSeaUniforms uniforms = {};
        eastl::string replaySha256;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
