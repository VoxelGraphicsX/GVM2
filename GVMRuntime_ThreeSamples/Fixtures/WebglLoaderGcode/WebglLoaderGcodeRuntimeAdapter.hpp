#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one shader float4 with the generated UGLC 16-byte ABI. */
    struct alignas(16) WebglLoaderGcodeHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors the endpoint-expanded line vertex consumed by the DSL pass. */
    struct alignas(16) WebglLoaderGcodeHostVertex
    {
        WebglLoaderGcodeHostFloat4 segmentStart;
        WebglLoaderGcodeHostFloat4 segmentEnd;
        WebglLoaderGcodeHostFloat4 cornerAndDistance;
    };

    /** Mirrors one loaded child transform and viewport component. */
    struct alignas(16) WebglLoaderGcodeHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        WebglLoaderGcodeHostFloat4 viewport;
    };

    /** Mirrors the mandatory per-entity instance entry. */
    struct alignas(16) WebglLoaderGcodeHostInstanceData
    {
        WebglLoaderGcodeHostFloat4 translation;
    };

    /** Mirrors one LineBasicMaterial's linear base color. */
    struct alignas(16) WebglLoaderGcodeHostMaterialData
    {
        WebglLoaderGcodeHostFloat4 baseColor;
    };

    /** Stores one expanded GCode child before RenderSet allocation. */
    struct WebglLoaderGcodeEntityState
    {
        eastl::string logicalId;
        eastl::string storagePrefix;
        eastl::vector<WebglLoaderGcodeHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglLoaderGcodeHostObjectData objectData = {};
        WebglLoaderGcodeHostInstanceData instanceData = {};
        WebglLoaderGcodeHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        uint64_t sourcePositionCount = 0u;
    };

    /** Connects the locked r185 GCodeLoader parser and one Scene RenderSet to the host. */
    class WebglLoaderGcodeRuntimeAdapter final
    {
    public:
        /** Validates the manifest scenario, parses its asset, and allocates both child entities. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            device = inDevice;
            initializeResources(renderer, options);
        }

        /** Keeps the deterministic loader camera stable before each requested frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the DSL target and writes capture, metadata, and structural evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Parses the selected asset and sends its two expanded children to the RenderSet. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 const ThreeSampleHostOptions &options);

        /** Allocates one child entity through the existing RenderSet command ABI. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const WebglLoaderGcodeEntityState &entity) const;

        /** Writes one tightly packed RGBA8 readback artifact. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes stable case, scenario, backend, and asset metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes one-Scene, one-RenderSet, two-entity structural evidence file. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        /** Writes the canonical loader sidecar for the loader-snapshot scenario. */
        void writeSemanticSnapshot(const ThreeSampleHostOptions &options) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLoaderGcodeEntityState> entities;
        eastl::string selectedAsset;
        eastl::string selectedAssetSha256;
        eastl::string canonicalSceneSha256;
        eastl::string caseId;
        glm::vec3 modelTranslation = glm::vec3(0.0f);
        glm::mat4 modelViewProjection = glm::mat4(1.0f);
        glm::mat4 modelView = glm::mat4(1.0f);
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        uint32_t totalSourcePositionCount = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
