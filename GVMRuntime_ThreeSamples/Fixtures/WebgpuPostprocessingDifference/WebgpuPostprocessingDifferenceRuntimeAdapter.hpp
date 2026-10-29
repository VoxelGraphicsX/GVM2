#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one canonical BoxGeometry position and UV payload. */
    struct alignas(16) WebgpuPostprocessingDifferenceHostVertex final
    {
        glm::vec4 position;
        glm::vec4 textureCoordinate;
    };

    /** Mirrors the target-frame model-view-projection component. */
    struct alignas(16) WebgpuPostprocessingDifferenceHostObjectData final
    {
        glm::mat4 modelViewProjection;
    };

    /** Mirrors the mandatory ordinary-entity instance component. */
    struct alignas(16) WebgpuPostprocessingDifferenceHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors the white MeshBasic color multiplier. */
    struct alignas(16) WebgpuPostprocessingDifferenceHostMaterialData final
    {
        glm::vec4 baseColor;
    };

    /** Connects exact r185 RTT state to the dedicated generated renderer. */
    class WebgpuPostprocessingDifferenceRuntimeAdapter final
    {
    public:
        /** Builds the grouped box, uploads its texture, and configures pointer color. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureEffect(pointerX, pointerY);
        }

        /** Keeps the precomputed target-frame Scene payload immutable. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes final RGBA8 and complete unique-Set structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all sample-private CPU staging storage. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and allocates the sole Scene entity. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuPostprocessingDifferenceHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> textureMipOffsets;
        WebgpuPostprocessingDifferenceHostObjectData objectData{};
        WebgpuPostprocessingDifferenceHostInstanceData instanceData{};
        WebgpuPostprocessingDifferenceHostMaterialData materialData{};
        float pointerX = 0.0f;
        float pointerY = 0.0f;
        GVM::Core::RenderEntityIndex entityIndex = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
