#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglPostprocessingData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one ordinary Mesh transform component in generated layout. */
    struct alignas(16) WebglPostprocessingHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalModelView;
    };

    /** Mirrors the mandatory identity instance component. */
    struct alignas(16) WebglPostprocessingHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors the shared white flat MeshPhong material component. */
    struct alignas(16) WebglPostprocessingHostMaterialData
    {
        glm::vec4 diffuseAndShininess;
        glm::vec4 specular;
    };

    /** Connects the exact 100-object deterministic Scene to its DSL Renderer. */
    class WebglPostprocessingRuntimeAdapter final
    {
    public:
        /** Builds SphereGeometry, reconstructs the random Scene, and allocates 100 entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the precomputed target-frame entity transforms immutable. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes final RGBA8 and the unique-Set structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU staging data after Renderer shutdown starts. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and allocates all ordinary entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        eastl::vector<WebglPostprocessingVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebglPostprocessingHostObjectData> objects;
        WebglPostprocessingHostInstanceData instanceData{};
        WebglPostprocessingHostMaterialData materialData{};
        GVM::Core::DeviceProxy device;
        bool captureWritten = false;
    };
}
