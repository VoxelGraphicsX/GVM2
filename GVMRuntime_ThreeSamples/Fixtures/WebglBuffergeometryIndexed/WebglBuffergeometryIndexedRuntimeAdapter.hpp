#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglBuffergeometryIndexedVertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the deterministic r185 indexed grid and GUI state to the private DSL renderer. */
    class WebglBuffergeometryIndexedRuntimeAdapter final
    {
    public:
        /** Builds the fixed CPU scene and uploads it only through generated DSL methods. */
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
                wireVertices,
                modelViewProjection,
                wireframeEnabled);
        }

        /** Keeps the target-frame geometry and material mode immutable during host warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the DSL output and writes capture metadata plus structural evidence. */
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
        /** Validates the locked scenario and constructs shared plus wire-expanded geometry. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes the exact tightly packed RGBA8 payload returned by the GPU queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic capture identity and replay provenance. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the RenderSet-free Scene topology and source-geometry invariants. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglBuffergeometryIndexedVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebglBuffergeometryIndexedWireVertex> wireVertices;
        glm::mat4 modelViewProjection{1.0f};
        eastl::string replaySha256;
        double timeSeconds = 0.0;
        double rotationX = 0.0;
        double rotationY = 0.0;
        bool wireframeEnabled = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
