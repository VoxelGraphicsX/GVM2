#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one vertex in the RectAreaLight Scene RenderSet ABI. */
    struct alignas(16) WebglLightsRectarealightHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 color;
    };

    /** Mirrors one entity transform and helper classification. */
    struct alignas(16) WebglLightsRectarealightHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 model;
        glm::vec4 positionAndKind;
    };

    /** Mirrors the required one-entry instance component. */
    struct alignas(16) WebglLightsRectarealightHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one material tint and phase record. */
    struct alignas(16) WebglLightsRectarealightHostMaterialData
    {
        glm::vec4 baseColor;
        glm::vec4 parameters;
    };

    /** Owns one floor, one knot, and six helper entities before RenderSet allocation. */
    struct WebglLightsRectarealightHostEntity
    {
        eastl::vector<WebglLightsRectarealightHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglLightsRectarealightHostObjectData objectData{};
        WebglLightsRectarealightHostObjectData baseObjectData{};
        WebglLightsRectarealightHostInstanceData instanceData{};
        WebglLightsRectarealightHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Builds deterministic spotlight geometry and records its RGBA8 capture. */
    class WebglLightsRectarealightRuntimeAdapter final
    {
    public:
        /** Generates the eight RenderSet entities and configures fixed capture output. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the three deterministic area-light helper transforms. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the final target and writes capture metadata and a structural snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases CPU staging data after the renderer shuts down. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates options and allocates the single Scene RenderSet. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Recomputes camera and animated helper matrices for one fixed frame. */
        void updateObjectData(uint32_t frameIndex);

        /** Writes the optional RGBA8 capture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes standardized capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frame,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the one-Scene RenderSet structural snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frame) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLightsRectarealightHostEntity> entities;
        bool captureWritten = false;
        bool orbitScenario = false;
    };
}
