#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "Fixtures/Phase1TextureCases/RgbaImageData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors the color-normalized vertex layout declared by the 3DS DSL shard. */
    struct alignas(16) WebglLoader3dsHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uv;
    };

    /** Mirrors one loader section's normalized clip transform and material index. */
    struct alignas(16) WebglLoader3dsHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 lightDirectionAndIntensity;
        glm::vec4 directionalLightColorAndIntensity;
    };

    /** Mirrors the one-entry instance component used by every loader section. */
    struct alignas(16) WebglLoader3dsHostInstanceData
    {
        glm::vec4 offsetAndScale;
        glm::vec4 tint;
    };

    /** Mirrors one deterministic material recovered from a 3DS section. */
    struct alignas(16) WebglLoader3dsHostMaterialData
    {
        glm::vec4 baseColor;
        glm::vec4 specularColorAndShininess;
    };

    /** Stores one expanded 3DS section and its RenderSet entity identity. */
    struct WebglLoader3dsEntityData
    {
        eastl::vector<WebglLoader3dsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> textureMipOffsets;
        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
        eastl::vector<uint8_t> normalTextureBytes;
        eastl::vector<uint64_t> normalTextureMipOffsets;
        uint32_t normalTextureWidth = 0u;
        uint32_t normalTextureHeight = 0u;
        WebglLoader3dsHostObjectData objectData{};
        WebglLoader3dsHostInstanceData instanceData{};
        WebglLoader3dsHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the loader snapshot and camera replay through one Scene RenderSet. */
    class WebglLoader3dsRuntimeAdapter final
    {
    public:
        /** Builds deterministic section geometry and allocates the Scene Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Applies the fixed camera input replay to all loader sections. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the DSL target and writes loader contract evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases section geometry after the capture completes. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the frozen scenarios and allocates all section entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Updates deterministic section positions for a replay frame. */
        void updateObjectData(uint32_t frameIndex);

        /** Writes an optional RGBA8 capture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes the standard capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the one-Scene loader structural snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        /** Writes the locked semantic result for the canonical 3DS loader state. */
        void writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options,
                                         uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLoader3dsEntityData> entities;
        glm::vec3 cameraPosition = glm::vec3(0.0f, 0.0f, 2.0f);
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
