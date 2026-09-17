#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one radial-blur tetrahedron vertex component. */
    struct alignas(16) WebgpuPostprocessingRadialBlurHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors the parent Group and camera object component. */
    struct alignas(16) WebgpuPostprocessingRadialBlurHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
    };

    /** Mirrors one canonical tetrahedron instance component. */
    struct alignas(16) WebgpuPostprocessingRadialBlurHostInstanceData
    {
        glm::vec4 transformColumn0;
        glm::vec4 transformColumn1;
        glm::vec4 transformColumn2;
        glm::vec4 transformColumn3;
        glm::vec4 color;
    };

    /** Mirrors the one standard material component. */
    struct alignas(16) WebgpuPostprocessingRadialBlurHostMaterialData
    {
        glm::vec4 roughnessMetalnessPadding;
    };

    /** Connects the locked radial-blur scenes to one dedicated RenderSet Renderer. */
    class WebgpuPostprocessingRadialBlurRuntimeAdapter final
    {
    public:
        /** Allocates the exact tetrahedron instances and configures the blur graph. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureCase(
                weight,
                decay,
                exposure,
                sampleCount,
                blurEnabled ? 1.0f : 0.0f,
                animationEnabled ? 1.0f : 0.0f,
                inspectorEnabled ? 1.0f : 0.0f);
        }

        /** Leaves the fixed scenario state immutable during warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes one-Set structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU-side instance and topology storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and allocates its only entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuPostprocessingRadialBlurHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebgpuPostprocessingRadialBlurHostObjectData objectData = {};
        eastl::vector<WebgpuPostprocessingRadialBlurHostInstanceData> instances;
        WebgpuPostprocessingRadialBlurHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        float weight = 0.9f;
        float decay = 0.95f;
        float exposure = 5.0f;
        float sampleCount = 32.0f;
        bool blurEnabled = true;
        bool animationEnabled = true;
        bool inspectorEnabled = true;
        bool captureWritten = false;
    };
}
