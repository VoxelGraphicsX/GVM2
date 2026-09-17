#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors the DSL's packed orientation-transform vertex component. */
    struct alignas(16) WebglMathOrientationTransformHostVertex
    {
        glm::vec4 position;
        glm::vec4 normalAndFlags;
        glm::vec4 barycentric;
    };

    /** Mirrors one Scene entity's matrix and material phase component. */
    struct alignas(16) WebglMathOrientationTransformHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalMatrix;
        glm::vec4 baseColorAndFlags;
    };

    /** Mirrors the mandatory one-entry instance payload. */
    struct alignas(16) WebglMathOrientationTransformHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one material component entry. */
    struct alignas(16) WebglMathOrientationTransformHostMaterialData
    {
        glm::vec4 baseColorAndFlags;
    };

    /** Mirrors orientation visibility and selection flags. */
    struct alignas(16) WebglMathOrientationTransformHostRenderFlags
    {
        uint32_t values[4]{};
    };

    /** Stores one exact CPU-generated entity before RenderSet allocation. */
    struct WebglMathOrientationTransformEntity
    {
        eastl::vector<WebglMathOrientationTransformHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglMathOrientationTransformHostObjectData objectData{};
        WebglMathOrientationTransformHostInstanceData instanceData{};
        WebglMathOrientationTransformHostMaterialData materialData{};
        WebglMathOrientationTransformHostRenderFlags renderFlags{};
    };

    /** Builds the Three.js orientation-transform scene and records deterministic captures. */
    class WebglMathOrientationTransformRuntimeAdapter final
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
        eastl::vector<WebglMathOrientationTransformEntity> entities;
        eastl::string inputReplaySha256;
        uint32_t inputReplayEventCount = 0u;
        bool captureWritten = false;
    };
}
