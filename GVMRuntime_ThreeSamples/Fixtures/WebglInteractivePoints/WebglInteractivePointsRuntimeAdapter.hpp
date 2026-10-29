#pragma once

#include "Fixtures/Phase1TextureCases/TexturedBoxSampleData.hpp"
#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one fully CPU-expanded point vertex in the RenderSet ABI. */
    struct alignas(16) InteractivePointsHostVertex
    {
        TexturedBoxHostFloat4 positionAndSize;
        TexturedBoxHostFloat4 color;
        TexturedBoxHostFloat4 corner;
    };

    /** Mirrors the projection and model-view matrices of the Points entity. */
    struct alignas(16) InteractivePointsHostObjectData
    {
        glm::mat4 projectionMatrix;
        glm::mat4 modelViewMatrix;
    };

    /** Mirrors the required non-instanced component record. */
    struct alignas(16) InteractivePointsHostInstanceData
    {
        TexturedBoxHostFloat4 reserved;
    };

    /** Mirrors the common ShaderMaterial color and alpha-test cutoff. */
    struct alignas(16) InteractivePointsHostMaterialData
    {
        TexturedBoxHostFloat4 colorAndAlphaTest;
    };

    /** Connects exact r185 point geometry and CPU picking to its renderer. */
    class WebglInteractivePointsRuntimeAdapter final
    {
    public:
        /** Builds the merged box, stages disc.png, and allocates one entity. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the immutable target-frame state stable during host warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 output and writes strict Scene evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases private CPU staging after generated resource destruction. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and allocates every RenderSet component. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes requested capture and structural snapshots exactly once. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<InteractivePointsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        InteractivePointsHostInstanceData instanceData = {};
        eastl::vector<uint8_t> discTextureBytes;
        eastl::vector<uint64_t> discMipOffsets;
        uint32_t selectedPoint = UINT32_MAX;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
