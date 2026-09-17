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
    struct alignas(16) LoaderStlHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors one shader uint4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) LoaderStlHostUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    /** Mirrors one consolidated STL Scene vertex containing position, facet normal, and optional linear color. */
    struct alignas(16) LoaderStlHostVertex
    {
        LoaderStlHostFloat4 position;
        LoaderStlHostFloat4 normal;
        LoaderStlHostFloat4 color;
    };

    /** Mirrors one entity transform plus the main and two directional shadow cameras. */
    struct alignas(16) LoaderStlHostObjectData
    {
        glm::mat4 model;
        glm::mat4 normalMatrix;
        glm::mat4 viewProjection;
        glm::mat4 shadowViewProjection0;
        glm::mat4 shadowViewProjection1;
        LoaderStlHostFloat4 cameraPositionAndFogNear;
        LoaderStlHostFloat4 cameraForwardAndFogFar;
    };

    /** Mirrors the single non-instanced component entry allocated for every Scene entity. */
    struct alignas(16) LoaderStlHostInstanceData
    {
        LoaderStlHostFloat4 translation;
    };

    /** Mirrors one private Phong material component entry. */
    struct alignas(16) LoaderStlHostMaterialData
    {
        LoaderStlHostFloat4 baseColor;
        LoaderStlHostFloat4 specularAndSurfaceParameter;
    };

    /** Mirrors material kind and caster, receiver, and logical ordinal phase flags. */
    struct alignas(16) LoaderStlHostRenderFlags
    {
        LoaderStlHostUint4 values;
    };

    /** Stores one logical renderable's geometry, components, source counts, and entity identity. */
    struct LoaderStlEntityState
    {
        eastl::string logicalId;
        eastl::string storagePrefix;
        eastl::vector<LoaderStlHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        LoaderStlHostObjectData objectData = {};
        LoaderStlHostInstanceData instanceData = {};
        LoaderStlHostMaterialData materialData = {};
        LoaderStlHostRenderFlags renderFlags = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        uint32_t sourceVertexCount = 0u;
        uint32_t sourceFaceCount = 0u;
    };

    /** Stores deterministic camera and all four immutable STL source identities for one capture scenario. */
    struct LoaderStlScenarioState
    {
        double cameraX = 0.0;
        double cameraY = 0.15;
        double cameraZ = 0.0;
        eastl::array<eastl::string, 4u> assetSha256;
    };

    /** Connects the generated STL RenderSet renderer to exact r185 loader and camera behavior. */
    class WebglLoaderStlRuntimeAdapter final
    {
    public:
        /** Parses all four STL assets and allocates exactly five Scene entities. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Updates the five object components from the deterministic r185 fixed clock. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads final RGBA8 and writes capture, structural, and loader semantic evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after generated renderer resource destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, parses assets, builds components, and creates the unique Set. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Allocates one ordinary entity in the unique Scene RenderSet. */
        GVM::Core::RenderEntityIndex allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, const LoaderStlEntityState &entity) const;

        /** Rebuilds all camera-dependent object components for one fixed clock frame. */
        void updateCameraState(uint32_t frameIndex, uint32_t width, uint32_t height);

        /** Writes exact RGBA8 bytes returned by the DSL-created output texture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes fixed capture identity and explicit null input replay metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes the one-Scene, one-RenderSet, five-entity pass reuse snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        /** Writes the canonical loader source counts, hashes, and scene semantic digest. */
        void writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::array<LoaderStlEntityState, 5u> entities;
        LoaderStlScenarioState scenarioState;
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
