#pragma once

#include "Ktx1ImageDecoder.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one KTX Scene vertex with an explicit cross-pipeline ABI. */
    struct alignas(16) WebglLoaderTextureKtxHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors one KTX entity transform and fixed material phase. */
    struct alignas(16) WebglLoaderTextureKtxHostObjectData final
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::uvec4 phaseAndFlags;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglLoaderTextureKtxHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors default Standard light and roughness parameters. */
    struct alignas(16) WebglLoaderTextureKtxHostMaterialData final
    {
        glm::vec4 ambientPointRoughnessOpacity;
    };

    /** Connects the dedicated KTX DSL renderer to all nine frozen assets. */
    class WebglLoaderTextureKtxRuntimeAdapter final
    {
    public:
        /** Decodes assets and allocates all nine Scene entities. */
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
        eastl::vector<WebglLoaderTextureKtxHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<Ktx1Rgba8Texture, 9u> textures;
        eastl::array<eastl::vector<uint8_t>, 9u> textureBytes;
        eastl::array<eastl::vector<uint64_t>, 9u> mipOffsets;
        eastl::array<WebglLoaderTextureKtxHostObjectData, 9u> objects = {};
        eastl::array<WebglLoaderTextureKtxHostInstanceData, 9u> instances = {};
        eastl::array<WebglLoaderTextureKtxHostMaterialData, 9u> materials = {};
        eastl::array<glm::uvec4, 9u> renderFlags = {};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
