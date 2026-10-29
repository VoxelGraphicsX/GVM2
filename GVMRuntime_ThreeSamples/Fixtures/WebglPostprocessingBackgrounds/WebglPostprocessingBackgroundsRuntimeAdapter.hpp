#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "Fixtures/Phase1TextureCases/RgbaImageData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/array.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one SphereGeometry vertex in the generated RenderSet ABI. */
    struct alignas(16) WebglPostprocessingBackgroundsHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uv;
    };

    /** Stores camera matrices, scene transform, and enabled postprocessing passes. */
    struct alignas(16) WebglPostprocessingBackgroundsHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 model;
        glm::vec4 cameraPosition;
        glm::vec4 cameraRightAndTanHalfFov;
        glm::vec4 cameraUpAndAspect;
        glm::vec4 cameraForwardAndReserved;
        glm::vec4 viewportWidthHeight;
        glm::vec4 clearColorAndAlpha;
        glm::uvec4 passFlags;
    };

    /** Stores the one identity instance component required by RenderSet ABI. */
    struct alignas(16) WebglPostprocessingBackgroundsHostInstanceData
    {
        glm::vec4 offsetAndScale;
        glm::vec4 tint;
    };

    /** Stores the StandardMaterial base color, roughness, and metalness. */
    struct alignas(16) WebglPostprocessingBackgroundsHostMaterialData
    {
        glm::vec4 baseColor;
        glm::vec4 parameters;
    };

    /** Owns the one nested-group sphere entity and deterministic background state. */
    struct WebglPostprocessingBackgroundsEntityData
    {
        eastl::vector<WebglPostprocessingBackgroundsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<eastl::vector<uint8_t>, 7u> textureBytes;
        eastl::array<eastl::vector<uint64_t>, 7u> textureMipOffsets;
        eastl::array<uint32_t, 7u> textureWidths = {};
        eastl::array<uint32_t, 7u> textureHeights = {};
        WebglPostprocessingBackgroundsHostObjectData objectData{};
        WebglPostprocessingBackgroundsHostInstanceData instanceData{};
        WebglPostprocessingBackgroundsHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Bridges background and orbit scenarios to the dedicated Scene RenderSet. */
    class WebglPostprocessingBackgroundsRuntimeAdapter final
    {
    public:
        /** Builds the sphere and allocates the sole logical Scene entity. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Applies the locked clear/background scenario before each frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the output texture and writes capture and Scene evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU staging data after the generated renderer shuts down. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the Manifest scenario and allocates the Scene entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Updates the deterministic orbit transform and visibility state. */
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

        /** Writes the one-Scene RenderSet contract snapshot. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        WebglPostprocessingBackgroundsEntityData entity;
        eastl::string scenarioState;
        eastl::string inputReplaySha256;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
