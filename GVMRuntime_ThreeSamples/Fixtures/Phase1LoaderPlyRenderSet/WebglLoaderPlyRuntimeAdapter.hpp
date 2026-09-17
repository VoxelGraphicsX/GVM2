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
    struct alignas(16) LoaderPlyHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors one shader uint4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) LoaderPlyHostUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    /** Mirrors one consolidated Scene vertex containing position and generated normal. */
    struct alignas(16) LoaderPlyHostVertex
    {
        LoaderPlyHostFloat4 position;
        LoaderPlyHostFloat4 normal;
    };

    /** Mirrors one entity transform plus the main and two directional shadow cameras. */
    struct alignas(16) LoaderPlyHostObjectData
    {
        glm::mat4 model;
        glm::mat4 normalMatrix;
        glm::mat4 viewProjection;
        glm::mat4 shadowViewProjection0;
        glm::mat4 shadowViewProjection1;
        LoaderPlyHostFloat4 cameraPositionAndFogNear;
        LoaderPlyHostFloat4 cameraForwardAndFogFar;
    };

    /** Mirrors the single non-instanced component entry allocated for every Scene entity. */
    struct alignas(16) LoaderPlyHostInstanceData
    {
        LoaderPlyHostFloat4 translation;
    };

    /** Mirrors one private Phong or Standard material component entry. */
    struct alignas(16) LoaderPlyHostMaterialData
    {
        LoaderPlyHostFloat4 baseColor;
        LoaderPlyHostFloat4 specularAndSurfaceParameter;
    };

    /** Mirrors material kind and caster, receiver, and logical ordinal phase flags. */
    struct alignas(16) LoaderPlyHostRenderFlags
    {
        LoaderPlyHostUint4 values;
    };

    /** Stores one logical renderable's geometry, components, source counts, and entity identity. */
    struct LoaderPlyEntityState
    {
        eastl::string logicalId;
        eastl::string storagePrefix;
        eastl::vector<LoaderPlyHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        LoaderPlyHostObjectData objectData = {};
        LoaderPlyHostInstanceData instanceData = {};
        LoaderPlyHostMaterialData materialData = {};
        LoaderPlyHostRenderFlags renderFlags = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        uint32_t sourceVertexCount = 0u;
        uint32_t sourceFaceCount = 0u;
    };

    /** Stores deterministic camera and immutable PLY source identity for one capture scenario. */
    struct LoaderPlyScenarioState
    {
        double cameraX = 0.0;
        double cameraY = 0.15;
        double cameraZ = 0.0;
        eastl::string asciiSha256;
        eastl::string binarySha256;
    };

    /** Connects the generated PLY RenderSet renderer to exact r185 loader and camera behavior. */
    class WebglLoaderPlyRuntimeAdapter final
    {
    public:
        /** Parses both PLY assets, computes normals, and allocates exactly three Scene entities. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Updates the three object components from the deterministic r185 fixed clock. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads final RGBA8 and writes capture, structural, and loader semantic evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after generated renderer resource destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, parses assets, builds components, and creates the unique Set. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Allocates one ordinary entity in the unique Scene RenderSet. */
        GVM::Core::RenderEntityIndex allocateSceneEntity(GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder, const LoaderPlyEntityState &entity) const;

        /** Rebuilds all camera-dependent object components for one fixed clock frame. */
        void updateCameraState(uint32_t frameIndex, uint32_t width, uint32_t height);

        /** Writes exact RGBA8 bytes returned by the DSL-created output texture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes fixed capture identity and explicit null input replay metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes the one-Scene, one-RenderSet, three-entity pass reuse snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        /** Writes the canonical loader source counts, hashes, and scene semantic digest. */
        void writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::array<LoaderPlyEntityState, 3u> entities;
        LoaderPlyScenarioState scenarioState;
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
