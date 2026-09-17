#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one triangle-expanded point sprite vertex. */
    struct alignas(16) WebgpuInstancePointsHostVertex
    {
        glm::vec4 cornerAndUv;
    };

    /** Mirrors both camera matrices, viewport, time, and GUI widths. */
    struct alignas(16) WebgpuInstancePointsHostObjectData
    {
        glm::mat4 mainViewProjection;
        glm::mat4 insetViewProjection;
        glm::vec4 viewportAndTime;
        glm::vec4 widthsAndPulse;
    };

    /** Mirrors one Hilbert spline point and its linear color. */
    struct alignas(16) WebgpuInstancePointsHostInstanceData
    {
        glm::vec4 position;
        glm::vec4 color;
    };

    /** Mirrors circular sprite opacity and coverage controls. */
    struct alignas(16) WebgpuInstancePointsHostMaterialData
    {
        glm::vec4 opacityAndCoverage;
    };

    /** Connects Hilbert/Catmull data to the dedicated Compute and two-pass Renderer. */
    class WebgpuInstancePointsRuntimeAdapter final
    {
    public:
        /** Builds the 256 points, allocates one entity, and configures the size Compute pass. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureCompute(
                float(options.targetFrame) / 60.0f,
                minimumWidth,
                maximumWidth,
                pulseSpeed);
        }

        /** Keeps the selected target-frame Compute controls immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads RGBA8 output and writes dual-viewport structural evidence. */
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
        /** Validates one scenario and allocates the sole 256-instance Scene entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuInstancePointsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebgpuInstancePointsHostInstanceData> instances;
        float minimumWidth = 6.0f;
        float maximumWidth = 20.0f;
        float pulseSpeed = 6.0f;
        bool captureWritten = false;
    };
}
