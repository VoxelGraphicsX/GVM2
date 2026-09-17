#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglLightprobeCubecameraData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors the generated helper object transform component. */
    struct alignas(16) WebglLightprobeCubecameraHostObjectData final
    {
        glm::mat4 projection;
        glm::mat4 modelView;
        glm::mat4 normalWorld;
    };

    /** Mirrors the mandatory identity instance component. */
    struct alignas(16) WebglLightprobeCubecameraHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors the helper intensity and material phase component. */
    struct alignas(16) WebglLightprobeCubecameraHostMaterialData final
    {
        glm::vec4 intensityAndPhase;
    };

    /** Mirrors one aligned spherical-harmonics coefficient component element. */
    struct alignas(16) WebglLightprobeCubecameraHostShData final
    {
        glm::vec4 coefficient;
    };

    /** Stores one pinned cube face and its complete CPU-built mip chain. */
    struct WebglLightprobeCubecameraFaceAsset final
    {
        eastl::vector<eastl::vector<uint8_t>> mipPixels;
    };

    /** Connects the exact Pisa cube, CPU SH projection, and helper Set to DSL. */
    class WebglLightprobeCubecameraRuntimeAdapter final
    {
    public:
        /** Decodes all faces, allocates the helper entity, and uploads the background. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureEnvironment(
                faces[0u].mipPixels,
                faces[1u].mipPixels,
                faces[2u].mipPixels,
                faces[3u].mipPixels,
                faces[4u].mipPixels,
                faces[5u].mipPixels,
                cameraRightAndTanHalfFov,
                cameraUpAndAspect,
                cameraForwardAndReserved);
        }

        /** Keeps the canonical target-frame state immutable during host warm-up. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads final RGBA8 and writes unique-Set structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all decoded CPU staging after Renderer shutdown. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenarios and prepares all non-GPU inputs. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLightprobeCubecameraVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<WebglLightprobeCubecameraFaceAsset, 6u> faces;
        eastl::array<WebglLightprobeCubecameraHostShData, 9u> shData;
        WebglLightprobeCubecameraHostObjectData objectData{};
        WebglLightprobeCubecameraHostInstanceData instanceData{};
        WebglLightprobeCubecameraHostMaterialData materialData{};
        glm::vec4 cameraRightAndTanHalfFov{};
        glm::vec4 cameraUpAndAspect{};
        glm::vec4 cameraForwardAndReserved{};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
