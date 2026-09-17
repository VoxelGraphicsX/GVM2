#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one simplified-mesh vertex from the private DSL shard. */
struct alignas(16) WebglModifierSimplifierHostVertex
{
    glm::vec4 position;
    glm::vec4 normal;
    };

    /** Stores a normalized screen transform and material selector. */
struct alignas(16) WebglModifierSimplifierHostObjectData
{
    glm::mat4 modelView;
    glm::mat4 projection;
    glm::mat4 normalMatrix;
    glm::uvec4 materialAndFlags;
};

    /** Stores the one instance transform and tint used by both comparison meshes. */
    struct alignas(16) WebglModifierSimplifierHostInstanceData
    {
        glm::vec4 offsetAndScale;
        glm::vec4 tint;
    };

    /** Stores one standard material base color. */
    struct alignas(16) WebglModifierSimplifierHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Stores one original or simplified mesh entity. */
    struct WebglModifierSimplifierEntityData
    {
        eastl::vector<WebglModifierSimplifierHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglModifierSimplifierHostObjectData objectData{};
        WebglModifierSimplifierHostInstanceData instanceData{};
        WebglModifierSimplifierHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the original-versus-simplified comparison through one RenderSet. */
    class WebglModifierSimplifierRuntimeAdapter final
    {
    public:
        /** Generates both deterministic comparison meshes and allocates the Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the fixed orbit comparison state. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Captures RGBA8 output and the comparison scene snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases generated comparison geometry. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates scenarios and allocates original and simplified entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Applies a deterministic one-pixel-equivalent orbit shift. */
        void updateObjectData(uint32_t frameIndex);

        /** Writes optional RGBA8 bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes standard capture metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the one-Scene two-entity snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglModifierSimplifierEntityData> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
