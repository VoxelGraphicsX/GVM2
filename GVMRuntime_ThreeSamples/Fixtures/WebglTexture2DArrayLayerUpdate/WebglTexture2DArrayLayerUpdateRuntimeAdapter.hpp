#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors the generated shared-quad vertex layout. */
    struct WebglTexture2DArrayLayerUpdateHostVertex
    {
        glm::vec4 positionAndUv;
    };

    /** Mirrors the generated per-entity projection component. */
    struct WebglTexture2DArrayLayerUpdateHostObjectData
    {
        glm::vec4 projectionScaleAndReserved;
    };

    /** Mirrors one generated vertical-offset and texture-slot instance record. */
    struct WebglTexture2DArrayLayerUpdateHostInstanceData
    {
        glm::vec4 verticalOffsetAndTextureSlot;
    };

    /** Mirrors the generated identity material component. */
    struct WebglTexture2DArrayLayerUpdateHostMaterialData
    {
        glm::vec4 tint;
    };

    /** Connects the dedicated texture-layer RenderSet renderer to the generated host. */
    class WebglTexture2DArrayLayerUpdateRuntimeAdapter final
    {
    public:
        /** Validates decoded KTX2 layers and allocates the single three-instance entity. */
        void initialize(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Leaves the immutable transfer state unchanged before each frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the DSL target and writes image and RenderSet evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU layer storage after generated renderer shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        GVM::Core::DeviceProxy device;
        eastl::vector<eastl::vector<uint8_t>> layers;
        uint32_t entityIndex = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
