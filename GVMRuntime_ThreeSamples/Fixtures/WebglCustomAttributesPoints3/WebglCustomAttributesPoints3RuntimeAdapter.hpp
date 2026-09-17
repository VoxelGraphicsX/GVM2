#pragma once

#include "ThreeCompat/DeterministicRandom.hpp"
#include "ThreeSampleHostOptions.hpp"
#include "WebglCustomAttributesPoints3VertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Reconstructs the exact r185 alpha-tested point scene for its private DSL renderer. */
    class WebglCustomAttributesPoints3RuntimeAdapter final
    {
    public:
        /** Builds immutable geometry and texture resources and installs frame uploads. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, indices, textureMips);
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

        /** Advances one fixed Date.now callback and uploads sizes and current matrices. */
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

        /** Releases only the callback because generated DSL owns every GPU resource. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates inputs and builds exact random geometry, ball mips, and camera state. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Reproduces one sequential r185 size callback and equal Y/Z rotation. */
        void advanceFrameState(uint32_t frameIndex, uint32_t targetFrame);

        /** Writes the exact tightly packed RGBA8 payload returned by the GPU queue. */
        void writeRgbaCapture(
            const ThreeSampleHostOptions &options,
            const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic capture identity and fixed-clock metadata. */
        void writeCaptureMetadata(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            uint32_t width,
            uint32_t height,
            uint64_t byteCount) const;

        /** Writes point expansion, fog, discard, RNG, and asset invariants. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        using FrameUploader = eastl::function<void(
            eastl::vector<float> &,
            const glm::mat4 &,
            const glm::mat4 &,
            bool)>;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglCustomAttributesPoints3Vertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<float> sizes;
        eastl::vector<eastl::vector<uint8_t>> textureMips;
        ThreeCompat::DeterministicRandom random{0x18500014u};
        FrameUploader frameUploader;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 modelViewMatrix{1.0f};
        eastl::string sizeSha256;
        double virtualTimeMilliseconds = 0.0;
        double renderedVirtualTimeMilliseconds = 0.0;
        double timeValue = 0.0;
        double rotationY = 0.0;
        double rotationZ = 0.0;
        uint32_t zeroSizeCount = 0u;
        uint32_t frameUpdateCount = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
