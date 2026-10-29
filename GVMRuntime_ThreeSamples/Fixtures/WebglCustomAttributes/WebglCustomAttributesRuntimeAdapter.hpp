#pragma once

#include "ThreeCompat/DeterministicRandom.hpp"
#include "ThreeSampleHostOptions.hpp"
#include "WebglCustomAttributesVertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/functional.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Advances the deterministic r185 custom-attribute scene and feeds its private DSL renderer. */
    class WebglCustomAttributesRuntimeAdapter final
    {
    public:
        /** Builds immutable resources and connects sequential frame uploads to generated DSL methods. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, indices, textureMips);
            frameUploader = [&renderer](
                                eastl::vector<WebglCustomAttributesVertex> &frameVertices,
                                const glm::mat4 &projectionMatrix,
                                const glm::mat4 &modelViewMatrix,
                                float amplitude,
                                const glm::vec3 &color) {
                renderer.updateFrame(
                    frameVertices,
                    projectionMatrix,
                    modelViewMatrix,
                    amplitude,
                    color);
            };
        }

        /** Advances exactly one r185 render callback and uploads its dynamic vertex state. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the target DSL output and writes metadata plus structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases the frame callback without issuing GPU work during teardown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and builds exact geometry, random, texture, and camera state. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Reproduces one Date.now-based render callback including every random-noise draw. */
        void advanceFrameState(uint32_t frameIndex);

        /** Writes the exact tightly packed RGBA8 payload returned by the GPU queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic capture identity and image-layout metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the ordinary indexed Scene and sequential update invariants. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        using FrameUploader = eastl::function<void(
            eastl::vector<WebglCustomAttributesVertex> &,
            const glm::mat4 &,
            const glm::mat4 &,
            float,
            const glm::vec3 &)>;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglCustomAttributesVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<float> noise;
        eastl::vector<eastl::vector<uint8_t>> textureMips;
        ThreeCompat::DeterministicRandom random{0x18500010u};
        FrameUploader frameUploader;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 modelViewMatrix{1.0f};
        glm::vec3 color{1.0f, 0.0f, 0.0f};
        double colorRed = 1.0;
        double colorGreen = 0.0;
        double colorBlue = 0.0;
        double virtualTimeMilliseconds = 0.0;
        double renderedVirtualTimeMilliseconds = 0.0;
        double timeValue = 0.0;
        double rotation = 0.0;
        float amplitude = 0.0f;
        uint32_t initializedRandomState = 0u;
        uint32_t frameUpdateCount = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
