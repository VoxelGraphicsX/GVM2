#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglBuffergeometrySelectiveDrawVertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the deterministic r185 selective line scene to its private DSL renderer. */
    class WebglBuffergeometrySelectiveDrawRuntimeAdapter final
    {
    public:
        /** Builds CPU line data and uploads it only through generated DSL methods. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(
                initialVertices,
                projectionMatrix,
                modelViewMatrix);
            if (cullingApplied)
            {
                renderer.updateVisibilityVertices(vertices);
            }
        }

        /** Keeps target-frame geometry and visibility immutable during host warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the DSL output and writes metadata plus structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction begins. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and constructs triangle-expanded line geometry. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes the exact tightly packed RGBA8 payload returned by the GPU queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic capture identity and hide-replay provenance. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the RenderSet-free Scene and expanded-line invariants. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglBuffergeometrySelectiveDrawVertex> initialVertices;
        eastl::vector<WebglBuffergeometrySelectiveDrawVertex> vertices;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 modelViewMatrix{1.0f};
        eastl::string replaySha256;
        double timeSeconds = 0.0;
        double rotationX = 0.0;
        double rotationY = 0.0;
        uint32_t visibleLineCount = 0u;
        uint32_t culledLineCount = 0u;
        bool cullingApplied = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
