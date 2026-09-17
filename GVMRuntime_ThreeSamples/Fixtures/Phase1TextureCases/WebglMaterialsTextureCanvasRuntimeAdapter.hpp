#pragma once

#include "CanvasTextureData.hpp"
#include "TexturedBoxSampleData.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the webgl_materials_texture_canvas DSL renderer to CPU canvas input and capture. */
    class WebglMaterialsTextureCanvasRuntimeAdapter final
    {
    public:
        /** Prepares deterministic host data for the generated renderer's exported Scene RenderSet. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the capture-frame entity immutable during deterministic host warm-up frames. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Captures the selected RGBA8 output and writes replay-aware audit metadata. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after the generated renderer finishes. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and prepares replay and geometry payloads for allocation. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Allocates the only Scene entity directly at the requested deterministic capture frame. */
        void allocateSceneEntity(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Writes exact RGBA8 bytes returned by the test-only texture readback. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes raw-image metadata and the immutable canonical input-replay identity. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes the single-entity Scene RenderSet, canvas state, and rotation snapshot. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<TexturedBoxHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        CanvasTextureReplayResult replayResult;
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
