#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors the expanded Scene vertex shared by the dedicated morph renderers. */
    struct alignas(16) MorphTargetsHostVertex
    {
        glm::vec4 position;
    };

    /** Mirrors both absolute morph targets and the remaining source triangle corners. */
    struct alignas(16) MorphTargetsHostTriangleData
    {
        glm::vec4 spherePosition;
        glm::vec4 twistPosition;
        glm::vec4 corner1Position;
        glm::vec4 corner1SpherePosition;
        glm::vec4 corner1TwistPosition;
        glm::vec4 corner2Position;
        glm::vec4 corner2SpherePosition;
        glm::vec4 corner2TwistPosition;
    };

    /** Mirrors camera, morph, viewport, and lighting state for the sole entity. */
    struct alignas(16) MorphTargetsHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 morphWeightsAndViewport;
        glm::vec4 ambientColorAndPointIntensity;
    };

    /** Mirrors the mandatory single non-instanced component record. */
    struct alignas(16) MorphTargetsHostInstanceData
    {
        glm::vec4 translation;
    };

    /** Mirrors the private MeshPhong material component. */
    struct alignas(16) MorphTargetsHostMaterialData
    {
        glm::vec4 baseColor;
        glm::vec4 specularAndShininess;
    };

    /** Stores exact expanded BoxGeometry and the runtime RenderSet entity identity. */
    struct MorphTargetsEntityState
    {
        eastl::vector<MorphTargetsHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<MorphTargetsHostTriangleData> morphTargets;
        MorphTargetsHostObjectData objectData = {};
        MorphTargetsHostInstanceData instanceData = {};
        MorphTargetsHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Stores deterministic GUI influences and OrbitControls camera state. */
    struct MorphTargetsScenarioState
    {
        float spherify = 0.0f;
        float twist = 0.0f;
        glm::vec4 cameraPosition = glm::vec4(0.0f, 0.0f, 10.0f, 1.0f);
    };

    /** Connects either dedicated morph renderer to exact r185 CPU geometry and input state. */
    class MorphTargetsRuntimeAdapter final
    {
    public:
        /** Builds exact BoxGeometry, allocates one entity, and uploads its six group ranges as one Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Uploads deterministic camera and morph components before every rendered frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads final RGBA8 and writes capture, Scene, and geometry contract evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction begins. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates one dedicated case and creates its unique Scene RenderSet entity. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Allocates all exact geometry and component data through existing RenderSet semantics. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const MorphTargetsEntityState &entity) const;

        /** Recomputes the camera matrices and selected morph weights. */
        void updateObjectData(uint32_t width, uint32_t height);

        /** Writes the tightly packed final RGBA8 bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes stable capture identity and byte-layout metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes one-Scene, one-Set, one-entity, six-group structural evidence. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        MorphTargetsEntityState entity;
        MorphTargetsScenarioState scenarioState;
        eastl::string caseId;
        eastl::string scenePassName;
        eastl::string renderSetTypeName;
        uint32_t sourceVertexCount = 0u;
        uint32_t sourceIndexCount = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
