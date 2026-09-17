#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors the consolidated box vertex consumed by WebglMathObbSceneRenderSet. */
    struct alignas(16) WebglMathObbHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 barycentric;
    };

    /** Mirrors one entity's camera transform and collision color. */
    struct alignas(16) WebglMathObbHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalMatrix;
        glm::vec4 colorAndPhase;
    };

    /** Mirrors the mandatory one-entry RenderSet instance component. */
    struct alignas(16) WebglMathObbHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one Lambert material component. */
    struct alignas(16) WebglMathObbHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Mirrors one visibility/collision/hitbox flag component. */
    struct alignas(16) WebglMathObbHostRenderFlags
    {
        uint32_t values[4];
    };

    /** Stores one deterministic box entity before RenderSet allocation. */
    struct WebglMathObbHostEntity
    {
        eastl::vector<WebglMathObbHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglMathObbHostObjectData objectData{};
        WebglMathObbHostInstanceData instanceData{};
        WebglMathObbHostMaterialData materialData{};
        WebglMathObbHostRenderFlags renderFlags{};
        glm::mat4 worldModel{1.0f};
    };

    /** Builds the seeded OBB scene and records the canonical RGBA8 capture. */
    class WebglMathObbRuntimeAdapter final
    {
    public:
        /** Generates one hundred boxes, and an optional selected hitbox entity. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Applies fixed-step OBB collision state before the requested frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the final target and writes metadata plus the RenderSet snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases CPU staging state after the generated renderer has been destroyed. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked manifest scenario and allocates every RenderSet entity. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Recomputes animated transforms, OBB collisions, and object components for one frame. */
        void updateFrameState(GVM::Core::AbstractRendererImpl &renderer,
                              const ThreeSampleHostOptions &options,
                              uint32_t frameIndex);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMathObbHostEntity> entities;
        eastl::vector<GVM::Core::RenderEntityIndex> entityIndices;
        eastl::vector<glm::dvec3> basePositions;
        eastl::vector<glm::dvec3> baseRotations;
        eastl::vector<glm::dvec3> baseScales;
        eastl::vector<glm::dmat4> worldModels;
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        bool captureWritten = false;
        uint32_t selectedEntity = 0u;
    };
}
