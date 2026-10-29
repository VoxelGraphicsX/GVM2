#pragma once

#include "DdsImageDecoder.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one DDS Scene vertex with an explicit cross-pipeline ABI. */
    struct alignas(16) WebglLoaderTextureDdsHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors one DDS entity transform and fixed material phase. */
    struct alignas(16) WebglLoaderTextureDdsHostObjectData final
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 environmentModel;
        glm::uvec4 phaseAndFlags;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglLoaderTextureDdsHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors default Standard light and roughness parameters. */
    struct alignas(16) WebglLoaderTextureDdsHostMaterialData final
    {
        glm::vec4 ambientPointRoughnessOpacity;
    };

    /** Connects the dedicated DDS DSL renderer to all fourteen frozen assets. */
    class WebglLoaderTextureDdsRuntimeAdapter final
    {
    public:
        /** Decodes assets and allocates all thirteen Scene entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Leaves the fixed captured frame unchanged before rendering. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes deterministic image and structural evidence at the target frame. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing sample teardown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and creates the unique Scene Set. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLoaderTextureDdsHostVertex> boxVertices;
        eastl::vector<uint32_t> boxIndices;
        eastl::vector<WebglLoaderTextureDdsHostVertex> torusVertices;
        eastl::vector<uint32_t> torusIndices;
        eastl::array<DdsRgba8Texture, 14u> textures;
        eastl::array<eastl::array<eastl::vector<uint8_t>, 7u>, 13u>
            textureBytes;
        eastl::array<eastl::array<eastl::vector<uint64_t>, 7u>, 13u>
            mipOffsets;
        eastl::array<WebglLoaderTextureDdsHostObjectData, 13u> objects = {};
        eastl::array<WebglLoaderTextureDdsHostInstanceData, 13u> instances = {};
        eastl::array<WebglLoaderTextureDdsHostMaterialData, 13u> materials = {};
        eastl::array<glm::uvec4, 13u> renderFlags = {};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
