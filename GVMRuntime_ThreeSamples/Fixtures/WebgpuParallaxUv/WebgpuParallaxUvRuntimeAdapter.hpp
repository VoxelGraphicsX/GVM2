#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Connects locked Ice/HDR assets and deterministic controls to the dedicated parallax renderer. */
    class WebgpuParallaxUvRuntimeAdapter final
    {
    public:
        /** Decodes assets, builds CircleGeometry, and configures one immutable scenario. */
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
                textureAtlases[0u],
                textureAtlases[1u],
                textureAtlases[2u],
                textureAtlases[3u],
                textureAtlases[4u],
                textureAtlasWidth,
                textureAtlasHeight,
                environmentPixels,
                environmentWidth,
                environmentHeight,
                dfgLutPackedPixels,
                cameraPositionAndScale,
                cameraRightAndBackgroundBlur,
                cameraUpAndParallaxScale,
                cameraForwardAndTanHalfFov,
                viewportAndExposure,
                outputFlagsAndOffset);
        }

        /** Keeps the locked target-frame scene immutable after setup. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures the selected RGBA8 frame and writes strict structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU-owned decoded pixels and geometry. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and prepares every CPU-owned input. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<glm::vec4> positions;
        eastl::vector<glm::vec4> normals;
        eastl::vector<glm::vec2> textureCoordinates;
        eastl::vector<uint32_t> indices;
        eastl::array<eastl::vector<uint8_t>, 5u> textureAtlases;
        eastl::vector<uint16_t> environmentPixels;
        eastl::vector<uint32_t> dfgLutPackedPixels;
        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
        uint32_t textureAtlasWidth = 0u;
        uint32_t textureAtlasHeight = 0u;
        uint32_t environmentWidth = 0u;
        uint32_t environmentHeight = 0u;
        glm::vec4 cameraPositionAndScale = {};
        glm::vec4 cameraRightAndBackgroundBlur = {};
        glm::vec4 cameraUpAndParallaxScale = {};
        glm::vec4 cameraForwardAndTanHalfFov = {};
        glm::vec4 viewportAndExposure = {};
        glm::vec4 outputFlagsAndOffset = {};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
