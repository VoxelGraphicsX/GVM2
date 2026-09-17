#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one screen-expanded point or line corner in the DrawRange Set. */
    struct alignas(16) WebglBuffergeometryDrawrangeHostVertex
    {
        glm::vec4 startPosition;
        glm::vec4 endPosition;
        glm::vec4 startColor;
        glm::vec4 endColor;
        glm::vec4 corner;
        glm::vec4 lineStartClip;
        glm::vec4 lineEndClip;
        glm::vec4 directClip;
    };

    /** Mirrors the group hierarchy transform and viewport component. */
    struct alignas(16) WebglBuffergeometryDrawrangeHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::vec4 viewport;
    };

    /** Mirrors the required non-instanced RenderSet instance record. */
    struct alignas(16) WebglBuffergeometryDrawrangeHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Stores one additive material phase and its linear base color. */
    struct alignas(16) WebglBuffergeometryDrawrangeHostMaterialData
    {
        glm::vec4 colorAndKind;
    };

    /** Runs the deterministic particle integration and submits one Scene Set. */
    class WebglBuffergeometryDrawrangeRuntimeAdapter final
    {
    public:
        /** Builds the target-frame particle, line, and helper entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the precomputed target-frame scene stable during capture. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the final image and emits RenderSet and simulation evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases host-side geometry and simulation state. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked DrawRange scenario contract and allocates entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglBuffergeometryDrawrangeHostVertex> boxVertices;
        eastl::vector<uint32_t> boxIndices;
        eastl::vector<WebglBuffergeometryDrawrangeHostVertex> pointVertices;
        eastl::vector<uint32_t> pointIndices;
        eastl::vector<WebglBuffergeometryDrawrangeHostVertex> lineVertices;
        eastl::vector<uint32_t> lineIndices;
        uint32_t particleCount = 500u;
        uint32_t connectionCount = 0u;
        uint32_t finalRandomState = 0u;
        bool limitConnections = false;
        uint32_t maxConnections = 20u;
        bool captureWritten = false;
    };
}
