#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglMultipleScenesComparisonData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Connects deterministic two-Scene geometry and interaction state to the generated DSL renderer. */
    class WebglMultipleScenesComparisonRuntimeAdapter final
    {
    public:
        /** Builds both simple Scene geometries and uploads their canonical state. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScenes(solidVertices, solidIndices, wireVertices, wireIndices,
                                     modelViewProjection[0][0], modelViewProjection[0][1], modelViewProjection[0][2], modelViewProjection[0][3],
                                     modelViewProjection[1][0], modelViewProjection[1][1], modelViewProjection[1][2], modelViewProjection[1][3],
                                     modelViewProjection[2][0], modelViewProjection[2][1], modelViewProjection[2][2], modelViewProjection[2][3],
                                     modelViewProjection[3][0], modelViewProjection[3][1], modelViewProjection[3][2], modelViewProjection[3][3],
                                     hemisphereDirection.x, hemisphereDirection.y, hemisphereDirection.z,
                                     sliderPosition);
        }

        /** Keeps the capture state immutable across deterministic warm-up frames. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the DSL output and writes metadata plus the two-Scene structural snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing teardown after renderer destruction starts. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the selected scenario and constructs exact detail-3 geometry. */
        void initializeResources(GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Writes one tightly packed RGBA8 capture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic capture identity and replay evidence. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the RenderSet-free two-Scene runtime contract. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMultipleScenesSolidVertex> solidVertices;
        eastl::vector<uint32_t> solidIndices;
        eastl::vector<WebglMultipleScenesWireVertex> wireVertices;
        eastl::vector<uint32_t> wireIndices;
        glm::mat4 modelViewProjection{1.0f};
        glm::vec4 hemisphereDirection{0.0f};
        float sliderPosition = 400.0f;
        double cameraX = 0.0;
        double cameraY = 0.0;
        double cameraZ = 6.0;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
