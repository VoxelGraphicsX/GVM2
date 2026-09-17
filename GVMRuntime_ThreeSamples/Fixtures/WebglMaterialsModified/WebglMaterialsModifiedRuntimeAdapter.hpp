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
    /** Mirrors one DSL vertex for RenderSet allocation. */
    struct alignas(16) WebglMaterialsModifiedHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors one DSL object component for RenderSet allocation. */
    struct alignas(16) WebglMaterialsModifiedHostObjectData final
    {
        glm::mat4 modelView;
        glm::mat4 projection;
        glm::mat4 normalTransform;
    };

    /** Mirrors the mandatory identity instance component. */
    struct alignas(16) WebglMaterialsModifiedHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors one opposed twist parameter component. */
    struct alignas(16) WebglMaterialsModifiedHostMaterialData final
    {
        glm::vec4 twistAmountAndTime;
    };

    /** Connects the locked GLB and scenario state to the modified-material renderer. */
    class WebglMaterialsModifiedRuntimeAdapter final
    {
    public:
        /** Decodes the GLB, creates two Scene entities, and configures output. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            renderer.configureOutput(options.width, options.height);
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the deterministic Scene immutable during warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads RGBA8 and writes structural and loader evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases decoded CPU data after generated renderer teardown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and allocates the unique Scene Set. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMaterialsModifiedHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
