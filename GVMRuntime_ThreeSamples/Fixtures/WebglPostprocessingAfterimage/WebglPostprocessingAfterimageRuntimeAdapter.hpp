#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one exact segmented BoxGeometry position and face normal. */
    struct alignas(16) AfterimageHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors the current deterministic model-view transforms. */
    struct alignas(16) AfterimageHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
    };

    /** Mirrors the mandatory non-instanced translation component. */
    struct alignas(16) AfterimageHostInstanceData
    {
        glm::vec4 translation;
    };

    /** Mirrors the private MeshNormal material multiplier. */
    struct alignas(16) AfterimageHostMaterialData
    {
        glm::vec4 normalColorMultiplier;
    };

    /** Stores the only Scene entity and its current component payloads. */
    struct AfterimageEntityState
    {
        eastl::vector<AfterimageHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        AfterimageHostObjectData objectData = {};
        AfterimageHostInstanceData instanceData = {};
        AfterimageHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Stores the locked feedback control and replay identity. */
    struct AfterimageScenarioState
    {
        float damp = 0.96f;
        bool enabled = true;
        eastl::string replaySha256;
        uint32_t replayEventCount = 0u;
    };

    /** Connects the exact box animation and temporal controls to the DSL renderer. */
    class WebglPostprocessingAfterimageRuntimeAdapter final
    {
    public:
        /** Allocates one Scene entity and all temporal scenario resources. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureScenario(
                options.width,
                options.height,
                scenario.damp,
                scenario.enabled);
        }

        /** Uploads the exact r185 Euler rotation before every frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the final DSL output and writes strict evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU-side box storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, builds geometry, and allocates the Scene Set. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Allocates the one ordinary entity through existing RenderSet semantics. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder) const;

        /** Recomputes exact model-view transforms for one frame index. */
        void updateObjectData(
            uint32_t width,
            uint32_t height,
            uint32_t frameIndex);

        /** Writes RGBA8, metadata, structural, and semantic artifacts. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        AfterimageEntityState entity;
        AfterimageScenarioState scenario;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
