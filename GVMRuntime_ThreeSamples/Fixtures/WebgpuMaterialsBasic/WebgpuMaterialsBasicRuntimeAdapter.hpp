#pragma once
#include "Host/ThreeSampleHostOptions.hpp"
#include "Fixtures/Phase1TextureCases/RgbaImageData.hpp"
#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>
#include <EASTL/vector.h>
#include <EASTL/array.h>
#include <EASTL/string.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one asset-expanded vertex in the Scene RenderSet ABI. */
    struct alignas(16) WebgpuMaterialsBasicHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uv;
    };
    /** Stores one head transform, camera basis, and cubemap material mode. */
    struct alignas(16) WebgpuMaterialsBasicHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 model;
        glm::vec4 cameraPositionAndFlags;
        glm::vec4 cameraRightAndTanHalfFov;
        glm::vec4 cameraUpAndAspect;
        glm::vec4 cameraForwardAndReserved;
    };
    /** Stores the mandatory one-entry instance component. */
    struct alignas(16) WebgpuMaterialsBasicHostInstanceData
    {
        glm::vec4 reserved;
    };
    /** Stores one material tint and reflection/refraction parameters. */
    struct alignas(16) WebgpuMaterialsBasicHostMaterialData
    {
        glm::vec4 baseColor;
        glm::vec4 parameters;
    };
    /** Owns one WebgpuMaterialsBasicSphere entity, six cube-face mip chains, and its RenderSet identity. */
    struct WebgpuMaterialsBasicEntityData
    {
        eastl::vector<WebgpuMaterialsBasicHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<eastl::vector<uint8_t>, 6u> textureBytes;
        eastl::array<eastl::vector<uint64_t>, 6u> textureMipOffsets;
        eastl::array<uint32_t, 6u> textureWidths = {};
        eastl::array<uint32_t, 6u> textureHeights = {};
        WebgpuMaterialsBasicHostObjectData objectData{};
        WebgpuMaterialsBasicHostObjectData baseObjectData{};
        WebgpuMaterialsBasicHostInstanceData instanceData{};
        WebgpuMaterialsBasicHostMaterialData materialData{};
        glm::vec3 randomPosition{};
        float randomScale = 1.0f;
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };
    /** Drives the dedicated cubemap scene through one RenderSet. */
    class WebgpuMaterialsBasicRuntimeAdapter final
    {
    public:
        /** Builds three deterministic spheres and allocates the Scene Set. */
        template <class RendererImpl> void initialize(RendererImpl &renderer,GVM::Core::DeviceProxy inDevice,const ThreeSampleHostOptions &options){initializeResources(renderer,inDevice,options);const auto &camera=entities.front().objectData;renderer.configureCameraData(camera.cameraPositionAndFlags.x,camera.cameraPositionAndFlags.y,camera.cameraPositionAndFlags.z,camera.cameraPositionAndFlags.w,camera.cameraRightAndTanHalfFov.x,camera.cameraRightAndTanHalfFov.y,camera.cameraRightAndTanHalfFov.z,camera.cameraRightAndTanHalfFov.w,camera.cameraUpAndAspect.x,camera.cameraUpAndAspect.y,camera.cameraUpAndAspect.z,camera.cameraUpAndAspect.w,camera.cameraForwardAndReserved.x,camera.cameraForwardAndReserved.y,camera.cameraForwardAndReserved.z,camera.cameraForwardAndReserved.w);renderer.configureOutput(options.width,options.height,options.scenarioId=="transparent-sorted"?1u:0u);}
        /** Updates the locked cubemap orbit. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &,const ThreeSampleHostOptions &,uint32_t);
        /** Captures RGBA8 and scene evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &,const ThreeSampleHostOptions &,uint32_t,GVM::RHI::Texture,uint32_t,uint32_t);
        /** Releases CPU staging data. */
        void shutdown(GVM::Core::AbstractRendererImpl &,const ThreeSampleHostOptions &);
    private:
        /** Validates scenarios and allocates cubemap entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &,GVM::Core::DeviceProxy,const ThreeSampleHostOptions &);
        /** Applies the deterministic orbit transform. */
        void updateObjectData(uint32_t);
        /** Writes the optional RGBA8 capture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &,const eastl::vector<uint8_t> &) const;
        /** Writes capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &,uint32_t,uint32_t,uint32_t,uint64_t) const;
        /** Writes the one-Scene snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &,uint32_t) const;
        /** Writes the canonical loader semantic sidecar. */
        void writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &) const;
        GVM::Core::DeviceProxy device; eastl::vector<WebgpuMaterialsBasicEntityData> entities; bool captureWritten=false;
    };
} // namespace GVM::ThreeSamples
