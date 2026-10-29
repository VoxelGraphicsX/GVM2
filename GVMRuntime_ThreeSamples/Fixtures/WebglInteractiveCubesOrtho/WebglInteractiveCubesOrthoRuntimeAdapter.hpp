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
    /** Mirrors one orthographic BoxGeometry vertex and face normal. */
    struct alignas(16) WebglInteractiveCubesOrthoHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors one orthographic entity's transform and directional light. */
    struct alignas(16) WebglInteractiveCubesOrthoHostObjectData
    {
    glm::mat4 modelViewProjection;
    glm::mat4 modelView;
    glm::mat4 normalMatrix;
    glm::vec4 lightDirectionAndIntensity;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglInteractiveCubesOrthoHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one Lambert material and pointer-hit emissive state. */
    struct alignas(16) WebglInteractiveCubesOrthoHostMaterialData
    {
        glm::vec4 baseColor;
        glm::vec4 emissive;
    };

    /** Stores one deterministic orthographic cube allocation. */
    struct WebglInteractiveCubesOrthoEntityData
    {
        eastl::vector<WebglInteractiveCubesOrthoHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        glm::mat4 model = glm::mat4(1.0f);
        WebglInteractiveCubesOrthoHostObjectData objectData{};
        WebglInteractiveCubesOrthoHostInstanceData instanceData{};
        WebglInteractiveCubesOrthoHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the 2,000-cube orthographic r185 example with one Scene RenderSet. */
    class WebglInteractiveCubesOrthoRuntimeAdapter final
    {
    public:
        /** Builds deterministic BoxGeometry entities and allocates one RenderSet. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the orthographic orbit and optional pointer-hit material. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads RGBA8 output and writes structural Scene evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases generated CPU cube payloads after renderer teardown. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates scenarios, builds cubes, and allocates every entity. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Recomputes orthographic camera matrices from the target frame. */
        void updateObjectData(uint32_t width, uint32_t height, uint32_t frameIndex);

        /** Writes the final RGBA8 bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes standard capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the one-Scene 2,000-entity snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglInteractiveCubesOrthoEntityData> entities;
        glm::mat4 currentViewMatrix = glm::mat4(1.0f);
        glm::mat4 currentProjectionMatrix = glm::mat4(1.0f);
        uint32_t selectedCubeIndex = UINT32_MAX;
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
