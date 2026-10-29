#pragma once

#include "ThreeSampleHostOptions.hpp"
#include "ThreeCompat/DeterministicRandom.hpp"
#include "Phase1LinesPointsSimpleData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace GVM::ThreeSamples
{
    /** Connects deterministic points, texture mips, and GUI state to the generated billboard renderer. */
    class WebglPointsBillboardsRuntimeAdapter final
    {
    public:
        /** Builds expanded point geometry and uploads one canonical material/camera state. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(inDevice, options);
            renderer.configureScene(vertices, indices, textureMips,
                                    modelView[0][0], modelView[0][1], modelView[0][2], modelView[0][3],
                                    modelView[1][0], modelView[1][1], modelView[1][2], modelView[1][3],
                                    modelView[2][0], modelView[2][1], modelView[2][2], modelView[2][3],
                                    modelView[3][0], modelView[3][1], modelView[3][2], modelView[3][3],
                                    projection[0][0], projection[0][1], projection[0][2], projection[0][3],
                                    projection[1][0], projection[1][1], projection[1][2], projection[1][3],
                                    projection[2][0], projection[2][1], projection[2][2], projection[2][3],
                                    projection[3][0], projection[3][1], projection[3][2], projection[3][3],
                                    materialColor.x, materialColor.y, materialColor.z,
                                    sizeAttenuation ? 1.0f : 0.0f);
        }

        /** Keeps the selected fixed-frame state immutable through deterministic warm-up frames. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the DSL output and writes capture metadata plus the simple-Scene snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing teardown after generated renderer destruction begins. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and reconstructs its CPU-side payload. */
        void initializeResources(GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Writes one tightly packed RGBA8 capture. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes deterministic capture and optional GUI replay identity. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes point counts, texture layout, and ordinary draw evidence. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglPointsBillboardsVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<eastl::vector<uint8_t>> textureMips;
        glm::mat4 modelView{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 materialColor{1.0f};
        bool sizeAttenuation = true;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
