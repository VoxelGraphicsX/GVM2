#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebgpuMultipleCanvasData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors the DSL's packed orientation-transform vertex component. */
    struct alignas(16) WebgpuMultipleCanvasHostVertex
    {
        glm::vec4 position;
        glm::vec4 normalAndFlags;
        glm::vec4 barycentric;
    };

    /** Mirrors one Scene entity's matrix and material phase component. */
    struct alignas(16) WebgpuMultipleCanvasHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalMatrix;
        glm::vec4 viewport;
        glm::vec4 baseColorAndFlags;
    };

    /** Mirrors the mandatory one-entry instance payload. */
    struct alignas(16) WebgpuMultipleCanvasHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one material component entry. */
    struct alignas(16) WebgpuMultipleCanvasHostMaterialData
    {
        glm::vec4 baseColorAndFlags;
    };

    /** Stores one exact CPU-generated entity before RenderSet allocation. */
    struct WebgpuMultipleCanvasEntity
    {
        eastl::vector<WebgpuMultipleCanvasHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebgpuMultipleCanvasHostObjectData objectData{};
        WebgpuMultipleCanvasHostInstanceData instanceData{};
        WebgpuMultipleCanvasHostMaterialData materialData{};
    };

    /** Builds the Three.js orientation-transform scene and records deterministic captures. */
    class WebgpuMultipleCanvasRuntimeAdapter final
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
            renderer.configureSimpleGeometry(
                simpleVertices, simpleIndices, simpleFirstIndices, simpleIndexCounts);
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
        eastl::vector<WebgpuMultipleCanvasEntity> entities;
        eastl::vector<WebgpuMultipleCanvasSimpleVertex> simpleVertices;
        eastl::vector<uint32_t> simpleIndices;
        eastl::vector<uint32_t> simpleFirstIndices;
        eastl::vector<uint32_t> simpleIndexCounts;
        bool captureWritten = false;
    };
}
