#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglLoaderXyzVertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects deterministic XYZ parsing and fixed scene state to the generated DSL renderer. */
    class WebglLoaderXyzRuntimeAdapter final
    {
    public:
        /** Parses and centers the locked asset, then uploads data through generated DSL methods. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, indices, modelView, projection);
        }

        /** Keeps the manifest-selected fixed frame immutable during host warm-up rendering. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the DSL output and writes capture identity plus a structural loader snapshot. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction starts. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and prepares parsed points, expanded indices, and transforms. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes the exact tightly packed RGBA8 payload returned by the graphics queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic image-layout and capture identity metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the RenderSet-free loader topology and canonical object state. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        /** Writes the canonical loader semantic sidecar required by the Three runner. */
        void writeSemanticSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLoaderXyzVertex> vertices;
        eastl::vector<uint> indices;
        glm::mat4 modelView{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 geometryCenter{0.0f};
        float rotationX = 0.0f;
        float rotationY = 0.0f;
        double timeSeconds = 0.0;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
