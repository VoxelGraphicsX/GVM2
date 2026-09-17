#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglGpgpuBirdsGltfData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one shader float4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) GpgpuBirdsGltfHostFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    /** Mirrors one shader uint4 with an explicit cross-pipeline 16-byte ABI. */
    struct alignas(16) GpgpuBirdsGltfHostUint4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    /** Mirrors the Scene object component containing camera, time, size, and fog. */
    struct alignas(16) GpgpuBirdsGltfHostObjectData
    {
        glm::mat4 projectionMatrix;
        glm::mat4 viewMatrix;
        GpgpuBirdsGltfHostFloat4 cameraPositionAndTime;
        GpgpuBirdsGltfHostFloat4 sizeAndFogRange;
    };

    /** Mirrors the mandatory identity instance component for the ordinary entity. */
    struct alignas(16) GpgpuBirdsGltfHostInstanceData
    {
        GpgpuBirdsGltfHostFloat4 translationAndScale;
    };

    /** Mirrors the sole Standard material component and source-model selector. */
    struct alignas(16) GpgpuBirdsGltfHostMaterialData
    {
        GpgpuBirdsGltfHostFloat4 baseColor;
        GpgpuBirdsGltfHostUint4 modelAndReserved;
    };

    /** Stores one decoded self-contained bird GLB and its baked morph texture. */
    struct GpgpuBirdsGltfAsset
    {
        eastl::string name;
        eastl::string sourceSha256;
        eastl::string morphTextureSha256;
        eastl::vector<float> positions;
        eastl::vector<float> colors;
        eastl::vector<uint32_t> indices;
        eastl::vector<eastl::vector<float>> morphTargets;
        eastl::vector<float4> animationTexture;
        uint32_t vertexCount = 0u;
        uint32_t indexCount = 0u;
        uint32_t morphTargetCount = 0u;
        uint32_t animationDurationFrames = 0u;
        uint32_t animationTextureWidth = 0u;
        uint32_t animationTextureHeight = 0u;
    };

    /** Stores the unique entity identity and its current same-Set allocation state. */
    struct GpgpuBirdsGltfEntityState
    {
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        GVM::Core::RenderEntityIndex removedEntityIndex = UINT32_MAX;
        uint32_t birdCount = 0u;
        float size = 0.0f;
        bool reallocated = false;
    };

    /** Bridges private GPU flocking readback into one mandatory Scene RenderSet. */
    class WebglGpgpuBirdsGltfRuntimeAdapter final
    {
    public:
        /** Loads locked GLBs, initializes simulation, and allocates the sole Scene entity. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureSimulation(initialPosition, initialVelocity);
            simulationStepper = [&renderer](
                                    WebglGpgpuBirdsGltfSimulationUniforms uniforms) {
                renderer.advanceSimulation(uniforms);
            };
            positionTextureProvider = [&renderer]() {
                return renderer.getCurrentPositionTextureHandle();
            };
            velocityTextureProvider = [&renderer]() {
                return renderer.getCurrentVelocityTextureHandle();
            };
            simulationStepProvider = [&renderer]() {
                return renderer.getSimulationStepCount();
            };
            allocateEntity(renderer, 1024u, 0.2f, false);
        }

        /** Advances GPU simulation, transfers 4,096 states, and updates the sole entity. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures the selected RGBA8 output and writes structural bridge evidence. */
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
        /** Validates the scenario and builds locked GLB, RNG, geometry, and simulation inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Allocates or reallocates the one entity inside the existing Scene RenderSet. */
        void allocateEntity(
            GVM::Core::AbstractRendererImpl &renderer,
            uint32_t birdCount,
            float size,
            bool replaceExisting);

        /** Writes current simulation and camera data through existing BufferComponent commands. */
        void updateEntityComponents(
            GVM::Core::AbstractRendererImpl &renderer,
            uint32_t frameIndex);

        /** Writes the exact tightly packed final RGBA8 capture. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic timing, replay, simulation, and bridge metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the single-Set entity, component, asset, and pass snapshot. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        using SimulationStepper = eastl::function<void(
            WebglGpgpuBirdsGltfSimulationUniforms)>;
        using TextureProvider = eastl::function<GVM::RHI::Texture()>;
        using SimulationStepProvider = eastl::function<uint32_t()>;

        GVM::Core::DeviceProxy device;
        GpgpuBirdsGltfAsset parrotAsset;
        GpgpuBirdsGltfAsset flamingoAsset;
        GpgpuBirdsGltfEntityState entityState;
        eastl::vector<WebglGpgpuBirdsGltfVertex> maximumVertices;
        eastl::vector<uint32_t> maximumIndices;
        eastl::vector<float4> initialPosition;
        eastl::vector<float4> initialVelocity;
        eastl::vector<WebglGpgpuBirdsGltfSimulationState> simulationStates;
        SimulationStepper simulationStepper;
        TextureProvider positionTextureProvider;
        TextureProvider velocityTextureProvider;
        SimulationStepProvider simulationStepProvider;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        eastl::string initialPositionSha256;
        eastl::string initialVelocitySha256;
        eastl::string currentPositionSha256;
        eastl::string currentVelocitySha256;
        eastl::string geometrySha256;
        eastl::string inputReplaySha256;
        eastl::string canonicalStateSha256;
        uint32_t processedFrameCount = 0u;
        bool interactiveScenario = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
