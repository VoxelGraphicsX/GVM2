#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglBuffergeometryLinesVertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects deterministic C++ line data to the generated r185 DSL renderer. */
    class WebglBuffergeometryLinesRuntimeAdapter final
    {
    public:
        /** Prepares the base and morph vertices and uploads them through the generated DSL interface. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, modelViewProjection, morphWeight);
        }

        /** Preserves the target-frame state selected before deterministic warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected DSL output and writes metadata plus a structural snapshot. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing teardown after the generated renderer is destroyed. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and constructs its immutable CPU payload. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes the exact tightly packed RGBA8 payload returned by the GPU queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic dimensions, frame identity, and format metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the RenderSet-free topology and canonical morph state. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglBuffergeometryLinesVertex> vertices;
        glm::mat4 modelViewProjection{1.0f};
        uint32_t finalRandomState = 0u;
        float timeSeconds = 0.0f;
        float rotationX = 0.0f;
        float rotationY = 0.0f;
        float morphWeight = 0.0f;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
