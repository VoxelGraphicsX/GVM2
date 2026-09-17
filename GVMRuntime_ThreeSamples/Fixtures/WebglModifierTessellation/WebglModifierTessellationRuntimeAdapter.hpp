#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "Phase1ModifierTessellationSimpleData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples
{
    /** Connects the deterministic tessellated text asset and Trackball state to the generated DSL renderer. */
    class WebglModifierTessellationRuntimeAdapter final
    {
    public:
        /** Loads the canonical CPU vertex stream and uploads the selected fixed-frame state. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices,
                                    modelViewProjection[0][0], modelViewProjection[0][1], modelViewProjection[0][2], modelViewProjection[0][3],
                                    modelViewProjection[1][0], modelViewProjection[1][1], modelViewProjection[1][2], modelViewProjection[1][3],
                                    modelViewProjection[2][0], modelViewProjection[2][1], modelViewProjection[2][2], modelViewProjection[2][3],
                                    modelViewProjection[3][0], modelViewProjection[3][1], modelViewProjection[3][2], modelViewProjection[3][3],
                                    amplitude);
        }

        /** Keeps the selected deterministic capture state immutable during warm-up frames. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the DSL output and writes capture identity plus the simple-Scene snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction begins. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and reconstructs its CPU-side state. */
        void initializeResources(GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Writes one tightly packed RGBA8 readback payload. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic frame, random-stream, and replay metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the ordinary RenderClass topology and canonical CPU-asset evidence. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglModifierTessellationVertex> vertices;
        glm::mat4 modelViewProjection{1.0f};
        float amplitude = 1.0f;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
