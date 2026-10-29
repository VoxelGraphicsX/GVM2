#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one dedicated Suzanne vertex in the generated RenderSet ABI. */
    struct alignas(16) WebgpuInstanceMeshHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors the animated mesh transform and camera projection component. */
    struct alignas(16) WebgpuInstanceMeshHostObjectData
    {
        glm::mat4 viewProjection;
        glm::mat4 model;
        glm::vec4 timeAndCount;
    };

    /** Mirrors one cubic-grid transform and stable instance ordinal. */
    struct alignas(16) WebgpuInstanceMeshHostInstanceData
    {
        glm::vec4 transformColumn0;
        glm::vec4 transformColumn1;
        glm::vec4 transformColumn2;
        glm::vec4 transformColumn3;
        glm::vec4 ordinal;
    };

    /** Mirrors the private MeshBasicNodeMaterial component. */
    struct alignas(16) WebgpuInstanceMeshHostMaterialData
    {
        glm::vec4 colorAndOpacity;
    };

    /** Connects the r185 animated Suzanne grid to its dedicated generated Renderer. */
    class WebgpuInstanceMeshRuntimeAdapter final
    {
    public:
        /** Decodes Suzanne and allocates the sole Scene entity before rendering. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the captured target-frame instance payload immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads RGBA8 output and writes structural and loader evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU staging after the generated Renderer shuts down. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario, builds CPU data, and allocates one RenderSet entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuInstanceMeshHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebgpuInstanceMeshHostInstanceData> instances;
        uint32_t activeInstanceCount = 0u;
        bool captureWritten = false;
    };
}
