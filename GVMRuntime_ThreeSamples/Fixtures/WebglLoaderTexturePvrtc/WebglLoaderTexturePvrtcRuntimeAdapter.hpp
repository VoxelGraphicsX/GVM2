#pragma once

#include "PvrImageDecoder.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one PVR Scene vertex with an explicit cross-pipeline ABI. */
    struct alignas(16) WebglLoaderTexturePvrtcHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors one PVR entity transform and fixed material phase. */
    struct alignas(16) WebglLoaderTexturePvrtcHostObjectData final
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::uvec4 phaseAndFlags;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglLoaderTexturePvrtcHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors default Standard light and roughness parameters. */
    struct alignas(16) WebglLoaderTexturePvrtcHostMaterialData final
    {
        glm::vec4 ambientPointRoughnessOpacity;
    };

    /** Connects the dedicated PVR DSL renderer to all eight frozen assets. */
    class WebglLoaderTexturePvrtcRuntimeAdapter final
    {
    public:
        /** Decodes assets and allocates all eight Scene entities. */
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
        eastl::vector<WebglLoaderTexturePvrtcHostVertex> boxVertices;
        eastl::vector<uint32_t> boxIndices;
        eastl::vector<WebglLoaderTexturePvrtcHostVertex> torusVertices;
        eastl::vector<uint32_t> torusIndices;
        eastl::array<PvrRgba8Texture, 8u> textures;
        eastl::array<eastl::array<eastl::vector<uint8_t>, 6u>, 8u>
            textureFaceBytes;
        eastl::array<eastl::array<eastl::vector<uint64_t>, 6u>, 8u>
            textureFaceMipOffsets;
        eastl::array<WebglLoaderTexturePvrtcHostObjectData, 8u> objects = {};
        eastl::array<WebglLoaderTexturePvrtcHostInstanceData, 8u> instances = {};
        eastl::array<WebglLoaderTexturePvrtcHostMaterialData, 8u> materials = {};
        eastl::array<glm::uvec4, 8u> renderFlags = {};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
