#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors the DSL vertex union shared by shape, line, and point entities. */
    struct alignas(16) WebglGeometryShapesHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uv;
    };

    /** Mirrors one entity model-view/projection and point-light payload. */
    struct alignas(16) WebglGeometryShapesHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 pointLightViewPositionAndIntensity;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglGeometryShapesHostInstanceData
    {
        glm::vec4 translation;
    };

    /** Mirrors one shape material and its texture-selection flag. */
    struct alignas(16) WebglGeometryShapesHostMaterialData
    {
        glm::vec4 baseColorAndFlags;
    };

    /** Stores one normalized renderable from the r185 shape example. */
    struct WebglGeometryShapesEntityData
    {
        eastl::string logicalId;
        eastl::vector<WebglGeometryShapesHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglGeometryShapesHostObjectData objectData{};
        WebglGeometryShapesHostInstanceData instanceData{};
        WebglGeometryShapesHostMaterialData materialData{};
        glm::mat4 model = glm::mat4(1.0f);
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Builds the complete 93-entity shape Scene and drives deterministic captures. */
    class WebglGeometryShapesRuntimeAdapter final
    {
    public:
        /** Generates CPU shape/extrusion/line/point geometry and allocates one RenderSet. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the deterministic group rotation for fixed animation frames. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the DSL target and writes RGBA, metadata, and Scene contract evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases generated CPU geometry after the renderer has been destroyed. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the frozen scenario contract and allocates all 93 entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Recomputes all entity matrices from the fixed camera and group angle. */
        void updateObjectData(uint32_t width, uint32_t height, uint32_t frameIndex);

        /** Writes one exact RGBA8 capture to the requested path. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes the stable capture metadata schema. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the one-Scene one-RenderSet 93-entity snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglGeometryShapesEntityData> entities;
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> textureMipOffsets;
        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
