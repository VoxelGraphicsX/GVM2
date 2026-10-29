#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one shader float4 with an explicit 16-byte ABI. */
    struct alignas(16) Phase1LineHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors one endpoint-expanded segment vertex shared by both line cases. */
    struct alignas(16) Phase1LineHostVertex
    {
        Phase1LineHostFloat4 segmentStart;
        Phase1LineHostFloat4 segmentEnd;
        Phase1LineHostFloat4 startColor;
        Phase1LineHostFloat4 endColor;
        Phase1LineHostFloat4 endpointSideAndDistance;
    };

    /** Mirrors one entity's transform, target extent, and fog component. */
    struct alignas(16) Phase1LineHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        Phase1LineHostFloat4 viewportAndFog;
    };

    /** Mirrors the mandatory non-instanced component entry. */
    struct alignas(16) Phase1LineHostInstanceData
    {
        Phase1LineHostFloat4 translation;
    };

    /** Mirrors one line material including dash and fog configuration. */
    struct alignas(16) Phase1LineHostMaterialData
    {
        Phase1LineHostFloat4 baseColor;
        Phase1LineHostFloat4 dashAndFlags;
        Phase1LineHostFloat4 fogColor;
    };

    /** Stores one source point and its linear working-space vertex color. */
    struct Phase1LinePoint
    {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 linearColor = glm::vec3(1.0f);
    };

    /** Stores all RenderSet payloads and runtime identity for one line renderable. */
    struct Phase1LineEntityState
    {
        eastl::string logicalId;
        eastl::string storagePrefix;
        eastl::vector<Phase1LineHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        Phase1LineHostObjectData objectData = {};
        Phase1LineHostInstanceData instanceData = {};
        Phase1LineHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Stores camera and animation values written to the formal structural snapshot. */
    struct Phase1LinesScenarioState
    {
        double timeSeconds = 0.0;
        double cameraX = 0.0;
        double cameraY = 0.0;
        double pointerX = 0.0;
        double pointerY = 0.0;
    };

    /** Connects the two r185 line examples to one private RenderSet DSL shard. */
    class Phase1LinesPointsRuntimeAdapter final
    {
    public:
        /** Validates the case, builds deterministic geometry, and allocates every Scene entity. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            renderer.configureCase(options.caseId == "webgl_lines_dashed" ? 1u : 0u);
            initializeResources(renderer, inDevice, options);
        }

        /** Updates all per-entity matrices before the current deterministic frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads the final RGBA8 output and writes structural and capture evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Builds and allocates the selected example's unique Scene RenderSet. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Allocates one entity through existing RenderSet component semantics. */
        GVM::Core::RenderEntityIndex allocateEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, const Phase1LineEntityState &entity) const;

        /** Recomputes one frame's camera and object transforms. */
        void updateFrameState(uint32_t frameIndex, uint32_t width, uint32_t height);

        /** Writes the final tightly packed RGBA8 artifact. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes stable case, scenario, backend, and capture-layout metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes one-Scene, one-RenderSet entity and pass evidence. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<Phase1LineEntityState> entities;
        Phase1LinesScenarioState scenarioState;
        eastl::string caseId;
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        bool dashedCase = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
