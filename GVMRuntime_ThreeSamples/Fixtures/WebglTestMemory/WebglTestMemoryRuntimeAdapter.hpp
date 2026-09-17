#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one expanded WebGL wireframe segment corner. */
    struct alignas(16) WebglTestMemoryHostVertex
    {
        glm::vec4 position;
        glm::vec4 lineEnd;
        glm::vec4 lineData;
    };

    /** Mirrors the target-frame camera and viewport component. */
    struct alignas(16) WebglTestMemoryHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
        glm::vec4 viewport;
    };

    /** Mirrors the mandatory non-instanced component. */
    struct alignas(16) WebglTestMemoryHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors the linear material multiplier. */
    struct alignas(16) WebglTestMemoryHostMaterialData
    {
        glm::vec4 colorAndOpacity;
    };

    /** Mirrors the lifetime generation and visibility record. */
    struct alignas(16) WebglTestMemoryHostRenderFlags
    {
        glm::vec4 generationAndVisibility;
    };

    /** Stores the current temporary sphere entity staging payload. */
    struct WebglTestMemoryEntityState final
    {
        eastl::vector<WebglTestMemoryHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglTestMemoryHostObjectData objectData{};
        WebglTestMemoryHostInstanceData instanceData{};
        WebglTestMemoryHostMaterialData materialData{};
        WebglTestMemoryHostRenderFlags renderFlags{};
        eastl::vector<uint8_t> textureBytes;
        eastl::vector<uint64_t> mipOffsets;
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Connects deterministic allocation churn to the dedicated one-Set Renderer. */
    class WebglTestMemoryRuntimeAdapter final
    {
    public:
        /** Builds the target sphere and allocates the first temporary entity. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Reallocates the temporary entity once before every churn frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the target image and writes structural churn evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases CPU staging state after Renderer teardown starts. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and creates exact target-frame data. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Allocates one complete sphere through generated component handles. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            uint32_t generation) const;

        WebglTestMemoryEntityState entity;
        GVM::Core::DeviceProxy device;
        uint32_t finalRandomState = 0u;
        uint32_t widthSegments = 0u;
        uint32_t heightSegments = 0u;
        uint32_t reallocationCount = 0u;
        bool memoryChurn = false;
        bool captureWritten = false;
    };
}
