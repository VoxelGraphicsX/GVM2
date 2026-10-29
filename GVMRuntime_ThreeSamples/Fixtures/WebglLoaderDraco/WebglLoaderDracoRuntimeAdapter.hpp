#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one shader float4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) LoaderDracoHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors one shader uint4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) LoaderDracoHostUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    /** Mirrors one consolidated Scene vertex containing position and generated normal. */
    struct alignas(16) LoaderDracoHostVertex
    {
        LoaderDracoHostFloat4 position;
        LoaderDracoHostFloat4 normal;
    };

    /** Mirrors one entity transform and the current main and spotlight shadow cameras. */
    struct alignas(16) LoaderDracoHostObjectData
    {
        glm::mat4 model;
        glm::mat4 normalMatrix;
        glm::mat4 viewProjection;
        glm::mat4 shadowViewProjection;
        LoaderDracoHostFloat4 cameraPositionAndFogNear;
        LoaderDracoHostFloat4 cameraForwardAndFogFar;
    };

    /** Mirrors the one non-instanced component entry allocated for each Scene entity. */
    struct alignas(16) LoaderDracoHostInstanceData
    {
        LoaderDracoHostFloat4 translation;
    };

    /** Mirrors one linear material color and material/shadow phase flags. */
    struct alignas(16) LoaderDracoHostMaterialData
    {
        LoaderDracoHostFloat4 baseColor;
        LoaderDracoHostUint4 flags;
    };

    /** Stores one logical renderable's geometry, components, counts, and RenderSet identity. */
    struct LoaderDracoEntityState
    {
        eastl::string logicalId;
        eastl::string storagePrefix;
        eastl::vector<LoaderDracoHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        LoaderDracoHostObjectData objectData = {};
        LoaderDracoHostInstanceData instanceData = {};
        LoaderDracoHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        uint32_t sourceVertexCount = 0u;
        uint32_t sourceFaceCount = 0u;
    };

    /** Stores deterministic camera state and immutable Draco source identity. */
    struct LoaderDracoScenarioState
    {
        double cameraX = 0.0;
        double cameraY = 0.25;
        double cameraZ = 0.5;
        eastl::string assetSha256;
    };

    /** Connects the generated Draco RenderSet renderer to exact r185 loader and camera behavior. */
    class WebglLoaderDracoRuntimeAdapter final
    {
    public:
        /** Decodes the bunny and allocates exactly two entities in the unique Scene Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Updates camera-dependent components from the deterministic fixed clock. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads final RGBA8 and writes capture, structural, and loader semantic evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after generated resource destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates options, builds exact geometry and components, and creates the unique Set. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Allocates one ordinary entity in the unique Scene RenderSet. */
        GVM::Core::RenderEntityIndex allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, const LoaderDracoEntityState &entity) const;

        /** Rebuilds both entity camera components for one fixed-clock frame. */
        void updateCameraState(uint32_t frameIndex, uint32_t width, uint32_t height);

        /** Writes exact RGBA8 bytes returned by the DSL-created final texture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes fixed capture identity and explicit single-sample metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes the one-Scene, one-RenderSet, two-entity pass-reuse snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        /** Writes canonical Draco source counts, hash, and generated-normal evidence. */
        void writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::array<LoaderDracoEntityState, 2u> entities;
        LoaderDracoScenarioState scenarioState;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
