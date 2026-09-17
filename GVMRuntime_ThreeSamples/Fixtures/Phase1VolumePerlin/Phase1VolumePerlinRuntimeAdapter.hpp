#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors the dedicated volume RenderSet position-only vertex ABI. */
    struct alignas(16) Phase1VolumePerlinHostVertex
    {
        glm::vec4 position;
    };

    /** Mirrors one mandatory volume object component record. */
    struct alignas(16) Phase1VolumePerlinHostObjectData
    {
        glm::vec4 value;
    };

    /** Mirrors one mandatory non-instanced volume instance record. */
    struct alignas(16) Phase1VolumePerlinHostInstanceData
    {
        glm::vec4 value;
    };

    /** Mirrors one mandatory volume material component record. */
    struct alignas(16) Phase1VolumePerlinHostMaterialData
    {
        glm::vec4 value;
    };

    /** Stores the canonical volume camera, traversal, and Inspector state. */
    struct Phase1VolumePerlinHostFrame
    {
        glm::vec4 cameraPosition;
        glm::vec4 cameraRight;
        glm::vec4 cameraUp;
        glm::vec4 cameraForward;
        glm::vec4 raymarch;
        glm::vec4 viewportAndOverlay;
    };

    /** Connects either dedicated volume renderer to its unique Scene RenderSet. */
    class Phase1VolumePerlinRuntimeAdapter final
    {
    public:
        /** Builds exact ImprovedNoise data, allocates BoxGeometry, and uploads frame state. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureVolume(
                frame.cameraPosition,
                frame.cameraRight,
                frame.cameraUp,
                frame.cameraForward,
                frame.raymarch,
                frame.viewportAndOverlay);
        }

        /** Keeps the deterministic volume state immutable during warm-up frames. */
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
        /** Builds canonical CPU data and allocates the only RenderSet entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes RGBA8, metadata, and strict one-Set structural evidence. */
        void writeArtifacts(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<Phase1VolumePerlinHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<uint8_t> atlasBytes;
        Phase1VolumePerlinHostFrame frame = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        eastl::string caseId;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
