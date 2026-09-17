#pragma once

#include "MichelleGlbAsset.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one ground or Michelle vertex in the generated RenderSet ABI. */
    struct alignas(16) WebgpuSkinningInstancingHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::uvec4 joints;
        glm::vec4 weights;
    };

    /** Mirrors one entity's model, camera, and deterministic time data. */
    struct alignas(16) WebgpuSkinningInstancingHostObjectData final
    {
        glm::mat4 model;
        glm::mat4 viewProjection;
        glm::vec4 cameraPositionAndTime;
    };

    /** Mirrors one ground or Michelle instance transform. */
    struct alignas(16) WebgpuSkinningInstancingHostInstanceData final
    {
        glm::mat4 transform;
        glm::vec4 randomColorAndMetalness;
    };

    /** Mirrors one material phase and the two upstream point lights. */
    struct alignas(16) WebgpuSkinningInstancingHostMaterialData final
    {
        glm::vec4 baseColorAndRoughness;
        glm::vec4 pointColorAndPower;
        glm::vec4 cameraLightColorAndPower;
        glm::uvec4 phaseAndReserved;
    };

    /** Mirrors one matrix in the entity-local skin palette component. */
    struct alignas(16) WebgpuSkinningInstancingHostSkinMatrix final
    {
        glm::mat4 value;
    };

    static_assert(sizeof(WebgpuSkinningInstancingHostVertex) == 64u);
    static_assert(sizeof(WebgpuSkinningInstancingHostObjectData) == 144u);
    static_assert(sizeof(WebgpuSkinningInstancingHostInstanceData) == 80u);
    static_assert(sizeof(WebgpuSkinningInstancingHostMaterialData) == 64u);
    static_assert(sizeof(WebgpuSkinningInstancingHostSkinMatrix) == 64u);

    /** Connects the dedicated instanced-skinning renderer to Michelle.glb. */
    class WebgpuSkinningInstancingRuntimeAdapter final
    {
    public:
        /** Configures output and allocates both Scene entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            renderer.configureOutput(options.width, options.height);
            initializeResources(renderer, inDevice, options);
        }

        /** Uploads the fixed-step evaluated pose, palette evidence, and time. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes RGBA8 plus atomic-batch scene and semantic evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all sample-private decoded and animation state. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenarios and allocates one unique Scene Set. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        MichelleGlbAsset asset;
        eastl::vector<WebgpuSkinningInstancingHostVertex> michelleVertices;
        eastl::vector<uint32_t> planeIndices;
        eastl::vector<WebgpuSkinningInstancingHostVertex> planeVertices;
        eastl::vector<WebgpuSkinningInstancingHostInstanceData> michelleInstances;
        eastl::vector<WebgpuSkinningInstancingHostSkinMatrix> skinPalette;
        eastl::array<WebgpuSkinningInstancingHostObjectData, 2u> objects = {};
        eastl::array<WebgpuSkinningInstancingHostMaterialData, 2u> materials = {};
        eastl::array<glm::uvec4, 2u> renderFlags = {};
        uint32_t groundEntity = 0u;
        uint32_t michelleEntity = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
