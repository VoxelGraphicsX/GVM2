#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one generated plane, torus, or sphere vertex. */
    struct alignas(16) WebglRttHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors one entity camera and normal-transform component. */
    struct alignas(16) WebglRttHostObjectData final
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalTransform;
    };

    /** Mirrors the mandatory ordinary-entity instance component. */
    struct alignas(16) WebglRttHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors one shader phase, time, and Phong material component. */
    struct alignas(16) WebglRttHostMaterialData final
    {
        glm::vec4 phaseTimeAndColor;
        glm::vec4 specularAndShininess;
    };

    /** Stores one ordinary RenderSet entity before upload. */
    struct WebglRttHostEntity final
    {
        eastl::vector<WebglRttHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglRttHostObjectData objectData{};
        WebglRttHostInstanceData instanceData{};
        WebglRttHostMaterialData materialData{};
    };

    /** Connects the two r185 RTT Scenes to the dedicated generated renderer. */
    class WebglRttRuntimeAdapter final
    {
    public:
        /** Builds exact geometry and allocates both unique Scene Set instances. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Keeps target-frame transforms immutable during host warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes final RGBA8 plus complete two-Scene structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all sample-private CPU staging storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and fills both Scene Sets. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglRttHostEntity> rttEntities;
        eastl::vector<WebglRttHostEntity> mainEntities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
