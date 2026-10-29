#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one direct clip-space SVG stroke vertex. */
    struct alignas(16) SvgLinesHostVertex
    {
        glm::vec4 position;
        glm::vec4 color;
    };

    /** Mirrors one RenderSet object component entry. */
    struct alignas(16) SvgLinesHostObjectData
    {
        glm::vec4 geometryAndFlags;
    };

    /** Mirrors one mandatory non-instanced component entry. */
    struct alignas(16) SvgLinesHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one SVG material component entry. */
    struct alignas(16) SvgLinesHostMaterialData
    {
        glm::vec4 colorAndWidth;
    };

    /** Stores all triangle-list payloads for one SVG source line. */
    struct SvgLinesEntityData
    {
        eastl::string logicalId;
        eastl::vector<SvgLinesHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        SvgLinesHostObjectData objectData = {};
        SvgLinesHostInstanceData instanceData = {};
        SvgLinesHostMaterialData materialData = {};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
    };

    /** Connects the dedicated SVG stroke renderer to deterministic projected geometry. */
    class SvgLinesRuntimeAdapter final
    {
    public:
        /** Builds all four source lines at the requested locked frame. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Leaves capture-frame geometry immutable during host warm-up. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads and writes the requested SVG compatibility capture and evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing teardown after renderer destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates the scenario, builds projected strokes, and allocates one RenderSet. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<SvgLinesEntityData> entities;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
