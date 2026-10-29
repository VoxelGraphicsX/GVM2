#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one source-box vertex in the generated RenderSet ABI. */
    struct alignas(16) WebgpuTexturegatherHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors one source-box transform component. */
    struct alignas(16) WebgpuTexturegatherHostObjectData final
    {
        glm::mat4 modelViewProjection;
        glm::mat4 model;
    };

    /** Mirrors one mandatory RenderSet instance record. */
    struct alignas(16) WebgpuTexturegatherHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors the source Standard-material component. */
    struct alignas(16) WebgpuTexturegatherHostMaterialData final
    {
        glm::vec4 baseColorAndAmbient;
    };

    /** Connects the dedicated texture-gather renderer to frozen source geometry. */
    class WebgpuTexturegatherRuntimeAdapter final
    {
    public:
        /** Configures output and allocates one box into each independent source Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            renderer.configureOutput(options.width, options.height);
            initializeResources(renderer, inDevice, options);
        }

        /** Preserves the immutable frame-zero source state. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Writes RGBA8, identity metadata, and the four-root Scene contract. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases host-side geometry after generated teardown begins. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the Manifest scenario and populates both source Sets. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Allocates one source box through the selected exported RenderSet. */
        void allocateSourceBox(GVM::Core::AbstractRendererImpl &renderer,
                               GVM::Core::RenderSetHandle renderSetHandle,
                               const char *label);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuTexturegatherHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebgpuTexturegatherHostObjectData objectData = {};
        WebgpuTexturegatherHostInstanceData instanceData = {};
        WebgpuTexturegatherHostMaterialData materialData = {};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
