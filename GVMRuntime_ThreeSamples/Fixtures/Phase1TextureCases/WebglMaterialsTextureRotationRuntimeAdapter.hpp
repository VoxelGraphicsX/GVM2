#pragma once

#include "TexturedBoxSampleData.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Stores the locked UV parameters, OrbitControls camera, and replay identity for one scenario. */
    struct TextureRotationScenarioState
    {
        double offsetX = 0.0;
        double offsetY = 0.0;
        double repeatX = 0.25;
        double repeatY = 0.25;
        double rotation = 0.7853981633974483;
        double centerX = 0.5;
        double centerY = 0.5;
        double cameraX = 10.0;
        double cameraY = 15.0;
        double cameraZ = 25.0;
        eastl::string replaySha256;
        eastl::string replayTarget;
        uint32_t replayEventCount = 0u;
        uint32_t replayLastEventFrame = 0u;
        bool usesInputReplay = false;
    };

    /** Connects the generated texture-rotation DSL renderer to the locked r185 loader state. */
    class WebglMaterialsTextureRotationRuntimeAdapter final
    {
    public:
        /** Decodes the pinned JPEG, prepares explicit mips, and allocates the only Scene entity. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the immutable capture-state entity unchanged during deterministic warm-up. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads the requested RGBA8 frame and writes metadata, structure, and loader semantics. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after generated renderer resource destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates inputs and prepares immutable geometry, material, camera, and texture data. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Allocates the packed six-group BoxGeometry as one RenderSet entity. */
        void allocateSceneEntity(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

        /** Writes exact RGBA8 bytes returned by the DSL output texture readback. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes fixed capture identity and the canonical input-replay digest when present. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes the one-Scene, one-RenderSet entity and pass topology snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        /** Writes the exact canonical loader semantic sidecar required by the global runner. */
        void writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options) const;

        GVM::Core::DeviceProxy device;
        TextureRotationScenarioState scenarioState;
        eastl::vector<TexturedBoxHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<uint8_t> uvGridTextureBytes;
        eastl::vector<uint64_t> uvGridMipOffsets;
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
