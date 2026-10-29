#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglMaterialsTexturePartialupdateData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects deterministic r185 partial-update state to the generated DSL renderer. */
    class WebglMaterialsTexturePartialupdateRuntimeAdapter final
    {
    public:
        /** Decodes the pinned asset and uploads only CPU-prepared data through generated methods. */
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
                patches,
                baseTextureBytes,
                patchTextureBytes,
                modelViewProjection,
                patchCount);
        }

        /** Keeps the selected canonical target state fixed through deterministic warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the final DSL-created RGBA8 target and writes all capture evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction begins. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario and prepares geometry, texture bytes, patches, and camera state. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Writes the exact tightly packed RGBA8 payload returned by the graphics queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic capture identity and image-layout metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes the ordinary-RenderClass topology and compute-update contract. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglMaterialsTexturePartialupdateVertex> vertices;
        eastl::vector<uint> indices;
        eastl::vector<WebglMaterialsTexturePartialupdatePatch> patches;
        eastl::vector<uint8_t> baseTextureBytes;
        eastl::vector<uint8_t> patchTextureBytes;
        glm::mat4 modelViewProjection{1.0f};
        uint32_t patchCount = 0u;
        uint32_t initialRandomState = 0u;
        uint32_t finalRandomState = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
