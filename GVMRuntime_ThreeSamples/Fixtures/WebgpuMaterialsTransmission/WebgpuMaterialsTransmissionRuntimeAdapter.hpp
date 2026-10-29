#pragma once

#include "GifImageDecoder.hpp"
#include "ThreeR185DfgLutData.hpp"
#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

namespace GVM::ThreeSamples
{
    /** Connects the exact Bridge2/equirectangular assets and sphere geometry to the DSL renderer. */
    class WebgpuMaterialsTransmissionRuntimeAdapter final
    {
    public:
        /** Decodes all pinned assets, generates the detail-15 sphere, and configures one scenario. */
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
                backgroundPositions,
                backgroundNormals,
                backgroundIndices,
                cubeMips[0u],
                cubeMips[1u],
                cubeMips[2u],
                cubeMips[3u],
                cubeMips[4u],
                cubeMips[5u],
                equirectangularPixels,
                dfgLutPackedPixels,
                cameraPositionAndMode,
                cameraRightAndRefraction,
                cameraUpAndFrame,
                cameraForwardAndTanHalfFov,
                rotationRows[0u],
                rotationRows[1u],
                rotationRows[2u]);
        }

        /** Leaves the target-frame scene immutable after deterministic setup. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected RGBA8 frame and writes strict gate artifacts. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases decoded CPU texture and generated geometry storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked contract and prepares all CPU-owned non-render inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8, metadata, and the ordinary Scene structural snapshot. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<glm::vec4> positions;
        eastl::vector<glm::vec4> normals;
        eastl::vector<glm::vec2> textureCoordinates;
        eastl::vector<uint32_t> indices;
        eastl::vector<glm::vec4> backgroundPositions;
        eastl::vector<glm::vec4> backgroundNormals;
        eastl::vector<uint32_t> backgroundIndices;
        eastl::array<eastl::vector<eastl::vector<uint8_t>>, 6u> cubeMips;
        eastl::vector<uint16_t> equirectangularPixels;
        eastl::vector<uint32_t> dfgLutPackedPixels;
        eastl::array<glm::vec4, 3u> rotationRows = {};
        glm::vec4 cameraPositionAndMode = {};
        glm::vec4 cameraRightAndRefraction = {};
        glm::vec4 cameraUpAndFrame = {};
        glm::vec4 cameraForwardAndTanHalfFov = {};
        bool equirectangularScenario = false;
        bool rotationScenario = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
