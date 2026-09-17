#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglPostprocessingSobelData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples
{
    /** Connects the exact TorusKnot and Sobel pass chain to the shared host. */
    class WebglPostprocessingSobelRuntimeAdapter final
    {
    public:
        /** Builds the exact CPU geometry and uploads immutable scenario state. */
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
                projectionMatrix,
                modelViewMatrix,
                ambientLight,
                effectEnabled);
        }

        /** Leaves the immutable target-frame state unchanged. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the final DSL texture and writes strict evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU-side TorusKnot storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and builds exact geometry and camera matrices. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8, metadata, structural, and semantic artifacts. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglPostprocessingSobelVertex> vertices;
        eastl::vector<uint32_t> indices;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 modelViewMatrix{1.0f};
        float ambientLight = 0.0f;
        bool effectEnabled = true;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
