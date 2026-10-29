#pragma once

#include "Host/ThreeSampleHostOptions.hpp"

#include <GVMCore/GVMCore.Public.hpp>
#include <GVMCore/Private/GDeviceProxy.hpp>

#include <EASTL/vector.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace GVM::ThreeSamples
{
    /** Mirrors one expanded BVH/Grid line vertex. */
    struct alignas(16) WebglLoaderBvhHostVertex
    {
        glm::vec4 segmentStart;
        glm::vec4 segmentEnd;
        glm::vec4 endpointSide;
        glm::vec4 color;
    };

    /** Mirrors the view/projection component consumed by the line DSL. */
    struct alignas(16) WebglLoaderBvhHostObjectData
    {
        glm::mat4 modelViewProjection;
        glm::vec4 viewportAndReserved;
    };

    /** Mirrors one mandatory non-instanced instance record. */
    struct alignas(16) WebglLoaderBvhHostInstanceData
    {
        glm::vec4 reserved;
    };

    /** Mirrors one entity phase/visibility record. */
    struct alignas(16) WebglLoaderBvhHostMaterialData
    {
        glm::vec4 phaseAndFlags;
    };

    /** Stores one line entity before upload into the unique RenderSet. */
    struct WebglLoaderBvhEntityData
    {
        eastl::vector<WebglLoaderBvhHostVertex> vertices;
        eastl::vector<uint32_t> indices;
        WebglLoaderBvhHostObjectData objectData{};
        WebglLoaderBvhHostInstanceData instanceData{};
        WebglLoaderBvhHostMaterialData materialData{};
        GVM::Core::RenderEntityIndex entityIndex = UINT32_MAX;
        const char *logicalId = nullptr;
    };

    /** Parses pirouette.bvh, evaluates CPU poses, and updates two line entities. */
    class WebglLoaderBvhRuntimeAdapter final
    {
    public:
        /** Loads the locked BVH asset, creates line quads, and configures output. */
        template <class RendererImpl>
        void initialize(RendererImpl &renderer,
                        GVM::Core::DeviceProxy inDevice,
                        const ThreeSampleHostOptions &options)
        {
            initializeResources(renderer, inDevice, options);
            renderer.configureOutput(options.width, options.height);
        }

        /** Applies the fixed-step BVH pose and camera replay before a frame. */
        void beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                         const ThreeSampleHostOptions &options,
                         uint32_t frameIndex);

        /** Reads the output and writes loader/RenderSet evidence. */
        void afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                        const ThreeSampleHostOptions &options,
                        uint32_t frameIndex,
                        GVM::RHI::Texture readbackTexture,
                        uint32_t width,
                        uint32_t height);

        /** Releases parsed hierarchy and staging data. */
        void shutdown(GVM::Core::AbstractRendererImpl &renderer,
                      const ThreeSampleHostOptions &options);

    private:
        struct BvhNode
        {
            std::string name;
            int32_t parent = -1;
            glm::vec3 offset = glm::vec3(0.0f);
            std::vector<std::string> channels;
            std::vector<int32_t> children;
            uint32_t channelStart = 0u;
            bool endSite = false;
        };

        /** Validates the manifest scenario and finds the pinned pirouette file. */
        void initializeResources(GVM::Core::AbstractRendererImpl &renderer,
                                 GVM::Core::DeviceProxy inDevice,
                                 const ThreeSampleHostOptions &options);

        /** Parses one complete BVH hierarchy and all motion samples. */
        void parseBvh(const std::string &path);

        /** Parses a hierarchy node from the token stream. */
        int32_t parseNode(size_t &tokenIndex, int32_t parent);

        /** Evaluates world-space bone positions for one 120-Hz source frame. */
        void evaluatePose(uint32_t frameIndex,
                          std::vector<glm::vec3> &worldPositions) const;

        /** Rebuilds the skeleton line-quads from the selected pose. */
        void updateSkeletonVertices(uint32_t frameIndex);

        /** Builds the static 400-by-400 GridHelper line entity. */
        void buildGrid(WebglLoaderBvhEntityData &entity) const;

        /** Adds one triangle-list line quad with endpoint colors. */
        void appendSegment(WebglLoaderBvhEntityData &entity,
                           const glm::vec3 &start,
                           const glm::vec3 &end,
                           const glm::vec4 &startColor,
                           const glm::vec4 &endColor,
                           float width,
                           float rasterBiasY = 0.0f) const;

        /** Uploads one entity through existing RenderSet allocation semantics. */
        GVM::Core::RenderEntityIndex allocateEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            WebglLoaderBvhEntityData &entity);

        /** Recomputes the camera projection and view for one fixed frame. */
        void updateObjectData(uint32_t width, uint32_t height, uint32_t frameIndex);

        /** Creates parent directories for an optional capture file. */
        void prepareOutputPath(const eastl::string &path) const;

        GVM::Core::DeviceProxy device;
        eastl::vector<WebglLoaderBvhEntityData> entities;
        std::vector<BvhNode> nodes;
        std::vector<std::string> parserTokens;
        std::vector<std::vector<float>> motionFrames;
        std::vector<uint32_t> motionChannelStarts;
        uint32_t totalChannelCount = 0u;
        float motionFrameTime = 1.0f / 120.0f;
        glm::mat4 viewProjection = glm::mat4(1.0f);
        uint32_t captureWidth = 800u;
        uint32_t captureHeight = 500u;
        // The locked static captures need a deterministic tie-break for the
        // center grid line; the camera replay must retain Three.js's native
        // half-pixel placement.
        bool centerGridRasterBias = false;
        bool captureWritten = false;
    };
}
