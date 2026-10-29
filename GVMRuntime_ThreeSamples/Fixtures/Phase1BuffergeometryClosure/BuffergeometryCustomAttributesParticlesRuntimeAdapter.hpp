#pragma once

#include "Fixtures/Phase1TextureCases/TexturedBoxSampleData.hpp"
#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one corner record in the particle RenderSet vertex component. */
    struct alignas(16) BuffergeometryParticlesHostVertex
    {
        TexturedBoxHostFloat4 corner;
    };

    /** Mirrors the projection and model-view matrices of the particle entity. */
    struct alignas(16) BuffergeometryParticlesHostObjectData
    {
        glm::mat4 projectionMatrix;
        glm::mat4 modelViewMatrix;
    };

    /** Mirrors one particle position, size, and HSL color instance record. */
    struct alignas(16) BuffergeometryParticlesHostInstanceData
    {
        TexturedBoxHostFloat4 positionAndSize;
        TexturedBoxHostFloat4 color;
    };

    /** Mirrors the dedicated additive material multiplier. */
    struct alignas(16) BuffergeometryParticlesHostMaterialData
    {
        TexturedBoxHostFloat4 colorMultiplier;
    };

    /** Connects deterministic r185 particle data to its dedicated generated Renderer. */
    class BuffergeometryCustomAttributesParticlesRuntimeAdapter final
    {
    public:
        /** Allocates the sole 100,000-instance RenderSet entity before frame zero. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the target-frame particle state immutable during host warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the DSL output and writes strict metadata and Scene evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Clears CPU staging after generated resource destruction. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the case, constructs exact attributes, and allocates RenderSet components. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes requested capture, metadata, and structural artifacts exactly once. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<BuffergeometryParticlesHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<BuffergeometryParticlesHostInstanceData> instances;
        eastl::vector<uint8_t> sparkTextureBytes;
        eastl::vector<uint64_t> sparkMipOffsets;
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
