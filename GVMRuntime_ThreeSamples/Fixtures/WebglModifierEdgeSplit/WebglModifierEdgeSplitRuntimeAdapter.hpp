#pragma once

#include "Phase1ModifierEdgeSplitSimpleData.hpp"
#include "RgbaImageData.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects locked Cerberus assets and r185 EdgeSplit CPU semantics to the generated renderer. */
    class WebglModifierEdgeSplitRuntimeAdapter final
    {
    public:
        /** Builds the selected topology and uploads it to the dedicated ordinary RenderClass. */
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
                uniforms,
                albedoMips,
                albedoWidth,
                albedoHeight);
        }

        /** Keeps the loader-complete canonical state immutable during warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the final target and writes strict capture and topology evidence. */
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
        /** Validates one manifest scenario and prepares exact r185 topology and material data. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglModifierEdgeSplitVertex> vertices;
        eastl::vector<uint> indices;
        WebglModifierEdgeSplitUniforms uniforms;
        eastl::vector<eastl::vector<uint8_t>> albedoMips;
        uint32_t albedoWidth = 0u;
        uint32_t albedoHeight = 0u;
        uint32_t mergedVertexCount = 0u;
        bool flatShading = false;
        bool showMap = false;
        bool captureWritten = false;
    };
}
