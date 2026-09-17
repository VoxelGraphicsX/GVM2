#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one camera-specialized teapot RenderSet vertex. */
    struct alignas(16) WebglGeometryTeapotHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uv;
    };

    /** Mirrors the fixed camera and light Scene component. */
    struct alignas(16) WebglGeometryTeapotHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::vec4 cameraPosition;
        glm::vec4 directionalLight;
        glm::vec4 cameraRightAndTanHalfFov;
        glm::vec4 cameraUpAndAspect;
        glm::vec4 cameraForwardAndReserved;
    };

    /** Mirrors the required non-instanced component value. */
    struct alignas(16) WebglGeometryTeapotHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors the active glossy teapot material component. */
    struct alignas(16) WebglGeometryTeapotHostMaterialData
    {
        glm::vec4 diffuseAndShininess;
        glm::vec4 specularAndMode;
        glm::vec4 ambientAndDirectionalIntensity;
    };

    /** Connects exact r185 CPU tessellation to the dedicated teapot Renderer. */
    class WebglGeometryTeapotRuntimeAdapter final
    {
    public:
        /** Builds and allocates the one-entity teapot Scene RenderSet. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Leaves the frozen target-frame Scene immutable. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes the RGBA8 capture and one-Set structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU-owned tessellation storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and allocates exact tessellated geometry. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglGeometryTeapotHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        std::array<eastl::vector<uint8_t>, 6u> environmentPixels;
        std::array<eastl::vector<uint64_t>, 6u> environmentMipOffsets;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
