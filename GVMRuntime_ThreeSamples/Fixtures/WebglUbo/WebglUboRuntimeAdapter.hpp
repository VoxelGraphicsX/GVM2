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
    struct alignas(16) WebglUboHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors one entity's model-view, normal transform, and material phase. */
    struct alignas(16) WebglUboHostObjectData final
    {
        glm::mat4 modelView;
        glm::mat4 normalTransform;
        glm::uvec4 materialPhase;
    };

    /** Mirrors the mandatory one-entry instance component. */
    struct alignas(16) WebglUboHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors the per-entity random color or white textured base. */
    struct alignas(16) WebglUboHostMaterialData final
    {
        glm::vec4 baseColor;
    };

    /** Connects the dedicated 200-entity UBO Scene to the generated renderer. */
    class WebglUboRuntimeAdapter final
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
                glm::vec4(0.0f, 0.0f, 10.0f, 64.0f),
                glm::vec4(0.201556f, 0.201556f, 0.201556f, 1.0f),
                glm::vec4(0.665387f, 0.665387f, 0.665387f, 1.0f),
                glm::vec4(0.799103f, 0.799103f, 0.799103f, 1.0f));
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
        eastl::vector<WebglUboHostVertex> tetrahedronVertices;
        eastl::vector<uint32_t> tetrahedronIndices;
        eastl::vector<WebglUboHostVertex> boxVertices;
        eastl::vector<uint32_t> boxIndices;
        eastl::vector<uint8_t> crateBytes;
        eastl::vector<uint64_t> crateMipOffsets;
        eastl::array<WebglUboHostObjectData, 200u> objects = {};
        eastl::array<WebglUboHostInstanceData, 200u> instances = {};
        eastl::array<WebglUboHostMaterialData, 200u> materials = {};
        glm::mat4 projection{1.0f};
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
