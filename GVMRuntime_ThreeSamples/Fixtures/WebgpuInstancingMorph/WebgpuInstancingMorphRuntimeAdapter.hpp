#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "HorseGlbAsset.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors the union ground and Horse Scene vertex layout. */
    struct alignas(16) WebgpuInstancingMorphHostVertex final
    {
        glm::vec4 position;
        glm::vec4 color;
        glm::vec4 localVertexAndReserved;
    };

    /** Mirrors camera, shadow, fog, lighting, and entity-kind component data. */
    struct alignas(16) WebgpuInstancingMorphHostObjectData final
    {
        glm::mat4 viewProjection;
        glm::mat4 shadowViewProjection;
        glm::vec4 cameraPositionAndFogNear;
        glm::vec4 cameraForwardAndFogFar;
        glm::vec4 directionalLightAndEntityKind;
    };

    /** Mirrors one transform, color, and four packed morph-weight vectors. */
    struct alignas(16) WebgpuInstancingMorphHostInstanceData final
    {
        glm::mat4 model;
        glm::vec4 color;
        glm::vec4 morphWeights0;
        glm::vec4 morphWeights1;
        glm::vec4 morphWeights2;
        glm::vec4 morphWeights3;
    };

    /** Mirrors one private Standard-material record. */
    struct alignas(16) WebgpuInstancingMorphHostMaterialData final
    {
        glm::vec4 baseColor;
        glm::vec4 roughnessMetalnessAndFlags;
    };

    /** Mirrors the per-entity cast and receive shadow component. */
    struct alignas(16) WebgpuInstancingMorphHostShadowFlags final
    {
        glm::uvec4 values;
    };

    /** Owns one logical RenderSet entity and all of its uploaded component arrays. */
    struct WebgpuInstancingMorphEntityState final
    {
        eastl::string name;
        eastl::vector<WebgpuInstancingMorphHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebgpuInstancingMorphHostInstanceData> instances;
        eastl::vector<glm::vec4> morphTargets;
        WebgpuInstancingMorphHostObjectData objectData = {};
        WebgpuInstancingMorphHostMaterialData materialData = {};
        WebgpuInstancingMorphHostShadowFlags shadowFlags = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Connects the dedicated WebGPU instancing-morph renderer to deterministic r185 data. */
    class WebgpuInstancingMorphRuntimeAdapter final
    {
    public:
        /** Loads Horse.glb and allocates ground plus 1,024 Horse instances in one Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            renderer.configureInspector(
                options.scenarioId == "loader-snapshot" ? 0.0f : 1.0f);
            initializeResources(renderer, inDevice, options);
        }

        /** Updates the camera and all per-instance morph weights for the current fixed frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads RGBA8 and writes loader plus one-Scene one-Set contract evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases host-side arrays after generated resource teardown starts. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, builds exact CPU data, and allocates both entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Allocates one entity through the existing RenderSet component ABI. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const WebgpuInstancingMorphEntityState &entity) const;

        /** Recomputes main-camera component records for both entities. */
        void updateCamera(uint32_t frameIndex, uint32_t width, uint32_t height);

        /** Recomputes all 1,024 independent clip samples for one fixed frame. */
        void updateMorphWeights(uint32_t frameIndex);

        GVM::Core::DeviceProxy device;
        HorseGlbAsset horseAsset;
        eastl::array<WebgpuInstancingMorphEntityState, 2u> entities;
        eastl::vector<float> timeOffsets;
        eastl::string horseSha256;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
