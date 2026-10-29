#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglGeometriesBundle.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors the dedicated position, normal, and UV RenderSet vertex layout. */
    struct alignas(16) WebglGeometriesHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uv;
    };

    /** Mirrors one geometry transform and camera-relative point-light state. */
    struct alignas(16) WebglGeometriesHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
        glm::vec4 ambientPointIntensity;
    };

    /** Mirrors the mandatory non-instanced component entry. */
    struct alignas(16) WebglGeometriesHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors the shared Phong material constants. */
    struct alignas(16) WebglGeometriesHostMaterialData
    {
        glm::vec4 diffuseAndShininess;
        glm::vec4 specular;
    };

    /** Stores one fully packed geometry RenderSet entity. */
    struct WebglGeometriesEntityData
    {
        eastl::string logicalId;
        eastl::vector<WebglGeometriesHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglGeometriesHostObjectData objectData{};
        WebglGeometriesHostInstanceData instanceData{};
        WebglGeometriesHostMaterialData materialData{};
    };

    /** Connects the exact sixteen-mesh r185 bundle to one Scene-owned RenderSet. */
    class WebglGeometriesRuntimeAdapter final
    {
    public:
        /** Decodes all meshes, applies the deterministic frame transform, and allocates one Set. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(
                renderer,
                inDevice,
                options);
            renderer.configureOutput(
                options.width,
                options.height);
        }

        /** Keeps the fixed target-frame geometry payload unchanged during warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes the complete sixteen-entity structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU mesh and explicit mip staging storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the frozen scenarios and allocates all sixteen RenderSet entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglGeometriesEntityData> entities;
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> textureMipOffsets;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
