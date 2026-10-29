#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors the selective-light teapot vertex component, including UVs. */
    struct alignas(16) WebgpuLightsSelectiveHostVertex
    {
        glm::vec4 position;
        glm::vec4 normalAndFlags;
        glm::vec4 uvAndFlags;
    };

    /** Mirrors one Scene entity's camera and four point-light records. */
    struct alignas(16) WebgpuLightsSelectiveHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 model;
        glm::mat4 normalMatrix;
        glm::vec4 cameraPosition;
        glm::mat4 viewNormalMatrix;
        glm::vec4 lightPositionPower0;
        glm::vec4 lightPositionPower1;
        glm::vec4 lightPositionPower2;
        glm::vec4 lightPositionPower3;
        glm::vec4 lightColor0;
        glm::vec4 lightColor1;
        glm::vec4 lightColor2;
        glm::vec4 lightColor3;
    };

    /** Mirrors the mandatory one-entry instance payload. */
    struct alignas(16) WebgpuLightsSelectiveHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one StandardNodeMaterial's selective-light controls. */
    struct alignas(16) WebgpuLightsSelectiveHostMaterialData
    {
        glm::vec4 baseColorAndFlags;
        // x: light mask bits, y: roughness, z: metalness, w: marker phase.
        glm::vec4 lightMaskRoughnessMetalnessAndPhase;
    };

    /** Mirrors the dedicated selective-light component. */
    struct alignas(16) WebgpuLightsSelectiveHostLightData
    {
        glm::vec4 maskAndIntensity;
    };

    /** Mirrors per-entity visibility and selection flags. */
    struct alignas(16) WebgpuLightsSelectiveHostRenderFlags
    {
        uint32_t values[4]{};
    };

    /** Stores one exact CPU-generated teapot or point-light marker entity. */
    struct WebgpuLightsSelectiveEntity
    {
        eastl::vector<WebgpuLightsSelectiveHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebgpuLightsSelectiveHostObjectData objectData{};
        WebgpuLightsSelectiveHostInstanceData instanceData{};
        WebgpuLightsSelectiveHostMaterialData materialData{};
        WebgpuLightsSelectiveHostLightData lightData{};
        WebgpuLightsSelectiveHostRenderFlags renderFlags{};
    };

    /** Builds the Three.js orientation-transform scene and records deterministic captures. */
    class WebgpuLightsSelectiveRuntimeAdapter final
    {
    public:
        /** Generates the cone, target sphere, wire sphere, and one Scene RenderSet. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the deterministic rotate-towards state before the requested frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the final target and writes metadata plus the RenderSet snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases CPU staging state after the generated renderer is destroyed. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked manifest scenarios and allocates all entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuLightsSelectiveEntity> entities;
        eastl::vector<uint8_t> normalPixels;
        eastl::vector<uint8_t> roughnessPixels;
        bool captureWritten = false;
    };
}
