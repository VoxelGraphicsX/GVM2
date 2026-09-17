#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglGpgpuProtoplanetData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Reconstructs the deterministic r185 protoplanet state for its private DSL renderer. */
    class WebglGpgpuProtoplanetRuntimeAdapter final
    {
    public:
        /** Builds the exact initial or restarted textures and connects frame uploads. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(initialPosition, initialVelocity);
            frameUploader = [&renderer](WebglGpgpuProtoplanetSimulationUniforms uniforms, const glm::mat4 &projectionMatrix, const glm::mat4 &viewMatrix, float cameraConstant, float density, uint32_t frameIndex) {
                renderer.updateFrame(uniforms, projectionMatrix, viewMatrix, cameraConstant, density, frameIndex);
            };
            positionTextureProvider = [&renderer]() {
                return renderer.getCurrentPositionTextureHandle();
            };
            velocityTextureProvider = [&renderer]() {
                return renderer.getCurrentVelocityTextureHandle();
            };
        }

        /** Uploads one fixed callback's gravity, density, and camera state. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads the selected RGBA8 and simulation targets into evidence artifacts. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Releases generated-renderer callbacks before resource destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario and builds exact RNG and camera inputs. */
        void initializeResources(GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Writes one tightly packed RGBA8 capture to its explicit output path. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes timing, RNG, fragment-ping-pong, and texture digest metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes the one-object no-RenderSet structural snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        using FrameUploader = eastl::function<void(WebglGpgpuProtoplanetSimulationUniforms, const glm::mat4 &, const glm::mat4 &, float, float, uint32_t)>;
        using TextureProvider = eastl::function<GVM::RHI::Texture()>;

        GVM::Core::DeviceProxy device;
        eastl::vector<float4> initialPosition;
        eastl::vector<float4> initialVelocity;
        FrameUploader frameUploader;
        TextureProvider positionTextureProvider;
        TextureProvider velocityTextureProvider;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        eastl::string initialPositionSha256;
        eastl::string initialVelocitySha256;
        eastl::string currentPositionSha256;
        eastl::string currentVelocitySha256;
        float gravityConstant = 100.0f;
        float density = 0.45f;
        float cameraConstant = 0.0f;
        uint32_t finalRandomState = 0u;
        uint32_t frameUpdateCount = 0u;
        bool restartScenario = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
