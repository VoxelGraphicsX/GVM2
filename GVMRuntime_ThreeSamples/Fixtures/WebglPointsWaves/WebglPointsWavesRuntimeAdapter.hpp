#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglPointsWavesData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Reconstructs deterministic r185 wave, camera, and capture state for the private DSL renderer. */
    class WebglPointsWavesRuntimeAdapter final
    {
    public:
        /** Builds immutable expanded point corners and connects per-frame uniform uploads. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, indices);
            frameUploader = [&renderer](WebglPointsWavesUniforms uniforms) {
                renderer.updateFrame(uniforms);
            };
        }

        /** Advances one exact r185 callback and uploads its wave phase and camera transforms. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures the target RGBA8 frame and writes deterministic structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases host callbacks before the generated renderer destroys its resources. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates explicit host inputs and builds the one immutable expanded grid object. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Returns the exact Three perspective matrix for the fixed 800 by 500 camera. */
        glm::mat4 makeProjectionMatrix(uint32_t width, uint32_t height) const;

        /** Returns the current Three camera view matrix after one 5-percent easing update. */
        glm::mat4 advanceCameraAndMakeView();

        /** Writes one tightly packed RGBA8 payload to the explicit capture path. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes exact frame, clock, camera, and byte-layout metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes ordinary RenderClass, point expansion, resolve, and input invariants. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        using FrameUploader = eastl::function<void(WebglPointsWavesUniforms)>;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglPointsWavesVertex> vertices;
        eastl::vector<uint32_t> indices;
        FrameUploader frameUploader;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        eastl::string inputReplaySha256;
        double cameraX = 0.0;
        double cameraY = 0.0;
        double pointerX = 0.0;
        double pointerY = 0.0;
        double phase = 0.0;
        uint32_t cameraUpdateCount = 0u;
        bool cameraInputScenario = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
