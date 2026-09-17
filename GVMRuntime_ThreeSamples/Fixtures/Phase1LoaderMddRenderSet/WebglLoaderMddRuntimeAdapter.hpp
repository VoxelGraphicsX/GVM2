#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one expanded position and immutable face normal in the Scene Set. */
    struct alignas(16) MddHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
    };

    /** Mirrors four absolute MDD positions for one expanded geometry vertex. */
    struct alignas(16) MddHostMorphTargetData
    {
        glm::vec4 frame0;
        glm::vec4 frame1;
        glm::vec4 frame2;
        glm::vec4 frame3;
    };

    /** Mirrors camera transforms, MDD frame weights, and viewport dimensions. */
    struct alignas(16) MddHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 normalMatrix;
        glm::vec4 frameWeights;
        glm::vec4 viewportAndReserved;
    };

    /** Mirrors the mandatory single non-instanced component record. */
    struct alignas(16) MddHostInstanceData
    {
        glm::vec4 translation;
    };

    /** Mirrors the private MeshNormal material component. */
    struct alignas(16) MddHostMaterialData
    {
        glm::vec4 opacityAndReserved;
    };

    /** Stores decoded MDD frames and their source times. */
    struct MddDecodedAsset
    {
        eastl::array<float, 4u> times = {};
        eastl::array<eastl::vector<glm::vec4>, 4u> frames;
        eastl::string sourceSha256;
    };

    /** Stores the expanded Set payload and its unique runtime entity identity. */
    struct MddEntityState
    {
        eastl::vector<MddHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<MddHostMorphTargetData> morphTargets;
        MddHostObjectData objectData = {};
        MddHostInstanceData instanceData = {};
        MddHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Connects the dedicated MDD renderer to r185 parsing and animation behavior. */
    class WebglLoaderMddRuntimeAdapter final
    {
    public:
        /** Parses the locked MDD asset and allocates one expanded Scene entity. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Uploads the deterministic MDD animation weights before each frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Writes final RGBA8, structural, and loader semantic evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing teardown after generated resource destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, parses geometry, and creates the unique Set. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Allocates all exact geometry and component data through RenderSet. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder) const;

        /** Recomputes the exact camera matrices and selected MDD frame weights. */
        void updateObjectData(const ThreeSampleHostOptions &options);

        /** Writes exact tightly packed RGBA8 capture bytes. */
        void writeRgbaCapture(const ThreeSampleHostOptions &options,
                              const eastl::vector<uint8_t> &rgba) const;

        /** Writes stable capture identity and byte-layout metadata. */
        void writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                  uint32_t frameIndex,
                                  uint32_t width,
                                  uint32_t height,
                                  uint64_t byteCount) const;

        /** Writes one-Scene, one-Set, one-entity structural evidence. */
        void writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                     uint32_t frameIndex) const;

        /** Writes the canonical MDD frame, point, time, and asset evidence. */
        void writeLoaderSemanticSnapshot(const ThreeSampleHostOptions &options,
                                         uint32_t frameIndex) const;

        GVM::Core::DeviceProxy device;
        MddDecodedAsset asset;
        MddEntityState entity;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
