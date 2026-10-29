#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one convex union-layout RenderSet vertex. */
    struct alignas(16) WebglGeometryConvexHostVertex
    {
        glm::vec4 position;
        glm::vec4 normalOrEnd;
        glm::vec4 color;
        glm::vec4 auxiliary;
    };

    /** Mirrors one convex entity transform and viewport component. */
    struct alignas(16) WebglGeometryConvexHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
        glm::vec4 viewport;
    };

    /** Mirrors the required non-instanced component entry. */
    struct alignas(16) WebglGeometryConvexHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one convex material phase and color component. */
    struct alignas(16) WebglGeometryConvexHostMaterialData
    {
        glm::vec4 colorAndPhase;
    };

    /** Stores one CPU convex-hull face before deterministic triangulation. */
    struct WebglGeometryConvexFace
    {
        eastl::vector<uint32_t> pointIndices;
        glm::vec4 plane;
    };

    /** Stores one complete RenderSet entity allocation payload. */
    struct WebglGeometryConvexEntityData
    {
        eastl::vector<WebglGeometryConvexHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglGeometryConvexHostObjectData objectData{};
        WebglGeometryConvexHostInstanceData instanceData{};
        WebglGeometryConvexHostMaterialData materialData{};
        const char *name = nullptr;
    };

    /** Connects exact dodecahedron hull data to the dedicated convex Renderer. */
    class WebglGeometryConvexRuntimeAdapter final
    {
    public:
        /** Builds all three entities and allocates the Scene's unique RenderSet. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Leaves the locked camera and entity payloads immutable during capture. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes the one-Set structural snapshot. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU geometry and texture staging storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and allocates all Scene entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglGeometryConvexEntityData> entities;
        eastl::vector<uint8_t> spriteBytes;
        eastl::vector<uint64_t> spriteMipOffsets;
        bool captureWritten = false;
    };
}
