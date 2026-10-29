#pragma once

#include "ThreeCompat/DeterministicRandom.hpp"
#include "ThreeSampleHostOptions.hpp"
#include "WebglCustomAttributesPoints2VertexData.hpp"

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
    /** Reconstructs the exact r185 sorted point scene and feeds its private DSL renderer. */
    class WebglCustomAttributesPoints2RuntimeAdapter final
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
            renderer.configureScene(vertices, indices, textureMips);
            frameUploader = [&renderer](
                                eastl::vector<float> &frameSizes,
                                eastl::vector<uint32_t> &frameIndices,
                                const glm::mat4 &projection,
                                const glm::mat4 &modelView) {
                renderer.updateFrame(
                    frameSizes,
                    frameIndices,
                    projection,
                    modelView);
            };
        }

        /** Advances one fixed Date.now callback and uploads sizes, order, and matrices. */
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
        /** Validates explicit inputs and builds exact geometry, texture, RNG, and camera state. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Reproduces one sequential size update, stable stale-matrix sort, and current draw matrix. */
        void advanceFrameState(uint32_t frameIndex);

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

        /** Writes ordinary topology, RNG, texture, sort-lag, and draw-state invariants. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        using FrameUploader = eastl::function<void(
            eastl::vector<float> &,
            eastl::vector<uint32_t> &,
            const glm::mat4 &,
            const glm::mat4 &)>;

        GVM::Core::DeviceProxy device;
        eastl::vector<glm::vec3> logicalPositions;
        eastl::vector<WebglCustomAttributesPoints2Vertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<float> sizes;
        eastl::vector<eastl::vector<uint8_t>> textureMips;
        ThreeCompat::DeterministicRandom random{0x18500013u};
        FrameUploader frameUploader;
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 modelViewMatrix{1.0f};
        eastl::string sizeSha256;
        eastl::string logicalIndexSha256;
        eastl::string expandedIndexSha256;
        double virtualTimeMilliseconds = 0.0;
        double renderedVirtualTimeMilliseconds = 0.0;
        double timeValue = 0.0;
        double currentRotation = 0.0;
        double sortRotation = 0.0;
        double previousRenderedRotation = 0.0;
        uint32_t frameUpdateCount = 0u;
        bool cameraMatricesInitialized = false;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
