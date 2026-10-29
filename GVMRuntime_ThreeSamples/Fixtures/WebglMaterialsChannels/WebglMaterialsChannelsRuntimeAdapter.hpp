#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors the packed OBJ position, normal, and UV consumed by the DSL RenderSet. */
    struct alignas(16) WebglMaterialsChannelsHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 textureCoordinate;
    };

    /** Stores current camera/material state for the one Ninja entity. */
    struct alignas(16) WebglMaterialsChannelsHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 previousModelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalTransform;
        glm::vec4 cameraAndViewport;
        glm::uvec4 materialAndCamera;
    };

    /** Provides the required one-entry instance component. */
    struct alignas(16) WebglMaterialsChannelsHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Stores displacement scale/bias and side mode for the selected material. */
    struct alignas(16) WebglMaterialsChannelsHostMaterialData
    {
        glm::vec4 displacementAndSide;
    };

    /** Owns the decoded Ninja mesh, three explicit maps, and RenderSet handles. */
    struct WebglMaterialsChannelsEntityData
    {
        eastl::vector<WebglMaterialsChannelsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<eastl::vector<uint8_t>, 3u> textureBytes;
        eastl::array<eastl::vector<uint64_t>, 3u> textureMipOffsets;
        eastl::array<uint32_t, 3u> textureWidths = {};
        eastl::array<uint32_t, 3u> textureHeights = {};
        WebglMaterialsChannelsHostObjectData objectData{};
        WebglMaterialsChannelsHostObjectData baseObjectData{};
        WebglMaterialsChannelsHostInstanceData instanceData{};
        WebglMaterialsChannelsHostMaterialData materialData{};
        glm::mat4 previousTransform{1.0f};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the locked materials/channels example through one Scene RenderSet. */
    class WebglMaterialsChannelsRuntimeAdapter final
    {
    public:
        /** Decodes the locked Ninja assets and allocates the single Scene entity. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the current/previous transforms before each deterministic frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads back RGBA8 and writes structural and asset evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases decoded CPU-side state after capture. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the Manifest scenario and loads all locked assets. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Recomputes camera and channel state for one fixed 60 Hz frame. */
        void updateObjectData(uint32_t frameIndex, const ThreeSampleHostOptions &options);

        /** Writes one optional RGBA8 readback artifact. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes one deterministic metadata artifact for the capture. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frame,
            uint32_t width,
            uint32_t height,
            uint64_t bytes) const;

        /** Writes the one-Scene RenderSet structural snapshot. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frame) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMaterialsChannelsEntityData> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
