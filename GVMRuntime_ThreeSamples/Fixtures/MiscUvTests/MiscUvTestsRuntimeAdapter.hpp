#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "MiscUvTestsVertexData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace GVM::ThreeSamples
{
    /** Stores both Arial advances used by centered UV face and edge labels. */
    struct MiscUvGlyphAdvance
    {
        float small = 0.0f;
        float large = 0.0f;
    };

    /** Builds one exact r185 UV utility section and connects it to the dedicated DSL compositor. */
    class MiscUvTestsRuntimeAdapter final
    {
    public:
        /** Selects one Manifest section, builds contour/text geometry, and uploads its glyph atlas. */
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
                atlasPixels,
                AtlasWidth,
                AtlasHeight,
                canvasTop);
            frameArmer = [&renderer](bool shouldDraw) {
                renderer.updateFrame(shouldDraw);
            };
        }

        /** Arms only the requested immutable target frame. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Captures the RGBA8 utility page and writes structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU-owned generated geometry and atlas bytes. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        static constexpr uint32_t GlyphCount = 13u;
        static constexpr uint32_t AtlasCellExtent = 32u;
        static constexpr uint32_t AtlasPhaseCount = 32u;
        static constexpr uint32_t AtlasPhaseColumns = 8u;
        static constexpr uint32_t AtlasWidth =
            GlyphCount * AtlasCellExtent * AtlasPhaseColumns;
        static constexpr uint32_t AtlasHeight =
            AtlasCellExtent * 2u * AtlasPhaseCount *
            (AtlasPhaseCount / AtlasPhaseColumns);

        /** Validates one Manifest scenario and builds all immutable CPU inputs. */
        void initializeResources(
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<MiscUvTestsVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<uint8_t> atlasPixels;
        eastl::array<MiscUvGlyphAdvance, GlyphCount> glyphAdvances = {};
        eastl::function<void(bool)> frameArmer;
        uint32_t faceCount = 0u;
        uint32_t sectionIndex = 0u;
        float canvasTop = 0.0f;
        eastl::string inputReplaySha256;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
