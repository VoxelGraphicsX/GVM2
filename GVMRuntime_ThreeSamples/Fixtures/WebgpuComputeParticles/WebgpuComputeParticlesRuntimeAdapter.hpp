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
    /** Mirrors the consolidated particle and grid vertex layout. */
    struct alignas(16) WebgpuComputeParticlesHostVertex
    {
        glm::vec4 positionAndCorner;
        glm::vec4 color;
    };

    /** Mirrors one Scene entity camera transform and material phase. */
    struct alignas(16) WebgpuComputeParticlesHostObjectData
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec4 viewportPhase;
    };

    /** Mirrors one mandatory RenderSet instance record. */
    struct alignas(16) WebgpuComputeParticlesHostInstanceData
    {
        glm::vec4 ordinal;
    };

    /** Mirrors the grid or sprite material controls. */
    struct alignas(16) WebgpuComputeParticlesHostMaterialData
    {
        glm::vec4 colorSizePhase;
    };

    /** Connects r185 particle state, physics, and RenderSet entities to the generated Renderer. */
    class WebgpuComputeParticlesRuntimeAdapter final
    {
    public:
        /** Allocates the grid and 200,000-instance sprite entity. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureControls(
                physics,
                clickPositionAndHit,
                inspectorEnabled);
        }

        /** Keeps deterministic controls immutable during fixed-frame warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures final RGBA8 and writes structural and semantic evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU staging after generated resources are destroyed. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates locked scenarios and allocates both Scene entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuComputeParticlesHostVertex> gridVertices;
        eastl::vector<uint32_t> gridIndices;
        eastl::vector<WebgpuComputeParticlesHostInstanceData> instances;
        glm::vec4 physics{};
        glm::vec4 clickPositionAndHit{};
        float inspectorEnabled = 1.0f;
        bool captureWritten = false;
    };
}
