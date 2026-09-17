#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one shader float4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) InstancingRaycastHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors one shader uint4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) InstancingRaycastHostUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    /** Mirrors one position-and-normal IcosahedronGeometry vertex. */
    struct alignas(16) InstancingRaycastHostVertex
    {
        InstancingRaycastHostFloat4 position;
        InstancingRaycastHostFloat4 normal;
    };

    /** Mirrors the shared camera, hemisphere direction, and material-selection component. */
    struct alignas(16) InstancingRaycastHostObjectData
    {
        glm::mat4 viewProjection;
        InstancingRaycastHostFloat4 hemisphereDirection;
        InstancingRaycastHostUint4 materialAndFlags;
    };

    /** Mirrors one mutable instance translation and linear-light color component entry. */
    struct alignas(16) InstancingRaycastHostInstanceData
    {
        InstancingRaycastHostFloat4 translation;
        InstancingRaycastHostFloat4 color;
    };

    /** Mirrors the private Phong material and hemisphere irradiance component entry. */
    struct alignas(16) InstancingRaycastHostMaterialData
    {
        InstancingRaycastHostFloat4 baseColor;
        InstancingRaycastHostFloat4 skyIrradiance;
        InstancingRaycastHostFloat4 groundIrradiance;
    };

    /** Stores the deterministic scenario, replay, camera, and active-instance state. */
    struct InstancingRaycastScenarioState
    {
        eastl::string replaySha256;
        eastl::string replayTarget;
        uint32_t replayEventCount = 0u;
        uint32_t replayLastEventFrame = 0u;
        uint32_t activeInstanceCount = 1000u;
        double cameraX = 10.0;
        double cameraY = 10.0;
        double cameraZ = 10.0;
        bool usesInputReplay = false;
        bool usesRaycast = false;
        bool requiresEntityReallocation = false;
    };

    /** Connects the generated instancing DSL renderer to deterministic r185 host behavior. */
    class WebglInstancingRaycastRuntimeAdapter final
    {
    public:
        /** Builds canonical geometry and instances and allocates the one Scene entity. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Applies scheduled ray-hit color updates or the active-count remove/reallocate mutation. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads the requested RGBA8 output and writes deterministic metadata and structure. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after generated renderer resource destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and prepares geometry, camera, raycast, and material state. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Allocates one instanced entity using the requested prefix of canonical instances. */
        GVM::Core::RenderEntityIndex allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, uint32_t instanceCount) const;

        /** Replaces the initial entity so active draw count changes without a second RenderSet. */
        void reallocateReducedCountEntity(GVM::Core::AbstractRendererImpl &renderer);

        /** Updates exactly one allocated instance component entry after a CPU ray hit. */
        void updateHitInstance(GVM::Core::AbstractRendererImpl &renderer, uint32_t instanceIndex);

        /** Writes exact RGBA8 bytes returned by the DSL-created readback texture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes fixed capture identity and the canonical replay digest when present. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes the one-Scene, one-RenderSet entity, instance, component, and pass snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        InstancingRaycastScenarioState scenarioState;
        eastl::vector<InstancingRaycastHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<InstancingRaycastHostInstanceData> instances;
        eastl::vector<uint32_t> raycastHitInstanceIds;
        eastl::vector<uint32_t> raycastHitFrames;
        eastl::vector<InstancingRaycastHostFloat4> raycastHitColors;
        InstancingRaycastHostObjectData objectData = {};
        InstancingRaycastHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex initialEntityIndex = UINT32_MAX;
        GVM::Core::RenderEntityIndex activeEntityIndex = UINT32_MAX;
        uint32_t appliedHitUpdateCount = 0u;
        bool entityReallocated = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
