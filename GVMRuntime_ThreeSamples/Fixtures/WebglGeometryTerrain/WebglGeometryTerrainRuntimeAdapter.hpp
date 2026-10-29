#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Connects exact CPU heightfield data to the dedicated terrain Renderer. */
    class WebglGeometryTerrainRuntimeAdapter final
    {
    public:
        /** Builds r185 PlaneGeometry, heights, camera replay, and Compute inputs. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(
                positions,
                textureCoordinates,
                indices,
                heights,
                grain,
                modelViewProjection[0u],
                modelViewProjection[1u],
                modelViewProjection[2u],
                modelViewProjection[3u],
                modelView[0u],
                modelView[1u],
                modelView[2u],
                modelView[3u],
                float(options.targetFrame) / 60.0f);
        }

        /** Leaves fixed CPU camera and height data immutable across warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures the final RGBA8 image and terrain contract evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU heightfield and topology storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and prepares its deterministic inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<glm::vec4> positions;
        eastl::vector<glm::vec4> textureCoordinates;
        eastl::vector<uint32_t> indices;
        eastl::vector<uint32_t> heights;
        eastl::vector<uint32_t> grain;
        glm::mat4 modelView{1.0f};
        glm::mat4 modelViewProjection{1.0f};
        bool captureWritten = false;
    };
}
