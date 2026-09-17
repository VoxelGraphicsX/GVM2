#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the decoded Lee Perry Smith GLB to the dedicated jelly Renderer. */
    class WebgpuComputeGeometryRuntimeAdapter final
    {
    public:
        /** Decodes the locked GLB and uploads all ordinary Scene resources. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(
                positions,
                indices,
                positions,
                normals,
                modelViewProjection[0u],
                modelViewProjection[1u],
                modelViewProjection[2u],
                modelViewProjection[3u],
                modelView[0u],
                modelView[1u],
                modelView[2u],
                modelView[3u],
                pointerPosition,
                physics,
                1.0f);
        }

        /** Keeps the locked jelly controls immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures final RGBA8 and writes loader and Compute evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases decoded CPU arrays after generated resources are destroyed. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and decodes its canonical GLB asset. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<glm::vec4> positions;
        eastl::vector<glm::vec4> normals;
        eastl::vector<uint32_t> indices;
        glm::mat4 modelViewProjection{1.0f};
        glm::mat4 modelView{1.0f};
        glm::vec4 pointerPosition{};
        glm::vec4 physics{};
        bool captureWritten = false;
    };
}
