#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one PlaneGeometry position and texture coordinate. */
    struct alignas(16) WebgpuLayersHostVertex final
    {
        glm::vec4 position;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors the shared camera matrix and canonical time. */
    struct alignas(16) WebgpuLayersHostObjectData final
    {
        glm::mat4 projectionView;
        glm::vec4 timeAndReserved;
    };

    /** Mirrors one blossom's animation attributes. */
    struct alignas(16) WebgpuLayersHostInstanceData final
    {
        glm::vec4 positionAndTimeOffset;
        glm::vec4 rotation;
        glm::vec4 direction;
    };

    /** Mirrors one entity's linear blossom color. */
    struct alignas(16) WebgpuLayersHostMaterialData final
    {
        glm::vec4 baseColor;
    };

    /** Mirrors entity membership and the active camera layer mask. */
    struct alignas(16) WebgpuLayersHostRenderLayerData final
    {
        glm::uvec4 entityLayerAndCameraMask;
    };

    /** Stores all CPU payloads for one 2,500-instance entity. */
    struct WebgpuLayersEntityData final
    {
        eastl::string logicalId;
        eastl::vector<WebgpuLayersHostInstanceData> instances;
        WebgpuLayersHostObjectData objectData{};
        WebgpuLayersHostMaterialData materialData{};
        WebgpuLayersHostRenderLayerData renderLayerData{};
    };

    /** Connects exact r185 blossom-layer data to one Scene RenderSet. */
    class WebgpuLayersRuntimeAdapter final
    {
    public:
        /** Builds 7,500 instances and allocates exactly three Scene entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps target-frame instance data immutable during host warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes RGBA8 and complete three-entity structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all sample-private CPU staging storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and fills the unique Scene Set. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuLayersHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<WebgpuLayersEntityData, 3u> entities;
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> textureMipOffsets;
        eastl::string replaySha256;
        uint32_t finalRandomState = 0u;
        uint32_t activeLayerMask = 7u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
