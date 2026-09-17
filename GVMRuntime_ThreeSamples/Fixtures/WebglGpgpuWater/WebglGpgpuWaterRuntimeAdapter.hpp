#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglGpgpuWaterData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one shader float4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) GpgpuWaterHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors one shader uint4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) GpgpuWaterHostUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    /** Mirrors one consolidated Scene vertex containing position, normal, and UV. */
    struct alignas(16) GpgpuWaterHostVertex
    {
        GpgpuWaterHostFloat4 position;
        GpgpuWaterHostFloat4 normal;
        GpgpuWaterHostFloat4 uvAndReserved;
    };

    /** Mirrors one entity transform and the deterministic main camera state. */
    struct alignas(16) GpgpuWaterHostObjectData
    {
        glm::mat4 model;
        glm::mat4 normalMatrix;
        glm::mat4 viewProjection;
        GpgpuWaterHostFloat4 cameraPositionAndTime;
    };

    /** Mirrors the mandatory instance component for an ordinary non-instanced entity. */
    struct alignas(16) GpgpuWaterHostInstanceData
    {
        GpgpuWaterHostFloat4 translation;
    };

    /** Mirrors one private water, border, or duck material entry. */
    struct alignas(16) GpgpuWaterHostMaterialData
    {
        GpgpuWaterHostFloat4 baseColor;
        GpgpuWaterHostFloat4 metalnessRoughnessOpacityAndReserved;
    };

    /** Mirrors material phase, visibility, shadow, and wireframe flags. */
    struct alignas(16) GpgpuWaterHostRenderFlags
    {
        GpgpuWaterHostUint4 values;
    };

    /** Stores one logical Scene object's geometry, components, and RenderEntity identity. */
    struct GpgpuWaterEntityState
    {
        eastl::string logicalId;
        eastl::string storagePrefix;
        eastl::vector<GpgpuWaterHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        GpgpuWaterHostObjectData objectData = {};
        GpgpuWaterHostInstanceData instanceData = {};
        GpgpuWaterHostMaterialData materialData = {};
        GpgpuWaterHostRenderFlags renderFlags = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Stores the CPU-side state used by one ordinary duck dynamics update. */
    struct GpgpuWaterDuckState
    {
        glm::dvec3 position{0.0};
        glm::dvec3 velocity{0.0};
        glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
    };

    /** Stores the locked derived duck primitive, PBR inputs, and explicit sRGB mip chain. */
    struct GpgpuWaterDuckMeshAsset
    {
        eastl::vector<GpgpuWaterHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        GpgpuWaterHostMaterialData material = {};
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> textureMipOffsets;
        eastl::string meshPackSha256;
        eastl::string textureSha256;
        uint32_t textureWidth = 0u;
        uint32_t textureHeight = 0u;
    };

    /** Stores the locked Radiance environment as top-down linear RGBA32Float texels. */
    struct GpgpuWaterEnvironmentAsset
    {
        eastl::vector<float4> pixels;
        eastl::string sourceSha256;
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    /** Reconstructs r185 water initialization and feeds the single-RenderSet DSL renderer. */
    class WebglGpgpuWaterRuntimeAdapter final
    {
    public:
        /** Builds exact initial height data, allocates fourteen entities, and binds typed callbacks. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureHeight(initialHeight);
            renderer.configureDfgLut(dfgLutPackedPixels);
            renderer.configureScenePasses(shadowEnabled ? 1u : 0u);
            renderer.configureEnvironment(
                environmentAsset.pixels,
                environmentAsset.width,
                environmentAsset.height);
            heightStepper = [&renderer](WebglGpgpuWaterHeightUniforms uniforms) {
                renderer.advanceHeight(uniforms);
            };
            heightTextureProvider = [&renderer]() {
                return renderer.getCurrentHeightTextureHandle();
            };
            heightTickProvider = [&renderer]() {
                return renderer.getHeightTickCount();
            };
            allocateSceneEntities(renderer);
        }

        /** Advances the frozen callback cadence and updates vertices and duck objects after readback. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected RGBA8 target and writes structural and simulation evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases typed callbacks before generated renderer resource destruction. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the development slice and builds exact deterministic CPU inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Allocates exactly fourteen ordinary entities in the exported Scene RenderSet. */
        void allocateSceneEntities(GVM::Core::AbstractRendererImpl &renderer);

        /** Updates the water vertex component and all duck object components after one step. */
        void updateSceneFromHeight(
            GVM::Core::AbstractRendererImpl &renderer,
            const eastl::vector<float4> &heightValues);

        /** Writes the exact tightly packed RGBA8 capture. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes fixed callback, ping-pong, seed, and byte-layout metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the one-Scene, one-RenderSet, fourteen-entity pass-reuse snapshot. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        /** Writes the locked one-primitive duck loader semantic sidecar. */
        void writeLoaderSemanticSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        using HeightStepper =
            eastl::function<void(WebglGpgpuWaterHeightUniforms)>;
        using HeightTextureProvider = eastl::function<GVM::RHI::Texture()>;
        using HeightTickProvider = eastl::function<uint32_t()>;

        GVM::Core::DeviceProxy device;
        eastl::array<GpgpuWaterEntityState, 14u> entities;
        eastl::array<GpgpuWaterDuckState, 12u> ducks;
        GpgpuWaterDuckMeshAsset duckMeshAsset;
        GpgpuWaterEnvironmentAsset environmentAsset;
        eastl::vector<uint32_t> dfgLutPackedPixels;
        eastl::vector<float4> initialHeight;
        eastl::vector<float4> currentHeight;
        HeightStepper heightStepper;
        HeightTextureProvider heightTextureProvider;
        HeightTickProvider heightTickProvider;
        eastl::string initialHeightSha256;
        eastl::string currentHeightSha256;
        eastl::string currentWaterVertexSha256;
        eastl::string inputReplaySha256;
        eastl::string canonicalStateSha256;
        float viscosity = 0.93f;
        uint32_t callbackCounter = 0u;
        uint32_t simulationTickCount = 0u;
        uint32_t processedFrameCount = 0u;
        uint32_t finalRandomState = 0u;
        bool shadowEnabled = false;
        bool interactiveScenario = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
