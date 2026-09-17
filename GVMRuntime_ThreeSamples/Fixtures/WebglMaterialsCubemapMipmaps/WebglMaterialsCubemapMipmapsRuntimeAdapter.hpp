#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>
#include <EASTL/vector.h>
#include <EASTL/array.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one sphere vertex in the mipmap comparison RenderSet ABI. */
    struct alignas(16) WebglMaterialsCubemapMipmapsHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };
    /** Stores one sphere transform, camera basis, and material mode. */
    struct alignas(16) WebglMaterialsCubemapMipmapsHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 model;
        glm::vec4 cameraPosition;
        glm::vec4 cameraRightAndTanHalfFov;
        glm::vec4 cameraUpAndAspect;
        glm::vec4 cameraForward;
    };
    /** Stores the required identity instance component. */
    struct alignas(16) WebglMaterialsCubemapMipmapsHostInstanceData
    {
        glm::vec4 reserved;
    };
    /** Stores the fixed white MeshBasicMaterial state. */
    struct alignas(16) WebglMaterialsCubemapMipmapsHostMaterialData
    {
        glm::vec4 baseColor;
    };
    /** Owns one cubemap comparison sphere, six packed faces, and its entity index. */
    struct WebglMaterialsCubemapMipmapsEntityData
    {
        eastl::vector<WebglMaterialsCubemapMipmapsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<eastl::vector<uint8_t>, 6u> textureBytes;
        eastl::array<eastl::vector<uint64_t>, 6u> textureMipOffsets;
        eastl::array<uint32_t, 6u> textureWidths = {};
        eastl::array<uint32_t, 6u> textureHeights = {};
        WebglMaterialsCubemapMipmapsHostObjectData objectData{};
        WebglMaterialsCubemapMipmapsHostObjectData baseObjectData{};
        WebglMaterialsCubemapMipmapsHostInstanceData instanceData{};
        WebglMaterialsCubemapMipmapsHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };
    /** Drives the dedicated two-entity cubemap-mipmap scene. */
    class WebglMaterialsCubemapMipmapsRuntimeAdapter final
    {
    public:
        /** Builds the two comparison spheres and allocates one Scene Set. */
        template <class RendererImpl> void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options) { initializeResources(renderer, inDevice, options); renderer.configureOutput(options.width, options.height); }
        /** Updates the fixed orbit replay. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &, uint32_t);
        /** Captures RGBA8 and structural evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &, uint32_t, GVM::RHI::Texture, uint32_t, uint32_t);
        /** Releases CPU staging data. */
        void shutdown(GVM::Core::AbstractRendererImpl &, const ThreeSampleHostOptions &);
    private:
        /** Validates scenarios and allocates the comparison entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &, GVM::Core::DeviceProxy, const ThreeSampleHostOptions &);
        /** Applies the deterministic orbit transform. */
        void updateObjectData(uint32_t);
        /** Writes capture bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &, const eastl::vector<uint8_t> &) const;
        /** Writes capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &, uint32_t, uint32_t, uint32_t, uint64_t) const;
        /** Writes the one-Scene snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &, uint32_t) const;
        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMaterialsCubemapMipmapsEntityData> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
