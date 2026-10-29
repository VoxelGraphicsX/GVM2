#pragma once

#include "MichelleGlbAsset.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one Michelle vertex in the generated RenderSet ABI. */
    struct alignas(16) WebglTslSkinningHostVertex final
    {
        glm::vec4 clipPosition;
        glm::vec4 worldPosition;
        glm::vec4 worldNormal;
        glm::vec4 uv;
    };

    /** Mirrors the per-entity camera and exposure component. */
    struct alignas(16) WebglTslSkinningHostObjectData final
    {
        glm::vec4 cameraPositionAndExposure;
    };

    /** Mirrors the mandatory single instance record. */
    struct alignas(16) WebglTslSkinningHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors the physical-material and light component. */
    struct alignas(16) WebglTslSkinningHostMaterialData final
    {
        glm::vec4 ambientColorAndPointIntensity;
        glm::vec4 pointPositionAndIor;
        glm::vec4 metallicDistanceAndReserved;
    };

    static_assert(sizeof(WebglTslSkinningHostVertex) == 64u);
    static_assert(sizeof(WebglTslSkinningHostObjectData) == 16u);
    static_assert(sizeof(WebglTslSkinningHostInstanceData) == 16u);
    static_assert(sizeof(WebglTslSkinningHostMaterialData) == 48u);

    /** Connects the dedicated Michelle renderer to the frozen GLB and animation. */
    class WebglTslSkinningRuntimeAdapter final
    {
    public:
        /** Configures output and allocates the one skinned entity. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            renderer.configureOutput(options.width, options.height);
            initializeResources(renderer, inDevice, options);
        }

        /** Uploads fixed-step CPU bone animation through the Set vertex component. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Writes RGBA8 and the unique-Set runtime evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases host-side decoded data after generated teardown starts. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, decodes Michelle, and allocates the Set entity. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        MichelleGlbAsset asset;
        eastl::vector<WebglTslSkinningHostVertex> vertices;
        eastl::vector<glm::vec4> skinnedPositions;
        eastl::vector<glm::vec4> skinnedNormals;
        WebglTslSkinningHostObjectData objectData = {};
        WebglTslSkinningHostInstanceData instanceData = {};
        WebglTslSkinningHostMaterialData materialData = {};
        glm::mat4 objectModel = glm::mat4(1.0f);
        uint32_t entityIndex = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
