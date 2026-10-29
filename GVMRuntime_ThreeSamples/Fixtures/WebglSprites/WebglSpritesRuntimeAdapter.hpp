#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one generated WebGL sprite corner and UV record. */
    struct alignas(16) WebglSpritesHostVertex
    {
        glm::vec4 cornerAndUv;
    };

    /** Mirrors one projection and sprite view-space center record. */
    struct alignas(16) WebglSpritesHostObjectData
    {
        glm::mat4 projection;
        glm::vec4 viewCenter;
    };

    /** Mirrors the mandatory non-instanced entity record. */
    struct alignas(16) WebglSpritesHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one sprite color, opacity, and UV transform record. */
    struct alignas(16) WebglSpritesHostMaterialData
    {
        glm::vec4 colorAndOpacity;
        glm::vec4 uvScaleAndOffset;
    };

    /** Mirrors one rotation, scale, center, visibility, and fog record. */
    struct alignas(16) WebglSpritesHostStateData
    {
        glm::vec4 rotationScaleCenterX;
        glm::vec4 centerYVisibilityFog;
    };

    /** Stores one ordinary world or HUD sprite staging payload. */
    struct WebglSpritesEntityState final
    {
        eastl::vector<WebglSpritesHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglSpritesHostObjectData objectData{};
        WebglSpritesHostInstanceData instanceData{};
        WebglSpritesHostMaterialData materialData{};
        WebglSpritesHostStateData spriteState{};
        uint32_t textureIndex = 0u;
    };

    /** Stores one pinned texture and its complete deterministic mip payload. */
    struct WebglSpritesTextureAsset final
    {
        eastl::vector<uint8_t> bytes;
        eastl::vector<uint64_t> mipOffsets;
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    /** Connects the two deterministic sprite Scenes to the dedicated Renderer. */
    class WebglSpritesRuntimeAdapter final
    {
    public:
        /** Decodes assets and allocates both unique Scene Set instances. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the frozen target-frame state immutable during host warm-up. */
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

        /** Releases all CPU staging state after shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Builds exact random state, assets, and both Scene Sets. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglSpritesEntityState> worldEntities;
        eastl::vector<WebglSpritesEntityState> hudEntities;
        eastl::array<WebglSpritesTextureAsset, 3u> textures;
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
}
