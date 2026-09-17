#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one CPU-expanded line vertex. */
    struct alignas(16) WebglInteractiveLinesHostVertex
    {
        glm::vec4 segmentStart;
        glm::vec4 segmentEnd;
        glm::vec4 endpointSide;
    };

    /** Mirrors one line entity's view and projection matrices. */
    struct alignas(16) WebglInteractiveLinesHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 viewportAndReserved;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglInteractiveLinesHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one line color and hit marker flag. */
    struct alignas(16) WebglInteractiveLinesHostMaterialData
    {
        glm::vec4 colorAndFlags;
    };

    /** Stores one expanded line entity and its parent/object transform. */
    struct WebglInteractiveLinesEntityData
    {
        eastl::vector<WebglInteractiveLinesHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        glm::mat4 model = glm::mat4(1.0f);
        WebglInteractiveLinesHostObjectData objectData{};
        WebglInteractiveLinesHostInstanceData instanceData{};
        WebglInteractiveLinesHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the 50-line parent hierarchy through one Scene RenderSet. */
    class WebglInteractiveLinesRuntimeAdapter final
    {
    public:
        /** Builds the shared path, expands every line, and allocates one Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates parent/object orbit and optional intersection marker state. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads RGBA8 output and writes line Scene evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases generated line geometry. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates replay scenarios and allocates all 50 line entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Recomputes matrices from the fixed camera and parent hierarchy. */
        void updateObjectData(uint32_t width, uint32_t height, uint32_t frameIndex);

        /** Writes RGBA8 capture bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes one-Scene 50-entity structural snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglInteractiveLinesEntityData> entities;
        glm::mat4 parentTransform = glm::mat4(1.0f);
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        double pointerX = 0.0;
        double pointerY = 0.0;
        bool sphereVisible = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
