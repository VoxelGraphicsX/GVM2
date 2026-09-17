#pragma once
#include "Host/ThreeSampleHostOptions.hpp"
#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>
#include <EASTL/vector.h>
#include <glm/vec4.hpp>
#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one cubemap sphere vertex in the RenderSet ABI. */
    struct alignas(16) WebglMaterialsCubemapRenderToMipmapsHostVertex { glm::vec4 position; glm::vec4 color; };
    /** Stores one cubemap object transform and material slot. */
    struct alignas(16) WebglMaterialsCubemapRenderToMipmapsHostObjectData { glm::vec4 offsetAndScale; glm::uvec4 materialAndFlags; };
    /** Stores the required identity instance component. */
    struct alignas(16) WebglMaterialsCubemapRenderToMipmapsHostInstanceData { glm::vec4 offsetAndScale; glm::vec4 tint; };
    /** Stores one cubemap material tint. */
    struct alignas(16) WebglMaterialsCubemapRenderToMipmapsHostMaterialData { glm::vec4 baseColor; };
    /** Owns one cubemap sphere and its entity index. */
    struct WebglMaterialsCubemapRenderToMipmapsEntityData { eastl::vector<WebglMaterialsCubemapRenderToMipmapsHostVertex> vertices; eastl::vector<uint32_t> indices; WebglMaterialsCubemapRenderToMipmapsHostObjectData objectData{}; WebglMaterialsCubemapRenderToMipmapsHostObjectData baseObjectData{}; WebglMaterialsCubemapRenderToMipmapsHostInstanceData instanceData{}; WebglMaterialsCubemapRenderToMipmapsHostMaterialData materialData{}; GVM::Core::RenderEntityIndex entityIndex=UINT32_MAX; };
    /** Drives the dedicated cubemap scene through one RenderSet. */
    class WebglMaterialsCubemapRenderToMipmapsRuntimeAdapter final
    {
    public:
        /** Builds three deterministic spheres and allocates the Scene Set. */
        template <class RendererImpl> void initialize(RendererImpl &renderer,GVM::Core::DeviceProxy inDevice,const ThreeSampleHostOptions &options){initializeResources(renderer,inDevice,options);renderer.configureOutput(options.width,options.height);}
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
        GVM::Core::DeviceProxy device; eastl::vector<WebglMaterialsCubemapRenderToMipmapsEntityData> entities; bool captureWritten=false;
    };
} // namespace GVM::ThreeSamples
