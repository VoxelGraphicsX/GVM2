#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "Fixtures/Phase1TextureCases/RgbaImageData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one OBJ-expanded vertex and normal. */
    struct alignas(16) WebglLoaderObjHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uv;
    };

    /** Mirrors one loader mesh transform and light payload. */
    struct alignas(16) WebglLoaderObjHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 lightPositionAndIntensity;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglLoaderObjHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one MTL material color. */
    struct alignas(16) WebglLoaderObjHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Mirrors loader material phase/visibility flags. */
    struct alignas(16) WebglLoaderObjHostRenderFlagsData
    {
        uint32_t phase;
        uint32_t visible;
        uint32_t reserved0;
        uint32_t reserved1;
    };

    /** Stores one expanded OBJ mesh section and stable loader identity. */
    struct WebglLoaderObjEntityData
    {
        eastl::string logicalId;
        eastl::string materialName;
        eastl::vector<WebglLoaderObjHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> textureMipOffsets;
        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
        eastl::string textureName;
        glm::mat4 model = glm::mat4(1.0f);
        WebglLoaderObjHostObjectData objectData{};
        WebglLoaderObjHostInstanceData instanceData{};
        WebglLoaderObjHostMaterialData materialData{};
        WebglLoaderObjHostRenderFlagsData renderFlags{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the 14-section OBJ/MTL loader Scene through one RenderSet. */
    class WebglLoaderObjRuntimeAdapter final
    {
    public:
        /** Parses a locked asset when present, or stages a deterministic fallback mesh set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the deterministic orbit camera and entity transforms. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads RGBA8 output and writes loader contract evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases staged OBJ mesh data. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates scenarios and allocates the complete 14-entity Scene. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Recomputes camera/object matrices from fixed orbit state. */
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

        /** Writes the one-Scene 14-entity loader snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLoaderObjEntityData> entities;
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        bool assetBacked = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
