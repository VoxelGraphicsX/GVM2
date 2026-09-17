#pragma once

#include "MichelleGlbAsset.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one expanded billboard corner in the generated Set ABI. */
    struct alignas(16) WebgpuSkinningPointsHostVertex final
    {
        glm::vec4 cornerAndUv;
    };

    /** Mirrors the frozen camera and viewport component. */
    struct alignas(16) WebgpuSkinningPointsHostObjectData final
    {
        glm::vec4 viewProjectionColumn0;
        glm::vec4 viewProjectionColumn1;
        glm::vec4 viewProjectionColumn2;
        glm::vec4 viewProjectionColumn3;
        glm::vec4 viewport;
    };

    /** Mirrors one stable source-vertex ordinal. */
    struct alignas(16) WebgpuSkinningPointsHostInstanceData final
    {
        glm::vec4 ordinal;
    };

    /** Mirrors the slow and fast point colors. */
    struct alignas(16) WebgpuSkinningPointsHostMaterialData final
    {
        glm::vec4 slowColor;
        glm::vec4 fastColor;
    };

    static_assert(sizeof(WebgpuSkinningPointsHostVertex) == 16u);
    static_assert(sizeof(WebgpuSkinningPointsHostObjectData) == 80u);
    static_assert(sizeof(WebgpuSkinningPointsHostInstanceData) == 16u);
    static_assert(sizeof(WebgpuSkinningPointsHostMaterialData) == 32u);

    /** Connects Michelle CPU skinning to the dedicated point Compute scene. */
    class WebgpuSkinningPointsRuntimeAdapter final
    {
    public:
        /** Allocates the unique Set and initializes all three point-state buffers. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            renderer.configureOutput(options.width, options.height);
            initializeResources(renderer, inDevice, options);
            targetBuffer = renderer.getTargetBufferHandle();
            positionBuffer = renderer.getPositionBufferHandle();
            speedBuffer = renderer.getSpeedBufferHandle();
            uploadInitialPointState();
        }

        /** Uploads the current CPU-evaluated skinned targets before Compute. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes RGBA8 plus RenderSet, Compute, and loader evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU-side Michelle and staging data. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates inputs, decodes Michelle, and allocates one point entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Evaluates one fixed animation time into point target positions. */
        void evaluatePointTargets(float animationTimeSeconds);

        /** Initializes GPU position state without introducing first-frame speed. */
        void uploadInitialPointState();

        GVM::Core::DeviceProxy device;
        GVM::RHI::Buffer targetBuffer;
        GVM::RHI::Buffer positionBuffer;
        GVM::RHI::Buffer speedBuffer;
        MichelleGlbAsset asset;
        eastl::vector<glm::vec4> skinnedPositions;
        eastl::vector<glm::vec4> skinnedNormals;
        eastl::vector<glm::vec4> pointTargets;
        eastl::vector<glm::vec4> zeroSpeeds;
        eastl::vector<WebgpuSkinningPointsHostInstanceData> instances;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
