#pragma once

#include "ColladaAsset.hpp"
#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one expanded Elf vertex in the generated RenderSet ABI. */
    struct alignas(16) WebglLoaderColladaHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uvAndMaterial;
    };

    /** Mirrors camera, hierarchy, normal, and lighting state. */
    struct alignas(16) WebglLoaderColladaHostObjectData final
    {
        glm::mat4 model;
        glm::mat4 viewProjection;
        glm::mat4 normalTransform;
        glm::vec4 cameraPosition;
        glm::vec4 directionalLight;
    };

    /** Mirrors the mandatory single-instance component entry. */
    struct alignas(16) WebglLoaderColladaHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors one four-entry Phong material component. */
    struct alignas(16) WebglLoaderColladaHostMaterialData final
    {
        glm::vec4 specularAndShininess;
    };

    /** Owns one flattened RGBA8 mip chain for a RenderSet texture slot. */
    struct WebglLoaderColladaTextureData final
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        eastl::vector<uint8_t> bytes;
        eastl::vector<uint64_t> mipOffsets;
    };

    /** Connects the frozen Elf COLLADA scene to its dedicated one-Set DSL renderer. */
    class WebglLoaderColladaRuntimeAdapter final
    {
    public:
        /** Parses the DAE and JPEG assets and allocates the Scene's unique Set. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Applies the exact fixed-step root Z rotation before each capture frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes complete one-Set structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all sample-private CPU asset staging data. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and allocates the dedicated Scene entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Updates model and normal components for one deterministic frame. */
        void updateObjectData(uint32_t frameIndex);

        GVM::Core::DeviceProxy device;
        ColladaElfAsset asset;
        eastl::vector<WebglLoaderColladaHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebglLoaderColladaHostMaterialData> materials;
        eastl::vector<WebglLoaderColladaTextureData> textures;
        WebglLoaderColladaHostObjectData objectData{};
        WebglLoaderColladaHostInstanceData instanceData{};
        GVM::Core::RenderEntityIndex entityIndex = {};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
