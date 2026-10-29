#pragma once

#include "ColladaAsset.hpp"
#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one robot or expanded-grid vertex in the generated Set ABI. */
    struct alignas(16) WebglLoaderColladaKinematicsHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 color;
    };

    /** Mirrors one entity transform and the shared orbit camera state. */
    struct alignas(16) WebglLoaderColladaKinematicsHostObjectData final
    {
        glm::mat4 model;
        glm::mat4 viewProjection;
        glm::mat4 normalTransform;
        glm::vec4 cameraPosition;
    };

    /** Mirrors the mandatory one-instance component payload. */
    struct alignas(16) WebglLoaderColladaKinematicsHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors one robot or grid material and its pass phase. */
    struct alignas(16) WebglLoaderColladaKinematicsHostMaterialData final
    {
        glm::vec4 baseColorAndPhase;
        glm::vec4 specularAndShininess;
    };

    /** Stores one allocated entity's mutable object component. */
    struct WebglLoaderColladaKinematicsEntityState final
    {
        GVM::Core::RenderEntityIndex entityIndex = {};
        WebglLoaderColladaKinematicsHostObjectData objectData{};
    };

    /** Connects the frozen ABB scene to its dedicated one-Set DSL renderer. */
    class WebglLoaderColladaKinematicsRuntimeAdapter final
    {
    public:
        /** Parses the DAE, generates the grid, and allocates eight Set entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Applies the frozen camera and deterministic joint pose for this frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures RGBA8 and writes the complete one-Set scene evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all sample-private CPU geometry and hierarchy state. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one Manifest scenario and creates the unique Scene Set. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Updates all eight object components from one deterministic frame. */
        void updateEntityObjects(
            GVM::Core::AbstractRendererImpl &renderer,
            uint32_t frameIndex);

        GVM::Core::DeviceProxy device;
        ColladaRobotAsset asset;
        eastl::vector<WebglLoaderColladaKinematicsEntityState> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
