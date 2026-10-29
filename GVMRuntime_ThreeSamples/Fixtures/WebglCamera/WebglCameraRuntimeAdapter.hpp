#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglCameraData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors both CPU-resolved model-view transforms and visibility. */
    struct alignas(16) WebglCameraHostObjectData final
    {
        glm::mat4 leftModelView;
        glm::mat4 rightModelView;
        glm::vec4 visibility;
    };

    /** Mirrors the mandatory identity instance component. */
    struct alignas(16) WebglCameraHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors one semantic material phase component. */
    struct alignas(16) WebglCameraHostMaterialData final
    {
        glm::vec4 phase;
    };

    /** Stores one logical camera-scene renderable before Set allocation. */
    struct WebglCameraEntity final
    {
        eastl::string logicalId;
        eastl::vector<WebglCameraVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglCameraHostObjectData objectData{};
        WebglCameraHostInstanceData instanceData{};
        WebglCameraHostMaterialData materialData{};
    };

    /** Connects r185 camera hierarchy data to its dedicated two-pass DSL Renderer. */
    class WebglCameraRuntimeAdapter final
    {
    public:
        /** Builds all six entities and configures both deterministic cameras. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureCameras(leftViewProjection, rightViewProjection);
        }

        /** Keeps target-frame hierarchy state immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads final RGBA8 and writes exact one-Set structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU geometry after generated renderer teardown begins. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the frozen contract and allocates exactly six Set entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        glm::mat4 leftViewProjection{1.0f};
        glm::mat4 rightViewProjection{1.0f};
        GVM::Core::DeviceProxy device;
        eastl::vector<WebglCameraEntity> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
