#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Stores one shared OBJ point animation state used by eight clone entities. */
    struct WebglPointsDynamicGroupState
    {
        eastl::vector<glm::vec4> initialPositions;
        eastl::vector<glm::vec4> positions;
        uint32_t firstEntity = 0u;
        uint32_t pointCount = 0u;
        uint32_t start = 0u;
        uint32_t delay = 0u;
        uint32_t verticesDown = 0u;
        uint32_t verticesUp = 0u;
        int32_t direction = 0;
        double speed = 15.0;
        double cloneSpeeds[8u] = {};
    };

    /** Mirrors one OBJ point and its CPU-authored billboard corner. */
    struct alignas(16) WebglPointsDynamicHostVertex
    {
        glm::vec4 position;
        glm::vec2 corner;
        glm::vec2 cornerPadding;
        glm::vec4 color;
    };

    /** Stores one entity camera transform, point size, fog density, and material index. */
    struct alignas(16) WebglPointsDynamicHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 viewportPointSizeAndFog;
        glm::uvec4 materialAndFlags;
    };

    /** Stores the one-entry instance component used by every point entity. */
    struct alignas(16) WebglPointsDynamicHostInstanceData
    {
        glm::vec4 tint;
    };

    /** Stores one point material tint. */
    struct alignas(16) WebglPointsDynamicHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Stores one expanded point and its RenderSet entity index. */
    struct WebglPointsDynamicEntityData
    {
        eastl::vector<WebglPointsDynamicHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglPointsDynamicHostObjectData objectData{};
        WebglPointsDynamicHostObjectData baseObjectData{};
        WebglPointsDynamicHostInstanceData instanceData{};
        WebglPointsDynamicHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        eastl::vector<glm::vec4> sourcePositions;
        glm::vec3 entityOrigin = glm::vec3(0.0f);
        float entityScale = 1.0f;
        uint32_t randomState = 0u;
        bool isGrid = false;
    };

    /** Drives the animated point field through one Scene RenderSet. */
    class WebglPointsDynamicRuntimeAdapter final
    {
    public:
        /** Generates the deterministic point field and allocates one Set entity per point. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the collapse animation and the EffectComposer film clock. */
        template <class RendererImpl>
        void beforeFrame(RendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex)
        {
            // Timer starts at the virtual clock origin.  The first callback
            // therefore contributes zero delta; every later callback advances
            // by the fixed 60 Hz step used by the browser reference.
            // EffectComposer.render(0.01) advances FilmPass.time by the
            // fixed delta supplied by the r185 sample on every callback,
            // including the first captured frame.  Preserve that clock
            // instead of deriving it from the simulation frame rate.
            renderer.updateFilmTime(0.01f * float(frameIndex + 1u));
            beforeFrameResources(renderer, options, frameIndex);
        }

        /** Captures the point field and structural metadata. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases the generated point geometry. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates scenarios and allocates the point field. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Uploads animated RenderSet components after the generated screen uniforms are updated. */
        void beforeFrameResources(GVM::Core::AbstractRendererImpl &renderer,
                                  const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex);

        /** Applies the deterministic parent and clone animation to object components. */
        void updateObjectData(uint32_t frameIndex, bool advanceAnimation = true);

        /** Writes optional RGBA8 bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes standard capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the one-Scene point field snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        /** Writes the canonical loader semantic sidecar for the OBJ scene. */
        void writeSemanticSnapshot(const ThreeSampleHostOptions &options,
                                   uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglPointsDynamicEntityData> entities;
        eastl::vector<WebglPointsDynamicGroupState> groups;
        uint32_t animationRandomState = 0u;
        double parentRotationY = 0.0;
        double cloneRotationY[9u][8u] = {};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
