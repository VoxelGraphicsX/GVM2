#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one packed sky or bird vertex. */
    struct alignas(16) WebgpuComputeBirdsHostVertex
    {
        glm::vec4 positionAndVertex;
    };

    /** Mirrors one Scene entity transform and material phase. */
    struct alignas(16) WebgpuComputeBirdsHostObjectData
    {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec4 phaseAndFog;
    };

    /** Mirrors one mandatory RenderSet instance record. */
    struct alignas(16) WebgpuComputeBirdsHostInstanceData
    {
        glm::vec4 initialPosition;
        glm::vec4 initialVelocity;
        glm::vec4 initialPhase;
        glm::vec4 ordinal;
    };

    /** Mirrors one Scene material and phase record. */
    struct alignas(16) WebgpuComputeBirdsHostMaterialData
    {
        glm::vec4 colorAndPhase;
    };

    /** Connects deterministic r185 flock data to its dedicated DSL Renderer. */
    class WebgpuComputeBirdsRuntimeAdapter final
    {
    public:
        /** Allocates the two Scene entities and uploads all 8192 initial states. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureState(
                controls,
                rayOrigin,
                rayDirection,
                inspectorEnabled);
        }

        /** Preserves the host lifecycle boundary before each generated frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads final RGBA8 output and writes RenderSet and Compute evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU staging after generated resources are destroyed. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and records both RenderSet allocations. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuComputeBirdsHostInstanceData> birdInstances;
        glm::vec4 controls{};
        glm::vec4 rayOrigin{};
        glm::vec4 rayDirection{};
        float inspectorEnabled = 0.0f;
        uint32_t finalRandomState = 0u;
        eastl::string inputReplaySha256;
        uint32_t skyVertexCount = 0u;
        uint32_t skyIndexCount = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
