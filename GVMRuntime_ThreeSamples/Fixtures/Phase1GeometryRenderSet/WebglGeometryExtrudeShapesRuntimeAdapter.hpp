#pragma once

#include "ExtrudeGeometry.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one entity transform, viewport, and Lambert lighting state. */
    struct alignas(16) ExtrudeHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 viewportAndReserved;
        glm::vec4 ambientColorAndReserved;
        glm::vec4 pointLightViewPositionAndIntensity;
    };

    /** Mirrors the mandatory single non-instanced component record. */
    struct alignas(16) ExtrudeHostInstanceData
    {
        glm::vec4 translation;
    };

    /** Mirrors one private MeshLambert material color. */
    struct alignas(16) ExtrudeHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Stores one exact extrusion entity and its runtime identity. */
    struct ExtrudeEntityState
    {
        eastl::string logicalId;
        eastl::string geometrySha256;
        ExtrudeCpuGeometry geometry;
        eastl::vector<uint32_t> indices;
        ExtrudeHostObjectData objectData = {};
        ExtrudeHostInstanceData instanceData = {};
        eastl::array<ExtrudeHostMaterialData, 2u> materials = {};
        glm::mat4 model = glm::mat4(1.0f);
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Stores deterministic TrackballControls camera and replay evidence. */
    struct ExtrudeScenarioState
    {
        glm::dvec3 cameraPosition = glm::dvec3(0.0, 0.0, 500.0);
        glm::dvec3 cameraUp = glm::dvec3(0.0, 1.0, 0.0);
        eastl::string replaySha256;
        uint32_t replayEventCount = 0u;
    };

    /** Connects the dedicated extrusion renderer to exact r185 CPU geometry. */
    class WebglGeometryExtrudeShapesRuntimeAdapter final
    {
    public:
        /** Builds all three extrusions and allocates exactly three Scene entities. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Uploads camera-following point-light transforms before each frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Writes final RGBA8 and complete Scene contract evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing teardown after generated resource destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, builds geometry, and creates the unique Set. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Allocates one ordinary entity through existing RenderSet semantics. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const ExtrudeEntityState &entity) const;

        /** Recomputes all entity matrices from deterministic Trackball state. */
        void updateObjectData(uint32_t width, uint32_t height);

        /** Writes exact tightly packed RGBA8 capture bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes stable capture identity and replay metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes one-Scene, one-Set, three-entity structural evidence. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::array<ExtrudeEntityState, 3u> entities;
        ExtrudeScenarioState scenarioState;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
