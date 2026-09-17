#pragma once

#include "TexturedBoxSampleData.hpp"
#include "ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Connects the dedicated TIFF DSL renderer to the three locked r185 texture assets. */
    class WebglLoaderTextureTiffRuntimeAdapter final
    {
    public:
        /** Decodes all TIFF variants and allocates the three Scene RenderSet entities. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
        }

        /** Leaves the immutable loader scene unchanged before each deterministic frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex);

        /** Reads the target frame and writes image, scene, and loader evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options, uint32_t frameIndex, GVM::RHI::Texture readbackTexture, uint32_t width, uint32_t height);

        /** Performs non-throwing sample teardown after renderer destruction. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer, const ThreeSampleHostOptions &options);

    private:
        /** Validates the locked scenario, decodes assets, and allocates all entities. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer, GVM::Core::DeviceProxy inDevice, const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<TexturedBoxHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::array<eastl::vector<uint8_t>, 3u> textureBytes;
        eastl::array<eastl::vector<uint64_t>, 3u> mipOffsets;
        eastl::array<uint32_t, 3u> textureWidths = {};
        eastl::array<uint32_t, 3u> textureHeights = {};
        eastl::array<TexturedBoxHostObjectData, 3u> objectData = {};
        eastl::array<TexturedBoxHostInstanceData, 3u> instanceData = {};
        eastl::array<TexturedBoxHostMaterialData, 3u> materialData = {};
        eastl::array<GVM::Core::RenderEntityIndex, 3u> entityIndices = {
            UINT32_MAX, UINT32_MAX, UINT32_MAX};
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
