#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglGeometryMinecraftData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Stores one locked scenario's FirstPersonControls and replay identity. */
    struct MinecraftScenarioState
    {
        glm::dvec3 cameraPosition{0.0, 100.0, 0.0};
        glm::dvec3 velocity{0.0};
        eastl::string replaySha256;
        eastl::string replayTarget;
        uint32_t replayEventCount = 0u;
        uint32_t replayLastEventFrame = 0u;
        bool moveForward = false;
        bool moveRight = false;
        bool usesInputReplay = false;
    };

    /** Bridges CPU terrain preparation and FirstPersonControls to the private DSL renderer. */
    class WebglGeometryMinecraftRuntimeAdapter final
    {
    public:
        /** Builds, uploads, and prewarms the immutable canonical frame through generated DSL methods. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(
                vertices,
                indices,
                atlasBytes,
                atlasMipOffsets,
                makeUniforms());
            renderer.render();
            renderer.render();
            frameUploader = [&renderer](
                                WebglGeometryMinecraftUniforms uniforms) {
                renderer.updateFrame(uniforms);
            };
        }

        /** Advances the exact fixed-step keyboard replay and uploads its current camera. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the final DSL output and writes deterministic metadata and topology evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases the generated renderer callback before renderer destruction. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates inputs and prepares noise, merged faces, atlas mips, and camera state. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Returns the current projection and translation-only camera view payload. */
        [[nodiscard]] WebglGeometryMinecraftUniforms makeUniforms() const;

        /** Writes exact tightly packed RGBA8 bytes returned by the graphics queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes locked RNG, Timer, controls, atlas, and image-layout metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the RenderSet-free one-Mesh scene and deterministic pass topology. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        using FrameUploader =
            eastl::function<void(WebglGeometryMinecraftUniforms)>;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglGeometryMinecraftVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<uint8_t> atlasBytes;
        eastl::vector<uint64_t> atlasMipOffsets;
        eastl::vector<double> heightData;
        MinecraftScenarioState scenarioState;
        FrameUploader frameUploader;
        glm::mat4 projectionMatrix{1.0f};
        eastl::string atlasSha256;
        eastl::string geometrySha256;
        uint32_t terrainFaceCount = 0u;
        uint32_t finalRandomState = 0u;
        uint32_t processedFrameCount = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
