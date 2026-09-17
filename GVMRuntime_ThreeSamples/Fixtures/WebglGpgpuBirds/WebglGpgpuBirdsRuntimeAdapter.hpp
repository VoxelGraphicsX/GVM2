#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglGpgpuBirdsVertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Reconstructs the deterministic r185 flock and feeds its private DSL renderer. */
    class WebglGpgpuBirdsRuntimeAdapter final
    {
    public:
        /** Builds exact static inputs and connects sequential callback uploads. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, initialPosition, initialVelocity);
            frameUploader = [&renderer](
                                WebglGpgpuBirdsSimulationUniforms uniforms,
                                const glm::mat4 &projectionMatrix,
                                const glm::mat4 &viewMatrix,
                                uint32_t frameIndex) {
                renderer.updateFrame(
                    uniforms,
                    projectionMatrix,
                    viewMatrix,
                    frameIndex);
            };
            positionTextureProvider = [&renderer]() {
                return renderer.getCurrentPositionTextureHandle();
            };
            velocityTextureProvider = [&renderer]() {
                return renderer.getCurrentVelocityTextureHandle();
            };
        }

        /** Advances one fixed callback and uploads its GUI, pointer, and time state. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected RGBA8 target and writes deterministic evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases host callbacks after generated renderer teardown begins. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and creates exact RNG, geometry, and camera inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes the exact tightly packed RGBA8 capture. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes fixed timing, ping-pong, GUI, pointer, and byte-layout metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the no-RenderSet single-object and simulation dependency snapshot. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        using FrameUploader = eastl::function<void(
            WebglGpgpuBirdsSimulationUniforms,
            const glm::mat4 &,
            const glm::mat4 &,
            uint32_t)>;
        using TextureProvider = eastl::function<GVM::RHI::Texture()>;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglGpgpuBirdsVertex> vertices;
        eastl::vector<float4> initialPosition;
        eastl::vector<float4> initialVelocity;
        FrameUploader frameUploader;
        TextureProvider positionTextureProvider;
        TextureProvider velocityTextureProvider;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        eastl::string vertexPositionSha256;
        eastl::string vertexColorSha256;
        eastl::string vertexReferenceSha256;
        eastl::string vertexOrdinalSha256;
        eastl::string currentPositionSha256;
        eastl::string currentVelocitySha256;
        uint32_t configuredSeparation = 20u;
        uint32_t configuredAlignment = 20u;
        uint32_t configuredCohesion = 20u;
        uint32_t frameUpdateCount = 0u;
        bool interactiveScenario = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
