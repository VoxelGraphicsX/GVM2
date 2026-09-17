#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglPostprocessingMaskingData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors the grouped box transform component. */
    struct alignas(16) WebglPostprocessingMaskingHostObjectData
    {
        glm::mat4 modelViewProjection;
    };

    /** Mirrors the mandatory identity instance component. */
    struct alignas(16) WebglPostprocessingMaskingHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors the opaque mask material phase. */
    struct alignas(16) WebglPostprocessingMaskingHostMaterialData
    {
        glm::vec4 maskAndPhase;
    };

    /** Connects both masking Scenes and pinned photographs to the dedicated Renderer. */
    class WebglPostprocessingMaskingRuntimeAdapter final
    {
    public:
        /** Builds both target-frame meshes, allocates Scene1, and uploads photos. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureScene(
                torusVertices,
                torusIndices,
                torusUniforms,
                photograph1Mips,
                photograph2Mips);
        }

        /** Keeps the frozen target-frame masks immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads final RGBA8 output and writes Set and pass evidence. */
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
        /** Validates the scenario, decodes assets, and allocates the grouped box. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        eastl::vector<WebglPostprocessingMaskingVertex> boxVertices;
        eastl::vector<uint32_t> boxIndices;
        eastl::vector<WebglPostprocessingMaskingVertex> torusVertices;
        eastl::vector<uint32_t> torusIndices;
        WebglPostprocessingMaskingTorusUniforms torusUniforms{};
        eastl::vector<eastl::vector<uint8_t>> photograph1Mips;
        eastl::vector<eastl::vector<uint8_t>> photograph2Mips;
        WebglPostprocessingMaskingHostObjectData boxObjectData{};
        WebglPostprocessingMaskingHostInstanceData boxInstanceData{};
        WebglPostprocessingMaskingHostMaterialData boxMaterialData{};
        GVM::Core::DeviceProxy device;
        bool captureWritten = false;
    };
}
