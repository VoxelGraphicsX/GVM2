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
    /** Mirrors one generated position-only BoxGeometry vertex. */
    struct alignas(16) MiscAnimationGroupsHostVertex
    {
        glm::vec4 position;
    };

    /** Mirrors one generated model-view and projection component record. */
    struct alignas(16) MiscAnimationGroupsHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
    };

    /** Mirrors the mandatory non-instanced component record. */
    struct alignas(16) MiscAnimationGroupsHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one generated material color and opacity record. */
    struct alignas(16) MiscAnimationGroupsHostMaterialData
    {
        glm::vec4 colorAndOpacity;
    };

    /** Mirrors the generated transparent material phase record. */
    struct alignas(16) MiscAnimationGroupsHostRenderFlagData
    {
        glm::vec4 phase;
    };

    /** Connects the deterministic 25-box animation group to its Renderer. */
    class MiscAnimationGroupsRuntimeAdapter final
    {
    public:
        /** Allocates all ordinary Scene entities before the first frame. */
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
        /** Builds exact geometry, keyframe state, and RenderSet entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<MiscAnimationGroupsHostVertex> boxVertices;
        eastl::vector<uint32_t> boxIndices;
        bool captureWritten = false;
    };
}
