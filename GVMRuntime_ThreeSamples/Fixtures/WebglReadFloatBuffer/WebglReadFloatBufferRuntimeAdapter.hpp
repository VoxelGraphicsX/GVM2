#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one DSL float-readback vertex component. */
    struct alignas(16) WebglReadFloatBufferHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors one DSL Scene object transform component. */
    struct alignas(16) WebglReadFloatBufferHostObjectData final
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalTransform;
    };

    /** Mirrors the mandatory one-entry ordinary instance component. */
    struct alignas(16) WebglReadFloatBufferHostInstanceData final
    {
        glm::vec4 translation;
    };

    /** Mirrors one plane or Phong torus material record. */
    struct alignas(16) WebglReadFloatBufferHostMaterialData final
    {
        glm::vec4 kindTimeAndColor;
        glm::vec4 specularAndShininess;
    };

    /** Connects the exact float RTT example to its dedicated RenderSet renderer. */
    class WebglReadFloatBufferRuntimeAdapter final
    {
    public:
        /** Builds three entities and allocates them in the sole Scene RenderSet. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the fixed camera and deterministic target frame unchanged. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads both the final RGBA8 image and the full float RTT. */
        template <class RendererImpl>
        void afterFrame(
            RendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height)
        {
            captureTargetFrame(
                options,
                frameIndex,
                readbackTexture,
                renderer.getFloatReadbackTextureHandle(),
                width,
                height);
        }

        /** Releases all CPU payloads after generated renderer destruction. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and allocates the three Scene entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Captures the final image and selected center float pixel. */
        void captureTargetFrame(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            GVM::RHI::Texture floatReadbackTexture,
            uint32_t width,
            uint32_t height);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglReadFloatBufferHostVertex> planeVertices;
        eastl::vector<uint32_t> planeIndices;
        eastl::vector<WebglReadFloatBufferHostVertex> torusVertices;
        eastl::vector<uint32_t> torusIndices;
        WebglReadFloatBufferHostObjectData planeObject{};
        WebglReadFloatBufferHostObjectData torusObjectA{};
        WebglReadFloatBufferHostObjectData torusObjectB{};
        WebglReadFloatBufferHostInstanceData instanceData{};
        WebglReadFloatBufferHostMaterialData planeMaterial{};
        WebglReadFloatBufferHostMaterialData torusMaterialA{};
        WebglReadFloatBufferHostMaterialData torusMaterialB{};
        uint32_t targetFrame = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
