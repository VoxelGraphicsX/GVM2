#pragma once

#include "TexturedBoxSampleData.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the generated webgl_geometry_cube DSL renderer to deterministic asset and transform data. */
    class WebglGeometryCubeRuntimeAdapter final
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

        /** Captures the requested RGBA8 frame and writes deterministic metadata and structure. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after the generated renderer releases its resources. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates the case and prepares immutable geometry and texture payloads for allocation. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        /** Allocates the only Scene entity directly at the requested deterministic capture frame. */
        void allocateSceneEntity(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Writes exact RGBA8 bytes returned by the test-only texture readback. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options, const eastl::vector<uint8_t> &rgba) const;

        /** Writes the fixed raw-image metadata used by the global Three comparison gate. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options, uint32_t frameIndex, uint32_t width, uint32_t height, uint64_t byteCount) const;

        /** Writes the single-entity Scene RenderSet topology and deterministic animation state. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options, uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<TexturedBoxHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<uint8_t> crateTextureBytes;
        eastl::vector<uint64_t> crateMipOffsets;
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
