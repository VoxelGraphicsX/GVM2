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
    /** Mirrors one packed TubeGeometry or expanded-wireframe vertex. */
    struct alignas(16) WebglGeometryExtrudeSplinesHostVertex
    {
        glm::vec4 position;
        glm::vec4 normalOrLineEnd;
        glm::vec4 lineData;
    };

    /** Mirrors one spline entity transform, viewport, and lighting component. */
    struct alignas(16) WebglGeometryExtrudeSplinesHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
        glm::vec4 viewport;
        glm::vec4 ambientAndDirectionalIntensity;
        glm::vec4 directionalView;
    };

    /** Mirrors the mandatory non-instanced component entry. */
    struct alignas(16) WebglGeometryExtrudeSplinesHostInstanceData
    {
        glm::vec4 reserved;
        glm::vec4 lineStart;
        glm::vec4 lineEnd;
    };

    /** Mirrors one spline material color, opacity, and render phase. */
    struct alignas(16) WebglGeometryExtrudeSplinesHostMaterialData
    {
        glm::vec4 colorAndOpacity;
        glm::vec4 phaseAndReserved;
    };

    /** Stores one complete spline RenderSet entity payload. */
    struct WebglGeometryExtrudeSplinesEntityData
    {
        eastl::string logicalId;
        eastl::vector<WebglGeometryExtrudeSplinesHostVertex>
            vertices;
        eastl::vector<uint32_t> indices;
        WebglGeometryExtrudeSplinesHostObjectData objectData{};
        eastl::vector<WebglGeometryExtrudeSplinesHostInstanceData>
            instanceData;
        WebglGeometryExtrudeSplinesHostMaterialData materialData{};
    };

    /** Connects r185 TubeGeometry and wireframe expansion to one four-entity Scene Set. */
    class WebglGeometryExtrudeSplinesRuntimeAdapter final
    {
    public:
        /** Builds the selected spline and allocates the Scene's unique RenderSet. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(
                renderer,
                inDevice,
                options);
        }

        /** Keeps the fixed default and alternate capture payloads unchanged. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes complete four-entity structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU tube and wireframe staging data. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and allocates all four entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<
            WebglGeometryExtrudeSplinesEntityData>
            entities;
        bool alternateSpline = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
