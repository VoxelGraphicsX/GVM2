#pragma once

#include "ThreeCompat/DeterministicRandom.hpp"
#include "ThreeSampleHostOptions.hpp"
#include "WebglCustomAttributesPointsVertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Advances the exact r185 custom point scene and feeds its private DSL renderer. */
    class WebglCustomAttributesPointsRuntimeAdapter final
    {
    public:
        /** Builds immutable point/texture resources and installs sequential frame uploads. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, indices, textureMips, projectionMatrix);
            frameUploader = [&renderer](
                                eastl::vector<float> &frameSizes,
                                const glm::mat4 &projection,
                                const glm::mat4 &modelView,
                                bool shouldDraw) {
                renderer.updateFrame(
                    frameSizes,
                    projection,
                    modelView,
                    shouldDraw);
            };
        }

        /** Advances exactly one Date.now-based callback and uploads all dynamic sizes. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures the final DSL output and writes deterministic structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases only the host callback because generated DSL owns every GPU resource. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates explicit host inputs and builds exact random, texture, and camera state. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Reproduces one sequential r185 size-wave callback and model matrix update. */
        void advanceFrameState(uint32_t frameIndex, uint32_t targetFrame);

        /** Writes the exact tightly packed RGBA8 payload returned by the GPU queue. */
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

        /** Writes ordinary indexed geometry, RNG, texture, blend, and update invariants. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        using FrameUploader = eastl::function<void(
            eastl::vector<float> &,
            const glm::mat4 &,
            const glm::mat4 &,
            bool)>;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglCustomAttributesPointsVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<float> sizes;
        eastl::vector<eastl::vector<uint8_t>> textureMips;
        ThreeCompat::DeterministicRandom random{0x18500012u};
        FrameUploader frameUploader;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 modelViewMatrix{1.0f};
        eastl::string sizeSha256;
        double virtualTimeMilliseconds = 0.0;
        double renderedVirtualTimeMilliseconds = 0.0;
        double timeValue = 0.0;
        double rotationZ = 0.0;
        uint32_t frameUpdateCount = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
