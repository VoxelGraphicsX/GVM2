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
    struct alignas(16) WebglPostprocessingPixelHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalModelView;
        glm::mat4 directionalShadowViewProjection;
        glm::mat4 spotShadowViewProjection;
    };

    /** Mirrors the mandatory identity instance component. */
    struct alignas(16) WebglPostprocessingPixelHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors the shared white flat MeshPhong material component. */
    struct alignas(16) WebglPostprocessingPixelHostMaterialData
    {
        glm::vec4 diffuseAndShininess;
        glm::vec4 specular;
    };

    /** Mirrors per-entity shadow and material phase flags. */
    struct alignas(16) WebglPostprocessingPixelHostRenderFlags
    {
        uint32_t values[4]{};
    };

    /** Connects the exact four-object deterministic Scene to its DSL Renderer. */
    class WebglPostprocessingPixelRuntimeAdapter final
    {
    public:
    /** Builds the two boxes, floor, crystal, and allocates four entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            if (options.scenarioId == "nondefault-pixel-edges")
            {
                renderer.configurePixelEffect(
                    options.width, options.height, 10u, 0.75f, 0.2f);
            }
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

        eastl::vector<WebglPostprocessingPixelVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<eastl::vector<WebglPostprocessingPixelVertex>> entityVertices;
        eastl::vector<eastl::vector<uint32_t>> entityIndices;
        eastl::vector<WebglPostprocessingPixelHostObjectData> objects;
        WebglPostprocessingPixelHostInstanceData instanceData{};
        eastl::vector<WebglPostprocessingPixelHostMaterialData> materials;
        eastl::vector<WebglPostprocessingPixelHostRenderFlags> renderFlags;
        GVM::Core::DeviceProxy device;
        bool captureWritten = false;
    };
}
