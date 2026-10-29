#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one union-layout vertex consumed by the dedicated DSL shard. */
    struct alignas(16) WebglInteractiveBuffergeometryHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 color;
        glm::vec4 auxiliary;
    };

    /** Mirrors the camera, lighting, and fog object component. */
    struct alignas(16) WebglInteractiveBuffergeometryHostObjectData
    {
        glm::mat4 modelView;
        glm::mat4 projection;
        glm::vec4 viewport;
        glm::vec4 lightAndAmbient;
        glm::vec4 fogColorAndRange;
        glm::vec4 fogFarAndReserved;
    };

    /** Mirrors the required non-instanced instance record. */
    struct alignas(16) WebglInteractiveBuffergeometryHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one mesh or highlight material phase. */
    struct alignas(16) WebglInteractiveBuffergeometryHostMaterialData
    {
        glm::vec4 colorAndPhase;
    };

    /** Stores one complete entity allocation payload before upload. */
    struct WebglInteractiveBuffergeometryEntityData
    {
        eastl::vector<WebglInteractiveBuffergeometryHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglInteractiveBuffergeometryHostObjectData objectData{};
        WebglInteractiveBuffergeometryHostInstanceData instanceData{};
        WebglInteractiveBuffergeometryHostMaterialData materialData{};
        const char *name = nullptr;
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Supplies deterministic 5000-triangle data and CPU face-hit updates. */
    class WebglInteractiveBuffergeometryRuntimeAdapter final
    {
    public:
        /** Builds the mesh and the hidden highlight entity in the one Scene Set. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Applies the deterministic face-hit material update at the capture frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures the DSL output and writes scene contract artifacts. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing adapter teardown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and allocates the two RenderSet entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Builds the deterministic non-index-shared random triangle mesh. */
        void buildMesh();

        /** Builds the four-point outline around the closest CPU-raycast triangle. */
        void buildHighlight(uint32_t frameIndex);

        /** Allocates one entity with the supplied payload through the RenderSet encoder. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            WebglInteractiveBuffergeometryEntityData &entity);

        /** Writes the captured RGBA8 payload. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes stable capture metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the one-Set entity, component, and pass snapshot. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglInteractiveBuffergeometryEntityData> entities;
        bool captureWritten = false;
        bool highlightEnabled = false;
        uint32_t selectedTriangle = UINT32_MAX;
    };
}
