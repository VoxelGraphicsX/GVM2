#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one CPU-expanded point marker vertex in the RenderSet ABI. */
    struct alignas(16) WebglInteractiveRaycastingPointsHostVertex
    {
        glm::vec4 position;
        glm::vec4 color;
        glm::vec4 uv;
    };

    /** Stores the normalized marker position, size, and material slot. */
    struct alignas(16) WebglInteractiveRaycastingPointsHostObjectData
    {
        glm::vec4 offsetAndScale;
        glm::uvec4 materialAndFlags;
    };

    /** Stores the required per-entity identity instance component. */
    struct alignas(16) WebglInteractiveRaycastingPointsHostInstanceData
    {
        glm::vec4 offsetAndScale;
        glm::vec4 tint;
    };

    /** Stores one deterministic point-marker material tint. */
    struct alignas(16) WebglInteractiveRaycastingPointsHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Owns one expanded marker quad and its RenderSet entity index. */
    struct WebglInteractiveRaycastingPointsEntityData
    {
        eastl::vector<WebglInteractiveRaycastingPointsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglInteractiveRaycastingPointsHostObjectData objectData{};
        WebglInteractiveRaycastingPointsHostObjectData baseObjectData{};
        WebglInteractiveRaycastingPointsHostInstanceData instanceData{};
        WebglInteractiveRaycastingPointsHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the dedicated point-raycasting scene through one RenderSet. */
    class WebglInteractiveRaycastingPointsRuntimeAdapter final
    {
    public:
        /** Builds the deterministic marker field and allocates all entities. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Applies the locked pointer/raycast animation before a frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Captures RGBA8 and writes the scene contract evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases CPU staging data after the generated renderer stops. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenarios and allocates the marker field. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Updates marker scales and the deterministic raycast highlight. */
        void updateObjectData(uint32_t frameIndex);

        /** Writes the optional RGBA8 capture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes normalized capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the one-Scene RenderSet structural snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglInteractiveRaycastingPointsEntityData> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
