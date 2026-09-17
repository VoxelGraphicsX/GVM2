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
    /** Mirrors one transition Scene camera component in generated layout. */
    struct alignas(16) WebglPostprocessingTransitionHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalModelView;
    };

    /** Mirrors one InstancedMesh transform and scalar color in generated layout. */
    struct alignas(16) WebglPostprocessingTransitionHostInstanceData
    {
        glm::mat4 model;
        glm::vec4 color;
    };

    /** Mirrors the shared white flat MeshPhong material component. */
    struct alignas(16) WebglPostprocessingTransitionHostMaterialData
    {
        glm::vec4 diffuseAndShininess;
        glm::vec4 specular;
    };

    /** Connects the exact 100-object deterministic Scene to its DSL Renderer. */
    class WebglPostprocessingTransitionRuntimeAdapter final
    {
    public:
        /** Builds the two r185 transition geometries, reconstructs the random Scene, and allocates both entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            const bool endpointA = options.scenarioId == "endpoint-scene-a";
            const bool midpoint = options.scenarioId == "mid-transition";
            const bool noTexture = options.scenarioId == "animated-no-texture";
            renderer.configureTransitionTexture(
                transitionTexturePixels,
                512u,
                512u);
            renderer.configureTransition(
                endpointA ? 1.0f : midpoint ? 0.5f : noTexture ? 0.5f : 0.0f,
                midpoint);
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

        eastl::vector<WebglPostprocessingTransitionVertex> verticesA;
        eastl::vector<uint32_t> indicesA;
        eastl::vector<WebglPostprocessingTransitionVertex> verticesB;
        eastl::vector<uint32_t> indicesB;
        eastl::vector<WebglPostprocessingTransitionHostObjectData> objects;
        eastl::vector<WebglPostprocessingTransitionHostInstanceData> instanceDataA;
        eastl::vector<WebglPostprocessingTransitionHostInstanceData> instanceDataB;
        eastl::vector<uint8_t> transitionTexturePixels;
        WebglPostprocessingTransitionHostMaterialData materialDataA{};
        WebglPostprocessingTransitionHostMaterialData materialDataB{};
        GVM::Core::DeviceProxy device;
        bool captureWritten = false;
    };
}
