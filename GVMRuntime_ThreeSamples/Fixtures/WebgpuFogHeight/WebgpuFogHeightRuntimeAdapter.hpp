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
    /** Mirrors one vertical box vertex in the generated FogHeight ABI. */
    struct alignas(16) WebgpuFogHeightHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors camera, object, light, and fog state for the single entity. */
    struct alignas(16) WebgpuFogHeightHostObjectData
    {
        glm::mat4 viewProjection;
        glm::mat4 model;
        glm::mat4 view;
        glm::vec4 cameraPositionAndDensity;
        glm::vec4 fogHeightAndReserved;
        glm::vec4 lightDirectionAndIntensity;
        glm::vec4 lightColor;
    };

    /** Mirrors the per-instance matrix and stable 10 by 10 grid ordinal. */
    struct alignas(16) WebgpuFogHeightHostInstanceData
    {
        glm::vec4 transformColumn0;
        glm::vec4 transformColumn1;
        glm::vec4 transformColumn2;
        glm::vec4 transformColumn3;
        glm::vec4 ordinal;
    };

    /** Mirrors the linear MeshPhong color and ambient coefficient. */
    struct alignas(16) WebgpuFogHeightHostMaterialData
    {
        glm::vec4 baseColorAndAmbient;
        glm::vec4 specularColorAndShininess;
    };

    /** Drives the exact one-entity 100-instance height-fog example. */
    class WebgpuFogHeightRuntimeAdapter final
    {
    public:
        /** Builds the vertical box and allocates one RenderSet entity with 100 instances. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Updates fog parameters and the deterministic camera for the target frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads RGBA8 output and writes semantic Scene evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases CPU geometry after the generated renderer is destroyed. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenarios and allocates the sole Scene entity. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Recomputes camera/light/fog data from the fixed scenario state. */
        void updateObjectData(uint32_t width, uint32_t height, uint32_t frameIndex);

        /** Writes the final RGBA8 capture bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes the standard host metadata record. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes the one-Scene one-RenderSet 100-instance snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuFogHeightHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebgpuFogHeightHostInstanceData> instances;
        WebgpuFogHeightHostObjectData objectData{};
        WebgpuFogHeightHostMaterialData materialData{};
        glm::vec4 fogParameters{};
        glm::uvec4 renderFlags{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
