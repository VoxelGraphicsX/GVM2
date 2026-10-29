#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one quad vertex in the glitch Scene RenderSet ABI. */
    struct alignas(16) WebglPostprocessingGlitchHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Stores the shared object transform and material index. */
    struct alignas(16) WebglPostprocessingGlitchHostObjectData
    {
        glm::vec4 offsetAndScale;
        glm::uvec4 materialAndFlags;
        glm::mat4 viewProjection;
        glm::vec4 cameraPositionAndFog;
        glm::vec4 lightDirectionAndIntensity;
        glm::vec4 fogColorNearFar;
        glm::vec4 parentRotation;
    };

    /** Stores one instanced transform and tint, matching RenderEntityInstanceID. */
    struct alignas(16) WebglPostprocessingGlitchHostInstanceData
    {
        glm::vec4 positionAndScale;
        glm::vec4 rotation;
        glm::vec4 tint;
    };

    /** Stores the base material tint selected by the object component. */
    struct alignas(16) WebglPostprocessingGlitchHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Owns one 100-instance glitch mesh and its deterministic update payload. */
    struct WebglPostprocessingGlitchEntityData
    {
        eastl::vector<WebglPostprocessingGlitchHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebglPostprocessingGlitchHostInstanceData> instances;
        WebglPostprocessingGlitchHostObjectData objectData{};
        WebglPostprocessingGlitchHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Stores the deterministic GlitchPass uniforms for one captured frame. */
    struct WebglPostprocessingGlitchEffectState
    {
        float time = 0.0f;
        float amount = 0.0f;
        float angle = 0.0f;
        float seed = 0.0f;
        float seedX = 0.0f;
        float seedY = 0.0f;
        float distortionX = 0.0f;
        float distortionY = 0.0f;
        float columnWidth = 0.05f;
        float bypass = 1.0f;
    };

    /** Bridges the pinned r185 glitch scene to one dedicated RenderSet adapter. */
    class WebglPostprocessingGlitchRuntimeAdapter final
    {
    public:
        /** Builds the 100-instance scene and allocates its single RenderSet entity. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
            const WebglPostprocessingGlitchEffectState effectState =
                makeEffectState(options.targetFrame, wildMode, options.randomSeed);
            renderer.configureEffect(effectState.time, effectState.amount,
                                     effectState.angle, effectState.seed,
                                     effectState.seedX, effectState.seedY,
                                     effectState.distortionX, effectState.distortionY,
                                     effectState.columnWidth, effectState.bypass,
                                     wildMode);
        }

        /** Updates deterministic instance motion and the wild trigger state. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
        uint32_t frameIndex);

        /** Reads the final target and writes standard capture evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU staging data after the generated Renderer shuts down. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked Manifest scenario and allocates its RenderSet entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Updates instance transforms and colors at one fixed simulation frame. */
        void updateInstances(uint32_t frameIndex, bool wildMode);

        /** Replays the r185 GlitchPass random stream through one target frame. */
        WebglPostprocessingGlitchEffectState makeEffectState(
            uint32_t targetFrame,
            bool wildMode,
            uint32_t randomSeed) const;

        /** Writes one optional RGBA8 capture to disk. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes the normalized capture metadata record. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the RenderSet and instance contract snapshot. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            bool wildMode) const;

        GVM::Core::DeviceProxy device;
        WebglPostprocessingGlitchEntityData entity;
        bool wildMode = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
