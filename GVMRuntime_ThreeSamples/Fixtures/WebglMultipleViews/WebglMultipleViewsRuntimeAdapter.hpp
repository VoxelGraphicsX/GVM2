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
    /** Mirrors one packed vertex used by the three-view scene. */
    struct alignas(16) WebglMultipleViewsHostVertex
    {
        glm::vec4 position;
        glm::vec4 normalAndFlags;
        glm::vec4 barycentric;
        glm::vec4 uv;
    };

    /** Mirrors one Scene entity's world transform and material phase component. */
    struct alignas(16) WebglMultipleViewsHostObjectData
    {
        glm::vec4 model0;
        glm::vec4 model1;
        glm::vec4 model2;
        glm::vec4 model3;
        glm::vec4 normalModel0;
        glm::vec4 normalModel1;
        glm::vec4 normalModel2;
        glm::vec4 normalModel3;
        glm::vec4 baseColorAndFlags;
    };


    /** Mirrors the mandatory one-entry instance payload. */
    struct alignas(16) WebglMultipleViewsHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one material component entry. */
    struct alignas(16) WebglMultipleViewsHostMaterialData
    {
        glm::vec4 baseColorAndFlags;
    };

    /** Mirrors the per-view visibility flags in the RenderSet ABI. */
    struct alignas(16) WebglMultipleViewsHostRenderFlags
    {
        uint32_t values[4]{};
    };

    /** Mirrors the three camera invocations without exposing generated DSL definitions in the host header. */
    struct WebglMultipleViewsHostInvocationData
    {
        glm::vec4 viewProjection0{};
        glm::vec4 viewProjection1{};
        glm::vec4 viewProjection2{};
        glm::vec4 viewProjection3{};
        glm::vec4 view0{};
        glm::vec4 view1{};
        glm::vec4 view2{};
        glm::vec4 view3{};
        glm::vec4 viewport{};
        glm::vec4 lightDirectionAndSelection{};
        glm::vec4 background{};
        glm::vec4 screenSize{};
    };

    /** Stores one exact CPU-generated entity before RenderSet allocation. */
    struct WebglMultipleViewsEntity
    {
        eastl::vector<WebglMultipleViewsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglMultipleViewsHostObjectData objectData{};
        WebglMultipleViewsHostInstanceData instanceData{};
        WebglMultipleViewsHostMaterialData materialData{};
        WebglMultipleViewsHostRenderFlags renderFlags{};
    };

    /** Builds the Three.js orientation-transform scene and records deterministic captures. */
    class WebglMultipleViewsRuntimeAdapter final
    {
    public:
        /** Generates the three-view scene and one Scene RenderSet. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureScene(invocationValues);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the deterministic camera state and uploads it through the generated renderer. */
        template <class RendererImpl>
        void beforeFrame(RendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex)
        {
            updateCameraState(options, frameIndex);
            renderer.configureScene(invocationValues);
        }

        /** Keeps the base-class callback available to non-generated callers. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the final target and writes metadata plus the RenderSet snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases CPU staging state after the generated renderer is destroyed. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked manifest scenarios and allocates all entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Rebuilds the camera matrices after a deterministic pointer replay. */
        void updateCameraState(const ThreeSampleHostOptions &options,
                               uint32_t frameIndex);

        /** Flattens the three invocation records into generated uniform payloads. */
        void rebuildInvocationValues();

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMultipleViewsEntity> entities;
        eastl::vector<uint8_t> shadowTexture;
        eastl::vector<WebglMultipleViewsHostInvocationData> invocations;
        eastl::vector<float> invocationValues;
        bool captureWritten = false;
    };
}
