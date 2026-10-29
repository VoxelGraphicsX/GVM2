#pragma once

#include "Fixtures/Phase1TextureCases/TexturedBoxSampleData.hpp"
#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one CPU-expanded snowflake vertex. */
    struct alignas(16) PointsSpritesHostVertex
    {
        TexturedBoxHostFloat4 position;
        TexturedBoxHostFloat4 corner;
    };

    /** Mirrors one point layer's model-view and projection matrices. */
    struct alignas(16) PointsSpritesHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
    };

    /** Mirrors the required non-instanced component record. */
    struct alignas(16) PointsSpritesHostInstanceData
    {
        TexturedBoxHostFloat4 reserved;
    };

    /** Mirrors per-layer color, size, and texture flags. */
    struct alignas(16) PointsSpritesHostMaterialData
    {
        TexturedBoxHostFloat4 colorAndSize;
        TexturedBoxHostFloat4 flags;
    };

    /** Stores one complete RenderSet entity before allocation. */
    struct PointsSpritesEntityState
    {
        eastl::vector<PointsSpritesHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        PointsSpritesHostObjectData objectData = {};
        PointsSpritesHostInstanceData instanceData = {};
        PointsSpritesHostMaterialData materialData = {};
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> mipOffsets;
        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
    };

    /** Connects deterministic five-layer r185 snowflakes to the generated renderer. */
    class WebglPointsSpritesRuntimeAdapter final
    {
    public:
        /** Builds shared random geometry and allocates five ordinary entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps target-frame entity state immutable during host warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes the one-Set structural contract. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all private CPU staging storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the case and allocates every entity component. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes requested strict artifacts exactly once. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::array<PointsSpritesEntityState, 5u> entities;
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
