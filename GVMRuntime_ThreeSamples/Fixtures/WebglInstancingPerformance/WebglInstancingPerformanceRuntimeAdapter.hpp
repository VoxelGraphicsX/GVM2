#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one Suzanne vertex in the dedicated performance RenderSet ABI. */
    struct alignas(16) WebglInstancingPerformanceHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors one entity model-view transform and camera projection. */
    struct alignas(16) WebglInstancingPerformanceHostObjectData
    {
        glm::mat4 viewProjection;
        glm::mat4 model;
        glm::vec4 modeAndTime;
    };

    /** Mirrors three rows of one seeded affine instance transform. */
    struct alignas(16) WebglInstancingPerformanceHostInstanceData
    {
        glm::vec4 transformRow0;
        glm::vec4 transformRow1;
        glm::vec4 transformRow2;
    };

    /** Mirrors private MeshNormalMaterial opacity controls. */
    struct alignas(16) WebglInstancingPerformanceHostMaterialData
    {
        glm::vec4 opacityAndFlags;
    };

    /** Connects all r185 performance modes to one dedicated Scene RenderSet. */
    class WebglInstancingPerformanceRuntimeAdapter final
    {
    public:
        /** Decodes Suzanne and allocates Instanced, Merged, or Naive entity organization. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the selected mode and target-frame camera immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads RGBA8 output and writes mode-specific RenderSet evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU staging after generated resources are destroyed. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one scenario and allocates its exact Scene entity organization. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglInstancingPerformanceHostVertex> sourceVertices;
        eastl::vector<uint32_t> sourceIndices;
        eastl::string mode;
        uint32_t entityCount = 0u;
        uint32_t instancesPerEntity = 0u;
        bool captureWritten = false;
    };
}
