#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one triangle-expanded single-pixel corner. */
    struct alignas(16) WebgpuComputePointsHostVertex
    {
        glm::vec4 corner;
    };

    /** Mirrors the orthographic viewport component. */
    struct alignas(16) WebgpuComputePointsHostObjectData
    {
        glm::vec4 viewport;
    };

    /** Mirrors one mandatory RenderSet instance record. */
    struct alignas(16) WebgpuComputePointsHostInstanceData
    {
        glm::vec4 ordinal;
    };

    /** Mirrors the private point material component. */
    struct alignas(16) WebgpuComputePointsHostMaterialData
    {
        glm::vec4 opacity;
    };

    /** Mirrors pointer and orthographic bounds consumed by Compute. */
    struct alignas(16) WebgpuComputePointsHostControls
    {
        glm::vec4 pointerAndLimit;
        glm::vec4 inspectorEnabled;
    };

    /** Connects the r185 point-state Compute algorithm to its dedicated Renderer. */
    class WebgpuComputePointsRuntimeAdapter final
    {
    public:
        /** Allocates one 300,000-instance entity and uploads deterministic state. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureControls(
                controls.pointerAndLimit,
                controls.inspectorEnabled.x);
        }

        /** Keeps scenario controls immutable throughout deterministic warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads final RGBA8 output and emits structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU staging after generated resources are destroyed. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenarios and allocates the sole Scene entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuComputePointsHostInstanceData> instances;
        WebgpuComputePointsHostControls controls{};
        bool captureWritten = false;
    };
}
