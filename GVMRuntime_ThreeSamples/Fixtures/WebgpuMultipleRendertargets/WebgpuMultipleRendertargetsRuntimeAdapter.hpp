#pragma once

#include "GifImageDecoder.hpp"
#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects TorusKnot CPU geometry and the pinned hardwood JPEG to the true MRT Renderer. */
    class WebgpuMultipleRendertargetsRuntimeAdapter final
    {
    public:
        /** Generates canonical geometry, decodes the texture, and uploads orbit state. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(
                positions,
                normals,
                textureCoordinates,
                indices,
                diffuseMips,
                diffuse.width,
                diffuse.height,
                orbitYaw,
                orbitPitch,
                options.targetFrame == 0u ? 1.0f : 0.0f);
        }

        /** Leaves fixed-clock rotation to the generated Renderer. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures final RGBA8 and writes true-MRT structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU geometry and decoded image storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and prepares its exact CPU inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<glm::vec4> positions;
        eastl::vector<glm::vec4> normals;
        eastl::vector<glm::vec4> textureCoordinates;
        eastl::vector<uint32_t> indices;
        RgbaImageData diffuse;
        eastl::vector<eastl::vector<uint8_t>> diffuseMips;
        float orbitYaw = 0.0f;
        float orbitPitch = 0.0f;
        bool captureWritten = false;
    };
}
