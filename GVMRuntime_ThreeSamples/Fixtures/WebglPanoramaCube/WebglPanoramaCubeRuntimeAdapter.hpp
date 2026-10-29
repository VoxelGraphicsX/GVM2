#pragma once

#include "RgbaImageData.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one panorama BoxGeometry vertex across both generated pipelines. */
    struct alignas(16) WebglPanoramaCubeHostVertex final
    {
        glm::vec4 position;
        glm::vec4 textureCoordinate;
        glm::uvec4 faceSlot;
    };

    /** Mirrors the canonical camera transform stored for the single entity. */
    struct alignas(16) WebglPanoramaCubeHostObjectData final
    {
        glm::mat4 modelViewProjection;
    };

    /** Mirrors the ordinary one-instance component required by the Scene Set. */
    struct alignas(16) WebglPanoramaCubeHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors the six-group material component required by the Scene Set. */
    struct alignas(16) WebglPanoramaCubeHostMaterialData final
    {
        glm::uvec4 faceCountAndReserved;
    };

    /** Stores one cropped atlas tile and its complete explicit mip chain. */
    struct WebglPanoramaCubeTextureData final
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        eastl::vector<uint8_t> bytes;
        eastl::vector<uint64_t> mipOffsets;
    };

    /** Connects the dedicated six-texture panorama Set to the shared host. */
    class WebglPanoramaCubeRuntimeAdapter final
    {
    public:
        /** Decodes the pinned atlas and allocates the one Scene entity. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the target-frame camera state immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the target image and writes formal structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Clears sample-private decoded texture and geometry storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates locked inputs and allocates all entity components. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglPanoramaCubeHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<WebglPanoramaCubeTextureData, 6u> textures;
        bool hasReplay = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
