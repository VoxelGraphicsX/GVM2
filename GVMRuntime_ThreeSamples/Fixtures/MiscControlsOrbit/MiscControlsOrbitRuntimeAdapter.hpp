#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one canonical triangle-list cone vertex. */
    struct alignas(16) MiscControlsOrbitHostVertex
    {
        glm::vec4 position;
    };

    /** Mirrors the camera, lights, and fog per-entity component. */
    struct alignas(16) MiscControlsOrbitHostObjectData
    {
        glm::mat4 viewProjection;
        glm::mat4 view;
        glm::vec4 whiteLightDirection;
        glm::vec4 blueLightDirection;
        glm::vec4 fogAndReserved;
    };

    /** Mirrors one deterministic cone instance translation. */
    struct alignas(16) MiscControlsOrbitHostInstanceData
    {
        glm::vec4 translation;
    };

    /** Mirrors the private flat MeshPhong material component. */
    struct alignas(16) MiscControlsOrbitHostMaterialData
    {
        glm::vec4 diffuseAndShininess;
        glm::vec4 specularAndReserved;
        glm::vec4 ambientAndReserved;
    };

    /** Connects deterministic orbit-control CPU state to the generated renderer. */
    class MiscControlsOrbitRuntimeAdapter final
    {
    public:
        /** Builds the cone template and allocates one 500-instance Scene entity. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            renderer.configureOutput(options.width, options.height);
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the immutable target-frame camera state unchanged. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes RGBA, metadata, and the one-Set structural snapshot. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases Host-side deterministic arrays after GPU completion. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and uploads the sole RenderSet entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<MiscControlsOrbitHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<MiscControlsOrbitHostInstanceData> instances;
        MiscControlsOrbitHostObjectData objectData = {};
        MiscControlsOrbitHostMaterialData materialData = {};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
