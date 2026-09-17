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
    /** Connects TorusKnot inputs to the dedicated MRT readback Renderer. */
    class WebgpuMultipleRendertargetsReadbackRuntimeAdapter final
    {
    public:
        /** Generates canonical geometry, texture mips, and output selection. */
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
                outputSelection);
            readbackBytes.assign(512u * 512u * 4u, uint8_t(0u));
            renderer.uploadSelectedAttachment(readbackBytes);
        }

        /** Leaves fixed-clock rotation to the generated Renderer. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Transfers the selected attachment between frames and captures the target frame. */
        template <class RendererImpl>
        void afterFrame(
            RendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height)
        {
            captureTargetFrame(
                options,
                frameIndex,
                readbackTexture,
                width,
                height);
            if (outputSelection > 0.5f && frameIndex == options.targetFrame)
            {
                renderer.copySelectedAttachmentToCpu(readbackBytes);
                renderer.uploadSelectedAttachment(readbackBytes);
            }
        }

        /** Releases CPU geometry and decoded image storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and prepares its exact CPU inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes final capture and structural artifacts for one target frame. */
        void captureTargetFrame(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        GVM::Core::DeviceProxy device;
        eastl::vector<glm::vec4> positions;
        eastl::vector<glm::vec4> normals;
        eastl::vector<glm::vec4> textureCoordinates;
        eastl::vector<uint32_t> indices;
        RgbaImageData diffuse;
        eastl::vector<eastl::vector<uint8_t>> diffuseMips;
        float orbitYaw = 0.0f;
        float orbitPitch = 0.0f;
        float outputSelection = 0.0f;
        eastl::vector<uint8_t> readbackBytes;
        bool captureWritten = false;
    };
}
