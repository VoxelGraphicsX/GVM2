#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors the packed vertex union consumed by the geometry-colors DSL. */
    struct alignas(16) WebglGeometryColorsHostVertex
    {
        glm::vec4 position;
        glm::vec4 normalOrEnd;
        glm::vec4 color;
        glm::vec4 uvOrCorner;
    };

    /** Mirrors one entity's camera transform and view-space light payload. */
    struct alignas(16) WebglGeometryColorsHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 lightViewAndIntensity;
        glm::vec4 viewport;
    };

    /** Mirrors the mandatory one-entry RenderSet instance component. */
    struct alignas(16) WebglGeometryColorsHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one solid, shadow, or wireframe phase selector. */
    struct alignas(16) WebglGeometryColorsHostMaterialData
    {
        glm::vec4 baseColorAndPhase;
    };

    /** Stores one normalized renderable and its RenderSet allocation payload. */
    struct WebglGeometryColorsEntityData
    {
        eastl::string logicalId;
        eastl::vector<WebglGeometryColorsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglGeometryColorsHostObjectData objectData{};
        WebglGeometryColorsHostInstanceData instanceData{};
        WebglGeometryColorsHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Builds the r185 geometry-colors Scene and drives deterministic captures. */
    class WebglGeometryColorsRuntimeAdapter final
    {
    public:
        /** Generates the three colored icosahedra, shadows, and wireframes. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Keeps the frozen geometry-colors camera unchanged during capture. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the DSL target and writes capture and structural evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases CPU staging data after generated renderer teardown. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenarios and allocates the single Scene Set. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Computes entity matrices and the camera-space light position. */
        void updateObjectData(uint32_t width, uint32_t height);

        /** Writes the requested RGBA8 capture artifact. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes stable capture metadata for the runner schema. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the one-Scene, nine-entity RenderSet snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglGeometryColorsEntityData> entities;
        eastl::vector<uint8_t> shadowTextureBytes;
        eastl::vector<uint64_t> shadowTextureMipOffsets;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
