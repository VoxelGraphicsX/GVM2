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
    /** Mirrors one expanded indexed-line segment corner in generated DSL storage. */
    struct alignas(16) WebglBuffergeometryLinesIndexedHostVertex
    {
        glm::vec4 startPosition;
        glm::vec4 endPosition;
        glm::vec4 startColor;
        glm::vec4 endColor;
        glm::vec4 corner;
    };

    /** Mirrors the parent hierarchy transform and output viewport component. */
    struct alignas(16) WebglBuffergeometryLinesIndexedHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::vec4 viewport;
    };

    /** Mirrors the mandatory non-instanced component record. */
    struct alignas(16) WebglBuffergeometryLinesIndexedHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors the opaque vertex-color material component. */
    struct alignas(16) WebglBuffergeometryLinesIndexedHostMaterialData
    {
        glm::vec4 color;
    };

    /** Connects indexed Koch geometry and deterministic evidence to its Renderer. */
    class WebglBuffergeometryLinesIndexedRuntimeAdapter final
    {
    public:
        /** Allocates the single expanded-line entity before the first frame. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the fixed-frame parent transform immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads RGBA8 output and writes deterministic structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU staging after generated Renderer shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Builds the exact indexed Koch stream and unique Scene RenderSet entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglBuffergeometryLinesIndexedHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        uint32_t sourceVertexCount = 0u;
        uint32_t sourceSegmentCount = 0u;
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
}
