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
    /** Mirrors one packed TorusKnot or ground vertex consumed by the DSL. */
    struct alignas(16) WebglClippingHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors the per-entity camera, light, fog, and shadow components. */
    struct alignas(16) WebglClippingHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 shadowModelViewProjection;
        glm::vec4 spotDirectionAndIntensity;
        glm::vec4 directionalDirectionAndIntensity;
        glm::vec4 ambientAndFogNear;
        glm::vec4 fogColorAndFar;
    };

    /** Mirrors the mandatory one-entry non-instanced component. */
    struct alignas(16) WebglClippingHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one material's linear base color and ground flag. */
    struct alignas(16) WebglClippingHostMaterialData
    {
        glm::vec4 baseColorAndFlags;
    };

    /** Mirrors local/global plane equations and clipping enable flags. */
    struct alignas(16) WebglClippingHostPlaneData
    {
        glm::vec4 localPlane;
        glm::vec4 globalPlaneView;
        glm::vec4 enableAndReserved;
    };

    /** Mirrors visible-pass phase, sidedness, and shadow flags. */
    struct alignas(16) WebglClippingHostRenderFlagsData
    {
        glm::vec4 phaseAndFlags;
    };

    /** Stores one complete entity before it is allocated in the sole Set. */
    struct WebglClippingEntityData
    {
        eastl::vector<WebglClippingHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglClippingHostObjectData objectData{};
        WebglClippingHostInstanceData instanceData{};
        WebglClippingHostMaterialData materialData{};
        WebglClippingHostPlaneData planeData{};
        WebglClippingHostRenderFlagsData renderFlags{};
        const char *logicalId = nullptr;
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Builds the r185 clipped TorusKnot/ground scene and capture evidence. */
    class WebglClippingRuntimeAdapter final
    {
    public:
        /** Generates both entities and configures the DSL output attachments. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Applies fixed-step transforms and replay-selected clipping state. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads RGBA8 output and writes scene/contract snapshots. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases CPU staging state after the generated renderer is destroyed. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and allocates the two Scene entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Generates the exact indexed r185 TorusKnotGeometry topology. */
        void buildTorusKnot(WebglClippingEntityData &entity);

        /** Generates the ground PlaneGeometry used by the shadow receiver. */
        void buildGround(WebglClippingEntityData &entity);

        /** Updates transforms, plane equations, and material flags for a frame. */
        void updateFrame(const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Allocates one complete entity through existing RenderSet semantics. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            WebglClippingEntityData &entity);

        /** Creates parent directories for an optional evidence file. */
        void prepareOutputPath(const eastl::string &path) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglClippingEntityData> entities;
        bool captureWritten = false;
    };
}
