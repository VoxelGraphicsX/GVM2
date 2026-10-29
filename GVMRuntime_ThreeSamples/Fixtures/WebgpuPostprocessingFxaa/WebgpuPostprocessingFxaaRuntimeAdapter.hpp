#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors the FXAA Scene RenderSet vertex ABI. */
    struct alignas(16) WebgpuPostprocessingFxaaHostVertex
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 color;
        glm::vec4 uvAndCorner;
        glm::vec4 morphPosition;
    };

    /** Mirrors the FXAA Scene RenderSet object component. */
    struct alignas(16) WebgpuPostprocessingFxaaHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::vec4 parameters;
    };

    /** Mirrors the FXAA Scene RenderSet instance component. */
    struct alignas(16) WebgpuPostprocessingFxaaHostInstanceData
    {
        glm::vec4 transformColumn0;
        glm::vec4 transformColumn1;
        glm::vec4 transformColumn2;
        glm::vec4 transformColumn3;
        glm::vec4 color;
    };

    /** Mirrors the FXAA Scene RenderSet material component. */
    struct alignas(16) WebgpuPostprocessingFxaaHostMaterialData
    {
        glm::vec4 baseColor;
        glm::vec4 emissiveAndOpacity;
        glm::vec4 modeAndParameters;
    };

    /** Stores the allocated tetrahedron entity and all component payloads. */
    struct WebgpuPostprocessingFxaaEntityState
    {
        eastl::string logicalId;
        eastl::vector<WebgpuPostprocessingFxaaHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebgpuPostprocessingFxaaHostObjectData objectData = {};
        eastl::vector<WebgpuPostprocessingFxaaHostInstanceData> instances;
        WebgpuPostprocessingFxaaHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Connects the FXAA example to its single Scene-owned RenderSet. */
    class WebgpuPostprocessingFxaaRuntimeAdapter final
    {
    public:
        /** Configures the private DSL mode and allocates every entity in the unique Scene RenderSet. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureCase(options.scenarioId == "disabled" ? 11.0f : 10.0f,
                                   float(options.targetFrame) / 60.0f,
                                   options.inputReplayPath.empty() ? 0.0f : 1.0f,
                                   options.inputReplayPath.empty() ? 0.0f : -1.0f);
        }

        /** Keeps deterministic frame state in the DSL uniform and existing RenderSet components. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the DSL output and records structural evidence for the selected example. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Performs non-throwing host teardown after generated renderer destruction begins. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Resolves case semantics and allocates the single Scene RenderSet. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Allocates one entity through current RenderSet component semantics. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const WebgpuPostprocessingFxaaEntityState &entity) const;

        /** Writes tightly packed RGBA8, metadata, and one-Set structural evidence. */
        void writeArtifacts(const ThreeSampleHostOptions &options,
                            uint32_t frameIndex,
                            uint32_t width,
                            uint32_t height,
                            const eastl::vector<uint8_t> &rgba) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebgpuPostprocessingFxaaEntityState> entities;
        eastl::string caseId;
        eastl::string inputReplaySha256;
        uint32_t inputReplayEventCount = 0u;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
