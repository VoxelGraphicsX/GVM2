#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebglLightprobeData.hpp"

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
    struct alignas(16) WebglLightprobeHostObjectData final
    {
        glm::mat4 projection;
        glm::mat4 modelView;
        glm::mat4 normalWorld;
    };

    /** Mirrors the mandatory identity instance component. */
    struct alignas(16) WebglLightprobeHostInstanceData final
    {
        glm::vec4 reserved;
    };

    /** Mirrors the helper intensity and material phase component. */
    struct alignas(16) WebglLightprobeHostMaterialData final
    {
        glm::vec4 intensityAndPhase;
    };

    /** Mirrors one entity-local record containing all nine SH coefficients. */
    struct alignas(16) WebglLightprobeHostShData final
    {
        eastl::array<glm::vec4, 9u> coefficients;
    };

    /** Stores one pinned cube face and its complete CPU-built mip chain. */
    struct WebglLightprobeFaceAsset final
    {
        eastl::vector<eastl::vector<uint8_t>> mipPixels;
        eastl::vector<uint8_t> uploadPixels;
        eastl::vector<uint64_t> mipmapOffsetBytes;
    };

    /** Connects the exact Pisa cube, CPU SH projection, and helper Set to DSL. */
    class WebglLightprobeRuntimeAdapter final
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
        eastl::vector<WebglLightprobeVertex> materialVertices;
        eastl::vector<uint32_t> materialIndices;
        eastl::vector<WebglLightprobeVertex> helperVertices;
        eastl::vector<uint32_t> helperIndices;
        eastl::array<WebglLightprobeFaceAsset, 6u> faces;
        WebglLightprobeHostShData shData{};
        eastl::array<WebglLightprobeHostObjectData, 2u> objectData{};
        WebglLightprobeHostInstanceData instanceData{};
        eastl::array<WebglLightprobeHostMaterialData, 2u> materialData{};
        glm::vec4 cameraRightAndTanHalfFov{};
        glm::vec4 cameraUpAndAspect{};
        glm::vec4 cameraForwardAndReserved{};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
