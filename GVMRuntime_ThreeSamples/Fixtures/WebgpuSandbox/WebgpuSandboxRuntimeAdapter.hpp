#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "RgbaImageData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors the union mesh, point-quad, and line-quad vertex layout. */
    struct alignas(16) WebgpuSandboxHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uv;
        glm::vec4 color;
    };

    /** Mirrors one entity transform, camera matrix, and fixed-clock state. */
    struct alignas(16) WebgpuSandboxHostObjectData final
    {
        glm::mat4 model;
        glm::mat4 viewProjection;
        glm::vec4 timeAndViewport;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebgpuSandboxHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors one private sandbox material selector and parameter record. */
    struct alignas(16) WebgpuSandboxHostMaterialData final
    {
        glm::uvec4 kindAndPhase;
        glm::vec4 parameters;
    };

    /** Mirrors the CPU primitive expansion descriptor stored per entity. */
    struct alignas(16) WebgpuSandboxHostPrimitiveExpansionData final
    {
        glm::vec4 parameters;
    };

    /** Mirrors the material phase flags stored per entity. */
    struct alignas(16) WebgpuSandboxHostRenderPhaseData final
    {
        glm::uvec4 flags;
    };

    /** Owns one packed Scene entity and its immutable decoded texture. */
    struct WebgpuSandboxEntityData final
    {
        eastl::string logicalId;
        eastl::vector<WebgpuSandboxHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebgpuSandboxHostObjectData objectData{};
        WebgpuSandboxHostInstanceData instanceData{};
        WebgpuSandboxHostMaterialData materialData{};
        WebgpuSandboxHostPrimitiveExpansionData primitiveExpansionData{};
        WebgpuSandboxHostRenderPhaseData renderPhaseData{};
        RgbaImageData texture;
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Connects the dedicated six-entity WebGPU sandbox renderer to frozen r185 data. */
    class WebgpuSandboxRuntimeAdapter final
    {
    public:
        /** Builds all six entities, decodes three assets, and allocates one Scene Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates only the animated box transform and fixed-clock object records. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Captures RGBA8 and writes the six-entity Scene contract evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases all host-side decoded images and packed entity data. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates one frozen scenario and allocates the complete Scene Set. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuSandboxEntityData> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
