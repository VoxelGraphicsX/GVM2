#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors the dedicated sky RenderSet clip-space vertex ABI. */
    struct alignas(16) Phase1SkyHostVertex
    {
        glm::vec4 position;
    };

    /** Mirrors one dedicated sky RenderSet object component. */
    struct alignas(16) Phase1SkyHostObjectData
    {
        glm::vec4 phase;
    };

    /** Mirrors one mandatory sky RenderSet instance component. */
    struct alignas(16) Phase1SkyHostInstanceData
    {
        glm::vec4 value;
    };

    /** Mirrors one mandatory sky RenderSet material component. */
    struct alignas(16) Phase1SkyHostMaterialData
    {
        glm::vec4 value;
    };

    /** Stores one independently allocated sky or expanded-grid entity. */
    struct Phase1SkyHostEntity
    {
        eastl::string logicalId;
        eastl::vector<Phase1SkyHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        Phase1SkyHostObjectData objectData = {};
        Phase1SkyHostInstanceData instanceData = {};
        Phase1SkyHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Stores one canonical r185 sky atmosphere, camera, and UI configuration. */
    struct Phase1SkyHostFrame
    {
        glm::vec4 atmosphere;
        glm::vec4 clouds;
        glm::vec4 sunAndDisc;
        glm::vec4 cameraRight;
        glm::vec4 cameraUp;
        glm::vec4 cameraForward;
        glm::vec4 viewportAndTime;
    };

    /** Connects one dedicated sky renderer to its unique Scene RenderSet. */
    class Phase1SkyRuntimeAdapter final
    {
    public:
        /** Allocates the exact Scene entities and uploads canonical sky controls. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureSky(
                frame.atmosphere,
                frame.clouds,
                frame.sunAndDisc,
                frame.cameraRight,
                frame.cameraUp,
                frame.cameraForward,
                frame.viewportAndTime);
        }

        /** Keeps fixed-clock sky state immutable during deterministic warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the DSL output and records strict Scene/RenderSet evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction begins. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Builds canonical atmosphere/camera state and allocates every Scene entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Allocates one entity through current RenderSet component semantics. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const Phase1SkyHostEntity &entity) const;

        /** Writes RGBA8, capture metadata, and one-Set structural evidence. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<Phase1SkyHostEntity> entities;
        Phase1SkyHostFrame frame = {};
        eastl::string caseId;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
