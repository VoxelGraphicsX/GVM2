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
    /** Mirrors one generated sprite corner and UV record. */
    struct alignas(16) WebgpuSpritesHostVertex
    {
        glm::vec4 cornerAndUv;
    };

    /** Mirrors one generated projection and view-space center record. */
    struct alignas(16) WebgpuSpritesHostObjectData
    {
        glm::mat4 projection;
        glm::vec4 viewCenterAndDepth;
    };

    /** Mirrors the mandatory non-instanced component record. */
    struct alignas(16) WebgpuSpritesHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one fixed white sprite material record. */
    struct alignas(16) WebgpuSpritesHostMaterialData
    {
        glm::vec4 colorAndOpacity;
    };

    /** Mirrors one per-entity rotation, scale, center, and visibility record. */
    struct alignas(16) WebgpuSpritesHostStateData
    {
        glm::vec4 rotationScaleCenter;
        glm::vec4 visibilityAndFog;
    };

    /** Stores one ordinary sprite entity's complete CPU staging payload. */
    struct WebgpuSpritesEntityState final
    {
        eastl::vector<WebgpuSpritesHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebgpuSpritesHostObjectData objectData{};
        WebgpuSpritesHostInstanceData instanceData{};
        WebgpuSpritesHostMaterialData materialData{};
        WebgpuSpritesHostStateData spriteState{};
    };

    /** Connects the deterministic 200-sprite scene to its dedicated Renderer. */
    class WebgpuSpritesRuntimeAdapter final
    {
    public:
        /** Decodes the pinned asset and allocates all ordinary entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the target-frame state immutable during host warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads final RGBA8 output and writes deterministic evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU texture and entity staging after shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Builds the exact random layout, animation state, texture, and Set. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuSpritesEntityState> entities;
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> textureMipOffsets;
        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
}
