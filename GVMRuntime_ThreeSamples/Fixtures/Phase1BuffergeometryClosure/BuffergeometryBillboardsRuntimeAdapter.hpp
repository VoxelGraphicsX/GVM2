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
    /** Mirrors one circle vertex with position and UV float4 attributes. */
    struct alignas(16) BuffergeometryBillboardsHostVertex
    {
        TexturedBoxHostFloat4 position;
        TexturedBoxHostFloat4 texCoord;
    };

    /** Mirrors the shared billboard camera transforms and animation time. */
    struct alignas(16) BuffergeometryBillboardsHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
        TexturedBoxHostFloat4 timeAndFlags;
    };

    /** Mirrors one seeded translate attribute for a billboard instance. */
    struct alignas(16) BuffergeometryBillboardsHostInstanceData
    {
        TexturedBoxHostFloat4 translate;
    };

    /** Mirrors the sole RawShaderMaterial color multiplier. */
    struct alignas(16) BuffergeometryBillboardsHostMaterialData
    {
        TexturedBoxHostFloat4 colorMultiplier;
    };

    /** Connects deterministic r185 billboards instance data to its dedicated generated Renderer. */
    class BuffergeometryBillboardsRuntimeAdapter final
    {
    public:
        /** Allocates the sole 75,000-instance RenderSet entity before the first frame. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the target-frame instance state immutable during deterministic warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected DSL output and writes strict metadata and Scene evidence. */
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
        /** Validates the scenario, decodes the locked texture, and allocates RenderSet components. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes the requested RGBA, metadata, and structural artifacts exactly once. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<BuffergeometryBillboardsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<BuffergeometryBillboardsHostInstanceData> instances;
        eastl::vector<uint8_t> circleTextureBytes;
        eastl::vector<uint64_t> circleMipOffsets;
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
