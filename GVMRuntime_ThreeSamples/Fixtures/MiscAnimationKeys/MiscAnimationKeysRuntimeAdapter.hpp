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
    /** Mirrors one generated box or axis vertex. */
    struct alignas(16) MiscAnimationKeysHostVertex
    {
        glm::vec4 position;
        glm::vec4 lineEnd;
        glm::vec4 startColor;
        glm::vec4 endColor;
        glm::vec4 lineCorner;
    };

    /** Mirrors one generated object transform and phase component. */
    struct alignas(16) MiscAnimationKeysHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
        glm::vec4 viewport;
    };

    /** Mirrors the required non-instanced component record. */
    struct alignas(16) MiscAnimationKeysHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one generated material color and opacity component. */
    struct alignas(16) MiscAnimationKeysHostMaterialData
    {
        glm::vec4 colorAndOpacity;
    };

    /** Mirrors the opaque-helper or transparent-box phase component. */
    struct alignas(16) MiscAnimationKeysHostRenderFlagData
    {
        glm::vec4 phase;
    };

    /** Connects the deterministic keyframe scene to its dedicated Renderer. */
    class MiscAnimationKeysRuntimeAdapter final
    {
    public:
        /** Allocates both Scene entities before the first generated frame. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps target-frame keyframe data immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads final RGBA8 output and writes deterministic evidence. */
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
        /** Builds exact box, axes, keyframe, and RenderSet component data. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<MiscAnimationKeysHostVertex> boxVertices;
        eastl::vector<uint32_t> boxIndices;
        eastl::vector<MiscAnimationKeysHostVertex> axesVertices;
        eastl::vector<uint32_t> axesIndices;
        bool captureWritten = false;
    };
}
