#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "Fixtures/Phase1TextureCases/GifImageDecoder.hpp"
#include "Fixtures/Phase1TextureCases/TexturedBoxSampleData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace GVM::ThreeSamples
{
    /** Mirrors one canonical textured BoxGeometry vertex. */
    struct alignas(16) WebglInstancingDynamicHostVertex final
    {
        glm::vec4 position;
        glm::vec4 normal;
        glm::vec4 uv;
    };

    /** Mirrors one root camera transform and target time. */
    struct alignas(16) WebglInstancingDynamicHostObjectData final
    {
        glm::mat4 viewProjection;
        glm::vec4 cameraPosition;
        glm::vec4 timeAndKind;
    };

    /** Mirrors one dynamic instance transform and color. */
    struct alignas(16) WebglInstancingDynamicHostInstanceData final
    {
        glm::vec4 transformColumn0;
        glm::vec4 transformColumn1;
        glm::vec4 transformColumn2;
        glm::vec4 transformColumn3;
        glm::vec4 color;
    };

    /** Mirrors private standard-material controls. */
    struct alignas(16) WebglInstancingDynamicHostMaterialData final
    {
        glm::vec4 baseColorRoughness;
    };

    /** Connects both Scene Sets to the fixed r185 dynamic-instancing state. */
    class WebglInstancingDynamicRuntimeAdapter final
    {
    public:
        /** Creates 10,000 main instances and the eight environment entities. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Preserves the target-frame snapshot during host warm-up. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Captures RGBA8 and writes the two-root structural snapshot. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases all target-frame CPU arrays. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        /** Validates one scenario and allocates both unique Scene Sets. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglInstancingDynamicHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebglInstancingDynamicHostInstanceData> mainInstances;
        eastl::vector<WebglInstancingDynamicHostInstanceData> environmentInstances;
        RgbaImageData edgeTexture;
        eastl::vector<uint64_t> edgeTextureMipOffsets;
        bool captureWritten = false;
    };
} // namespace GVM::ThreeSamples
