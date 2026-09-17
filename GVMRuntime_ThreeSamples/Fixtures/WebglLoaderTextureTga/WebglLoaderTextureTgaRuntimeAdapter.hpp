#pragma once

#include "TexturedBoxSampleData.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one TGA BoxGeometry vertex across both shader pipelines. */
    struct alignas(16) WebglLoaderTextureTgaHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors one entity's model-view, normal, projection, and light state. */
    struct alignas(16) WebglLoaderTextureTgaHostObjectData final
    {
        glm::mat4 modelView;
        glm::mat4 normalTransform;
        glm::mat4 projection;
        glm::vec4 lightDirectionAndIntensity;
        glm::vec4 ambientIntensityAndReserved;
    };

    /** Mirrors the required one-entry ordinary instance component. */
    struct alignas(16) WebglLoaderTextureTgaHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors one MeshPhong diffuse/specular material record. */
    struct alignas(16) WebglLoaderTextureTgaHostMaterialData final
    {
        glm::vec4 diffuseColor;
        glm::vec4 specularColorAndShininess;
    };

    /** Stores one decoded TGA texture and its explicit sRGB mip chain. */
    struct WebglLoaderTextureTgaTextureData final
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        eastl::vector<uint8_t> bytes;
        eastl::vector<uint64_t> mipOffsets;
    };

    /** Connects the dedicated TGA loader scene to the shared sample host. */
    class WebglLoaderTextureTgaRuntimeAdapter final
    {
    public:
        /** Decodes both locked TGA files and allocates their two entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Leaves the fixed camera and material state unchanged during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the final image and writes strict scene/loader artifacts. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Clears decoded CPU texture and geometry storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, decodes TGA assets, and allocates the Set. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLoaderTextureTgaHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<WebglLoaderTextureTgaTextureData, 2u> textures;
        bool hasReplay = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
