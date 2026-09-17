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
    /** Mirrors one TorusKnotGeometry position in the depth Scene. */
    struct alignas(16) WebgpuDepthTextureHostVertex
    {
        glm::vec4 position;
    };

    /** Stores the camera projection and deterministic object transform. */
    struct alignas(16) WebgpuDepthTextureHostObjectData
    {
        glm::mat4 modelViewProjection;
    };

    /** Stores one entry in the mandatory non-instanced component. */
    struct alignas(16) WebgpuDepthTextureHostInstanceData
    {
        glm::vec4 reserved{0.0f};
    };

    /** Stores the opaque MeshBasic material color. */
    struct alignas(16) WebgpuDepthTextureHostMaterialData
    {
        glm::vec4 baseColor;
    };

    /** Stores one depth-scene object and its RenderSet entity. */
    struct WebgpuDepthTextureEntityData
    {
        eastl::vector<WebgpuDepthTextureHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebgpuDepthTextureHostObjectData objectData{};
        WebgpuDepthTextureHostInstanceData instanceData{};
        WebgpuDepthTextureHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Drives the deterministic depth-scene geometry through one RenderSet. */
    class WebgpuDepthTextureRuntimeAdapter final
    {
    public:
        /** Generates 50 depth-tested entities and allocates the Scene Set. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates the fixed camera orbit. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Captures depth-scene output and structural metadata. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases generated depth-scene geometry. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates scenarios and allocates the depth-scene entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Applies a deterministic one-frame camera orbit. */
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

        /** Writes one-Scene depth structural evidence. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuDepthTextureEntityData> entities;
        glm::mat4 viewProjection{1.0f};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
