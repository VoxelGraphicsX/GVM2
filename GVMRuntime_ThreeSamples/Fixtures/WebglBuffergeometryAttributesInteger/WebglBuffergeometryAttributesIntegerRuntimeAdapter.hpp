#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "WebglBuffergeometryAttributesIntegerVertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects deterministic r185 integer geometry and decoded assets to the DSL renderer. */
    class WebglBuffergeometryAttributesIntegerRuntimeAdapter final
    {
    public:
        /** Prepares immutable CPU data and uploads it only through generated DSL methods. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(
                vertices,
                modelViewProjection,
                crateWidth,
                crateHeight,
                crateMips,
                floorWidth,
                floorHeight,
                floorMips,
                grassWidth,
                grassHeight,
                grassMips);
        }

        /** Keeps the manifest-selected target-frame state fixed during host warm-up frames. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Reads the selected DSL output and writes metadata plus a structural snapshot. */
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
        /** Validates the locked scenario and prepares geometry, transforms, and asset mips. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

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

        /** Writes the RenderSet-free Scene topology and immutable source evidence. */
        void writeStructuralSnapshot(
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglBuffergeometryAttributesIntegerVertex> vertices;
        eastl::vector<eastl::vector<uint8_t>> crateMips;
        eastl::vector<eastl::vector<uint8_t>> floorMips;
        eastl::vector<eastl::vector<uint8_t>> grassMips;
        glm::mat4 modelViewProjection{1.0f};
        uint32_t crateWidth = 0u;
        uint32_t crateHeight = 0u;
        uint32_t floorWidth = 0u;
        uint32_t floorHeight = 0u;
        uint32_t grassWidth = 0u;
        uint32_t grassHeight = 0u;
        uint32_t finalGeometryRandomState = 0u;
        double timeSeconds = 0.0;
        double rotationX = 0.0;
        double rotationY = 0.0;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
