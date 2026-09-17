#pragma once

#include "Phase1LoaderVoxSimpleData.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects locked VOX decoding, greedy meshing, and Orbit state to the generated renderer. */
    class WebglLoaderVoxRuntimeAdapter final
    {
    public:
        /** Decodes the asset and uploads the one deterministic triangle mesh. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, indices, uniforms);
        }

        /** Keeps the selected canonical camera immutable during host warm-up frames. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the final target and writes capture and loader evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction starts. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and builds exact r185 greedy geometry. */
        void initializeResources(GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLoaderVoxVertex> vertices;
        eastl::vector<uint> indices;
        WebglLoaderVoxUniforms uniforms;
        uint32_t voxelCount = 0u;
        uint32_t quadCount = 0u;
        bool captureWritten = false;
    };
}
