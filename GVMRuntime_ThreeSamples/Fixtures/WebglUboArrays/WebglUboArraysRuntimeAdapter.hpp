#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one unified UBO example vertex across both shader pipelines. */
    struct alignas(16) WebglUboArraysHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors one entity's model-view, normal transform, and material phase. */
    struct alignas(16) WebglUboArraysHostObjectData final
    {
        glm::mat4 model;
        glm::mat4 modelView;
        glm::mat4 normalTransform;
        glm::uvec4 materialPhase;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglUboArraysHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors the per-entity random color or white textured base. */
    struct alignas(16) WebglUboArraysHostMaterialData final
    {
        glm::vec4 baseColor;
    };

    /** Connects the dedicated 101-entity UBO-array Scene to the generated renderer. */
    class WebglUboArraysRuntimeAdapter final
    {
    public:
        /** Builds deterministic geometry/entities and uploads the shared UBO. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureShared(
                projection,
                lightPositions.data(), lightColors.data(),
                activeLightCount);
        }

        /** Keeps the fixed target-frame entity state unchanged during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes deterministic image and one-Set structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases sample-private CPU data after generated renderer teardown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and allocates all entities in the unique Set. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglUboArraysHostVertex> planeVertices;
        eastl::vector<uint32_t> planeIndices;
        eastl::vector<WebglUboArraysHostVertex> boxVertices;
        eastl::vector<uint32_t> boxIndices;
        eastl::array<WebglUboArraysHostObjectData, 101u> objects = {};
        eastl::array<WebglUboArraysHostInstanceData, 101u> instances = {};
        eastl::array<WebglUboArraysHostMaterialData, 101u> materials = {};
        eastl::array<glm::vec4, 300u> lightPositions = {};
        eastl::array<glm::vec4, 300u> lightColors = {};
        glm::vec4 activeLightCount{300.0f, 0.0f, 0.0f, 0.0f};
        glm::mat4 projection{1.0f};
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
