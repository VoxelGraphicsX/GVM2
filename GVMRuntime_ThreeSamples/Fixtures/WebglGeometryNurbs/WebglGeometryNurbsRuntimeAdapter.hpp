#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one packed NURBS surface or expanded-line vertex. */
    struct alignas(16) WebglGeometryNurbsHostVertex
    {
        glm::vec4 position;
        glm::vec4 normalOrLineEnd;
        glm::vec4 uvAndLineData;
    };

    /** Mirrors one NURBS entity transform, viewport, and lighting component. */
    struct alignas(16) WebglGeometryNurbsHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
        glm::vec4 viewport;
        glm::vec4 ambientAndDirectionalIntensity;
        glm::vec4 directionalView;
    };

    /** Mirrors the mandatory non-instanced component entry. */
    struct alignas(16) WebglGeometryNurbsHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one NURBS material color, opacity, and pass phase. */
    struct alignas(16) WebglGeometryNurbsHostMaterialData
    {
        glm::vec4 colorAndOpacity;
        glm::vec4 phaseAndReserved;
    };

    /** Stores one complete NURBS RenderSet entity payload. */
    struct WebglGeometryNurbsEntityData
    {
        eastl::string logicalId;
        eastl::vector<WebglGeometryNurbsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglGeometryNurbsHostObjectData objectData{};
        WebglGeometryNurbsHostInstanceData instanceData{};
        WebglGeometryNurbsHostMaterialData materialData{};
        glm::mat4 localTransform{1.0f};
    };

    /** Connects exact r185 NURBS CPU evaluation to one eight-entity Scene Set. */
    class WebglGeometryNurbsRuntimeAdapter final
    {
    public:
        /** Builds all eight entities and allocates the Scene's unique RenderSet. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the immutable capture-frame entity payloads unchanged. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes complete eight-entity structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU geometry and texture staging data. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and allocates all eight entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglGeometryNurbsEntityData> entities;
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> textureMipOffsets;
        uint32_t finalRandomState = 0u;
        double groupRotation = 0.0;
        bool captureWritten = false;
    };
}
