#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one projected array-camera vertex in the dedicated Scene RenderSet. */
    struct alignas(16) CameraArrayHostVertex
    {
        glm::vec4 clipPosition;
        glm::vec4 worldPosition;
        glm::vec4 worldNormalAndTile;
        glm::vec4 lightUvDepthAndReserved;
    };

    /** Mirrors the required per-object camera-array component. */
    struct alignas(16) CameraArrayHostObjectData
    {
        glm::vec4 receiveShadowAndReserved;
    };

    /** Mirrors the required identity per-instance camera-array component. */
    struct alignas(16) CameraArrayHostInstanceData
    {
        glm::vec4 identity;
    };

    /** Mirrors one linear material color and entity phase. */
    struct alignas(16) CameraArrayHostMaterialData
    {
        glm::vec4 baseColorAndPhase;
    };

    /** Stores one of the two logical Scene entities before RenderSet allocation. */
    struct CameraArrayEntity
    {
        eastl::string logicalId;
        eastl::vector<CameraArrayHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        CameraArrayHostObjectData objectData = {};
        CameraArrayHostInstanceData instanceData = {};
        CameraArrayHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Connects either r185 array-camera example to its dedicated one-Set DSL Renderer. */
    class CameraArrayRuntimeAdapter final
    {
    public:
        /** Builds both entities for all thirty-six cameras and allocates the unique Scene Set. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Leaves the target-frame deterministic transforms immutable during warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected target and writes formal gate evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Clears CPU entity storage after generated renderer teardown begins. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked case contract and allocates exactly two RenderSet entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Allocates one entity using only generated RenderSet component handles. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const CameraArrayEntity &entity) const;

        /** Writes RGBA8, metadata, and the exact one-Set structural snapshot. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<CameraArrayEntity> entities;
        eastl::string caseId;
        bool webgpuVariant = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
