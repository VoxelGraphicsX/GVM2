#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one clearcoat sphere vertex and analytic normal in the RenderSet ABI. */
    struct alignas(16) WebglMaterialsPhysicalClearcoatHostVertex { glm::vec4 position; glm::vec4 normal; };
    /** Stores one sphere transform and material slot. */
    struct alignas(16) WebglMaterialsPhysicalClearcoatHostObjectData { glm::vec4 offsetAndScale; glm::uvec4 materialAndFlags; };
    /** Stores the mandatory identity instance component. */
    struct alignas(16) WebglMaterialsPhysicalClearcoatHostInstanceData { glm::vec4 offsetAndScale; glm::vec4 tint; };
    /** Stores base color, roughness, metalness, and clearcoat parameters. */
    struct alignas(16) WebglMaterialsPhysicalClearcoatHostMaterialData
    {
        glm::vec4 baseColor;
        glm::vec4 physicalParameters;
    };

    /** Owns one sphere and its RenderSet entity index. */
    struct WebglMaterialsPhysicalClearcoatEntityData
    {
        eastl::vector<WebglMaterialsPhysicalClearcoatHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglMaterialsPhysicalClearcoatHostObjectData objectData{};
        WebglMaterialsPhysicalClearcoatHostObjectData baseObjectData{};
        WebglMaterialsPhysicalClearcoatHostInstanceData instanceData{};
        WebglMaterialsPhysicalClearcoatHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the dedicated clearcoat scene through one RenderSet. */
    class WebglMaterialsPhysicalClearcoatRuntimeAdapter final
    {
    public:
        /** Builds four deterministic spheres and allocates the Scene Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        { initializeResources(renderer, inDevice, options); renderer.configureOutput(options.width, options.height); }
        /** Updates the fixed orbit/clearcoat animation. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &, uint32_t frameIndex);
        /** Captures RGBA8 and structural evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &, uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);
        /** Releases CPU staging data. */
        void shutdown(GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &);
    private:
        /** Validates the locked clearcoat scenarios and allocates entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &, GVM::Core::DeviceProxy, const ThreeSampleHostOptions &);
        /** Applies the deterministic orbit and highlight update. */
        void updateObjectData(uint32_t frameIndex);
        /** Writes the optional RGBA8 capture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &, const eastl::vector<uint8_t> &) const;
        /** Writes normalized capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &, uint32_t, uint32_t, uint32_t, uint64_t) const;
        /** Writes the one-Scene RenderSet snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &, uint32_t) const;
        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMaterialsPhysicalClearcoatEntityData> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
