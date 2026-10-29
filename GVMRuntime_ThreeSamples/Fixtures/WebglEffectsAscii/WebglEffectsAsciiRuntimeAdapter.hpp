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
    /** Mirrors one ASCII Scene vertex in the generated RenderSet ABI. */
    struct alignas(16) WebglEffectsAsciiHostVertex
    {
        glm::vec4 position;
        glm::vec4 color;
        glm::vec4 normal;
    };

    /** Stores one entity transform and material index. */
    struct alignas(16) WebglEffectsAsciiHostObjectData
    {
        glm::vec4 offsetAndScale;
        glm::uvec4 materialAndFlags;
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 light0PositionIntensity;
        glm::vec4 light1PositionIntensity;
    };

    /** Stores one identity instance component. */
    struct alignas(16) WebglEffectsAsciiHostInstanceData
    {
        glm::vec4 offsetAndScale;
        glm::vec4 tint;
    };

    /** Stores one ASCII source material tint. */
    struct alignas(16) WebglEffectsAsciiHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Stores the private material/render phase flags declared by the DSL. */
    struct alignas(16) WebglEffectsAsciiHostRenderFlagsData
    {
        glm::uvec4 flags;
    };

    /** Owns one packed sphere or plane entity and all required components. */
    struct WebglEffectsAsciiEntityData
    {
        eastl::vector<WebglEffectsAsciiHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglEffectsAsciiHostObjectData objectData{};
        WebglEffectsAsciiHostInstanceData instanceData{};
        WebglEffectsAsciiHostMaterialData materialData{};
        WebglEffectsAsciiHostRenderFlagsData renderFlags{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Bridges the two-entity ASCII Scene to its independent RenderSet adapter. */
    class WebglEffectsAsciiRuntimeAdapter final
    {
    public:
        /** Builds sphere/plane data and allocates both entities in one Scene Set. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the fixed-step sphere transform before capture. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads RGBA8 and writes the Scene contract evidence. */
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
        /** Validates the locked ASCII scenarios and allocates both entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Updates the sphere bounce and plane stability at one fixed frame. */
        void updateObjectData(uint32_t frameIndex);

        /** Writes an optional RGBA8 capture. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes normalized capture metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the two-entity RenderSet snapshot. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglEffectsAsciiEntityData> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
