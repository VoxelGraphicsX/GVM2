#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one shared billboard corner. */
    struct alignas(16) WebgpuInstanceSpritesHostVertex
    {
        glm::vec4 corner;
    };

    /** Mirrors the camera, time, and fog component consumed by the DSL. */
    struct alignas(16) WebgpuInstanceSpritesHostObjectData
    {
        glm::mat4 viewProjection{1.0f};
        glm::vec4 cameraRightAndTime{0.0f};
        glm::vec4 cameraUpAndFogDensity{0.0f};
        glm::vec4 cameraPositionAndReserved{0.0f};
    };

    /** Mirrors one deterministic Sprite position and ordinal. */
    struct alignas(16) WebgpuInstanceSpritesHostInstanceData
    {
        glm::vec4 positionAndOrdinal{0.0f};
    };

    /** Mirrors the SpriteNodeMaterial color, scale, and attenuation state. */
    struct alignas(16) WebgpuInstanceSpritesHostMaterialData
    {
        glm::vec4 colorAndScale{0.0f};
        glm::vec4 flags{0.0f};
    };

    /** Connects the deterministic r185 Sprite scene to the generated renderer. */
    class WebgpuInstanceSpritesRuntimeAdapter final
    {
    public:
        /** Builds the one-entity, 10,000-instance Scene RenderSet. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates camera, hue, rotation time, and GUI replay state. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 output and the one-Set structural contract. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all private CPU staging storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates options, decodes the texture, and allocates the entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Evaluates the canonical camera and material state at one frame. */
        void updateFrameState(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes capture metadata and structural evidence. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuInstanceSpritesHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebgpuInstanceSpritesHostInstanceData> instances;
        WebgpuInstanceSpritesHostObjectData objectData{};
        WebgpuInstanceSpritesHostMaterialData materialData{};
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> mipOffsets;
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
