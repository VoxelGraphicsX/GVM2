#pragma once

#include "Host/ThreeSampleHostOptions.hpp"
#include "WebgpuPostprocessingData.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Mirrors one ordinary Mesh transform component in generated layout. */
    struct alignas(16) WebgpuPostprocessingSobelHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::mat4 modelView;
        glm::mat4 normalModelView;
    };

    /** Mirrors the mandatory identity instance component. */
    struct alignas(16) WebgpuPostprocessingSobelHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors the shared white flat MeshPhong material component. */
    struct alignas(16) WebgpuPostprocessingSobelHostMaterialData
    {
        glm::vec4 diffuseAndShininess;
        glm::vec4 specular;
    };

    /** Mirrors room visibility and postprocess phase flags. */
    struct alignas(16) WebgpuPostprocessingSobelHostRenderFlags
    {
        uint32_t values[4]{};
    };

    /** Connects the locked Dragon Scene and invisible RoomEnvironment capture Set. */
    class WebgpuPostprocessingSobelRuntimeAdapter final
    {
    public:
        /** Builds SphereGeometry, reconstructs the random Scene, and allocates 100 entities. */
        template <class RendererImpl>
        void initialize(
            RendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options)
        {
            renderer.configureSobelEnabled(options.scenarioId != "effect-disabled");
            initializeResources(renderer, inDevice, options);
        }

        /** Keeps the precomputed target-frame entity transforms immutable. */
        void beforeFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex);

        /** Writes final RGBA8 and the unique-Set structural evidence. */
        void afterFrame(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options,
            uint32_t frameIndex,
            GVM::RHI::Texture readbackTexture,
            uint32_t width,
            uint32_t height);

        /** Releases all CPU staging data after Renderer shutdown starts. */
        void shutdown(
            GVM::Core::AbstractRendererImpl &renderer,
            const ThreeSampleHostOptions &options);

    private:
        /** Validates one locked scenario and allocates all ordinary entities. */
        void initializeResources(
            GVM::Core::AbstractRendererImpl &renderer,
            GVM::Core::DeviceProxy inDevice,
            const ThreeSampleHostOptions &options);

        /** Applies the two deterministic OrbitControls damping updates for the locked drag replay. */
        void updateOrbitCamera(
            GVM::Core::AbstractRendererImpl &renderer,
            uint32_t frameIndex);

        eastl::vector<WebgpuPostprocessingSobelVertex> vertices;
        eastl::vector<uint32_t> indices;
        eastl::vector<WebgpuPostprocessingSobelHostObjectData> objects;
        glm::mat4 dragonModel{1.0f};
        /** Stores the instanced overlay payload read through RenderEntityInstanceID. */
        eastl::vector<WebgpuPostprocessingSobelHostInstanceData> instanceData;
        /** Stores the SHA-256 identity of the accepted input replay, if any. */
        eastl::string inputReplaySha256;
        uint32_t inputReplayEventCount = 0u;
        WebgpuPostprocessingSobelHostMaterialData materialData{};
        WebgpuPostprocessingSobelHostRenderFlags renderFlags{};
        WebgpuPostprocessingSobelHostRenderFlags roomRenderFlags{};
        GVM::Core::DeviceProxy device;
        bool captureWritten = false;
    };
}
