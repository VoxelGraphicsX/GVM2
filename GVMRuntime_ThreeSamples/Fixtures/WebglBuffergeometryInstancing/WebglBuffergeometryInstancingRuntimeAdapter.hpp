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
    /** Mirrors one shader float4 with a fixed 16-byte Legacy/Experimental ABI. */
    struct alignas(16) BuffergeometryInstancingHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors one shader uint4 with a fixed 16-byte Legacy/Experimental ABI. */
    struct alignas(16) BuffergeometryInstancingHostUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    /** Mirrors the sole four-float Scene vertex record. */
    struct alignas(16) BuffergeometryInstancingHostVertex
    {
        BuffergeometryInstancingHostFloat4 position;
    };

    /** Mirrors the per-entity transform, animation, and material-selection component. */
    struct alignas(16) BuffergeometryInstancingHostObjectData
    {
        glm::mat4 modelViewProjection;
        BuffergeometryInstancingHostFloat4 timeAndSineTime;
        BuffergeometryInstancingHostUint4 materialAndFlags;
    };

    /** Mirrors one offset, color, and quaternion-pair instance component entry. */
    struct alignas(16) BuffergeometryInstancingHostInstanceData
    {
        BuffergeometryInstancingHostFloat4 offset;
        BuffergeometryInstancingHostFloat4 color;
        BuffergeometryInstancingHostFloat4 orientationStart;
        BuffergeometryInstancingHostFloat4 orientationEnd;
    };

    /** Mirrors the sole private raw-shader material component entry. */
    struct alignas(16) BuffergeometryInstancingHostMaterialData
    {
        BuffergeometryInstancingHostFloat4 baseColor;
    };

    /** Stores the locked animation, replay, and active-entity state for one scenario. */
    struct BuffergeometryInstancingScenarioState
    {
        eastl::string replaySha256;
        uint32_t activeInstanceCount = 50000u;
        uint32_t replayEventCount = 0u;
        uint32_t replayLastEventFrame = 0u;
        double virtualTimeMilliseconds = 0.0;
        float rotationY = 0.0f;
        float shaderTime = 0.0f;
        float sineTime = 0.0f;
        bool usesInputReplay = false;
        bool requiresEntityReallocation = false;
    };

    /** Connects deterministic Three-compatible CPU data to the generated RenderSet renderer. */
    class WebglBuffergeometryInstancingRuntimeAdapter final
    {
    public:
        /** Builds 50,000 instance records and allocates exactly one Scene entity. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Applies the GUI count mutation through remove/reallocate in the same RenderSet. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads the target RGBA8 frame and writes capture metadata plus the Scene snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after generated resources have completed. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates the frozen scenario and prepares the complete immutable instance stream. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Allocates the sole entity with a prefix of the canonical 50,000 instances. */
        GVM::Core::RenderEntityIndex allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, uint32_t instanceCount) const;

        /** Replaces the full-count entity with the replay-selected active-count entity. */
        void reallocateReducedCountEntity(GVM::Core::AbstractRendererImpl &renderer);

        /** Writes exact tightly packed RGBA8 bytes returned by the graphics queue. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes fixed dimensions, frame identity, seed, and replay identity. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes the one-Scene, one-RenderSet, one-entity runtime contract. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        BuffergeometryInstancingScenarioState scenarioState;
        eastl::vector<BuffergeometryInstancingHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<BuffergeometryInstancingHostInstanceData> instances;
        BuffergeometryInstancingHostObjectData objectData = {};
        BuffergeometryInstancingHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex initialEntityIndex = UINT32_MAX;
        GVM::Core::RenderEntityIndex activeEntityIndex = UINT32_MAX;
        uint32_t finalRandomState = 0u;
        bool entityReallocated = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
