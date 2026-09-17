#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one corner of a triangle-expanded LOD wire segment. */
    struct alignas(16) WebglLodHostVertex
    {
        glm::vec4 segmentStart;
        glm::vec4 segmentEnd;
        glm::vec4 startNormal;
        glm::vec4 endNormal;
        glm::vec4 endpointAndSide;
    };

    /** Mirrors one LOD entity transform and lighting state. */
    struct alignas(16) WebglLodHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 viewport;
        glm::vec4 directionalLightDirectionAndIntensity;
        glm::vec4 pointLightViewPositionAndIntensity;
        glm::vec4 fogNearFar;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglLodHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one LOD material color. */
    struct alignas(16) WebglLodHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Stores one CPU-selected LOD geometry allocation. */
    struct WebglLodEntityData
    {
        eastl::vector<WebglLodHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        glm::mat4 model = glm::mat4(1.0f);
        uint32_t rootIndex = 0u;
        uint32_t levelIndex = 0u;
        uint32_t lodLevel = 0u;
        uint32_t requestedLevelIndex = 0u;
        bool visible = false;
        bool hasGeometry = false;
        WebglLodHostObjectData objectData{};
        WebglLodHostInstanceData instanceData{};
        WebglLodHostMaterialData materialData{};
        glm::uvec4 lodState{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the 1,000-object r185 LOD Scene through one RenderSet. */
    class WebglLodRuntimeAdapter final
    {
    public:
        /** Generates deterministic LOD sphere geometry and allocates one Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the fixed FlyControls-compatible camera replay. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads RGBA8 output and writes LOD Scene evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases generated LOD geometry. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates scenarios and allocates the five Mesh entities for every LOD root. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Recomputes matrices from the fixed camera orbit. */
        void updateObjectData(uint32_t width, uint32_t height, uint32_t frameIndex);

        /** Writes RGBA8 capture bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes standard capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes one-Scene 5,000-entity LOD snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLodEntityData> entities;
        eastl::vector<eastl::vector<WebglLodHostVertex>> levelVertices;
        eastl::vector<eastl::vector<uint32_t>> levelIndices;
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        glm::vec3 cameraPosition = glm::vec3(0.0f, 0.0f, 1000.0f);
        glm::quat cameraOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        bool flyControlsReplay = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
