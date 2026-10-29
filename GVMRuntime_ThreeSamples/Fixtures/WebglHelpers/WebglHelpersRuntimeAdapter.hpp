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
    /** Mirrors the union mesh and pre-expanded clip-space line vertex. */
    struct alignas(16) WebglHelpersHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 color;
        glm::vec4 lineEndpoints;
    };

    /** Mirrors one helper entity transform and animated light position. */
    struct alignas(16) WebglHelpersHostObjectData final
    {
        glm::mat4 modelViewProjection;
        glm::mat4 model;
        glm::vec4 lightPosition;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglHelpersHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors the loaded head material parameters. */
    struct alignas(16) WebglHelpersHostMaterialData final
    {
        glm::vec4 baseColorAndRoughness;
    };

    /** Mirrors helper kind, phase, color, opacity, and depth semantics. */
    struct alignas(16) WebglHelpersHostHelperData final
    {
        glm::uvec4 kindPhaseAndDepth;
        glm::vec4 colorOpacity;
    };

    /** Owns one of the thirteen entities packed into the unique Scene Set. */
    struct WebglHelpersEntityData final
    {
        eastl::string logicalId;
        eastl::vector<WebglHelpersHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglHelpersHostObjectData objectData{};
        WebglHelpersHostInstanceData instanceData{};
        WebglHelpersHostMaterialData materialData{};
        WebglHelpersHostHelperData helperData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Connects the dedicated thirteen-entity helper renderer to r185 data. */
    class WebglHelpersRuntimeAdapter final
    {
    public:
        /** Decodes the GLB and allocates all mesh and helper entities. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Keeps the target-frame helper snapshot immutable during warm-up. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Captures RGBA8 and writes structural and loader evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases all decoded mesh and helper arrays. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates one scenario and allocates the complete unique Scene Set. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglHelpersEntityData> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
