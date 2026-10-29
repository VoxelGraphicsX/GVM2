#include "WebglReversedDepthRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t MeshCount = 5u;
        constexpr uint32_t LockedRandomSeed = 305419896u;
        constexpr double FrameStepSeconds = 1.0 / 60.0;
        constexpr double Pi = 3.14159265358979323846;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        static_assert(sizeof(ReversedDepthHostVertex) == 32u);
        static_assert(sizeof(ReversedDepthHostObjectData) == 144u);
        static_assert(sizeof(ReversedDepthHostInstanceData) == 16u);
        static_assert(sizeof(ReversedDepthHostMaterialData) == 16u);

        /** Creates parent directories for one explicitly requested artifact. */
        void prepareReversedDepthOutputPath(
            const std::filesystem::path &outputPath)
        {
            if (!outputPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    outputPath.parent_path());
            }
        }

        /** Validates the exact WebGL case, scenarios, extent, seed, and replay policy. */
        void validateWebglReversedDepthScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" &&
                options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" &&
                options.targetFrame == 120u;
            if (options.caseId !=
                    "webgl_reversed_depth_buffer" ||
                (!initial && !animated) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != LockedRandomSeed ||
                !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "WebGL reversed-depth requires its locked case, scenario, extent, seed, and no replay.");
            }
        }

        /** Builds the authored pair of partially overlapping colored planes. */
        void buildReversedDepthGeometry(
            eastl::vector<ReversedDepthHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr float Distance = 0.0001f;
            constexpr float Offset = 0.5f;
            const float positions[12u][3u] = {
                {-1.0f - Offset, -1.0f, Distance},
                {1.0f - Offset, -1.0f, Distance},
                {-1.0f - Offset, 1.0f, Distance},
                {1.0f - Offset, -1.0f, Distance},
                {1.0f - Offset, 1.0f, Distance},
                {-1.0f - Offset, 1.0f, Distance},
                {-1.0f + Offset, -1.0f, -Distance},
                {1.0f + Offset, -1.0f, -Distance},
                {-1.0f + Offset, 1.0f, -Distance},
                {1.0f + Offset, -1.0f, -Distance},
                {1.0f + Offset, 1.0f, -Distance},
                {-1.0f + Offset, 1.0f, -Distance}};
            vertices.clear();
            indices.clear();
            vertices.reserve(12u);
            indices.reserve(12u);
            for (uint32_t vertexIndex = 0u;
                 vertexIndex < 12u;
                 ++vertexIndex)
            {
                const bool red = vertexIndex < 6u;
                vertices.push_back({
                    .position = {
                        positions[vertexIndex][0u],
                        positions[vertexIndex][1u],
                        positions[vertexIndex][2u],
                        1.0f},
                    .color = {
                        red ? 1.0f : 0.0f,
                        red ? 0.0f : 1.0f,
                        0.0f,
                        1.0f}});
                indices.push_back(vertexIndex);
            }
        }

        /** Builds a GPU-coordinate perspective matrix for forward or reverse depth. */
        glm::mat4 makeReversedDepthProjection(bool reversed)
        {
            constexpr double NearDistance = 5.0;
            constexpr double FarDistance = 9999.0;
            constexpr double Aspect = 0.33 * 800.0 / 500.0;
            const double top =
                NearDistance * std::tan(72.0 * Pi / 360.0);
            const double height = 2.0 * top;
            const double width = Aspect * height;
            const double depth =
                FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] =
                float(2.0 * NearDistance / width);
            projection[1u][1u] =
                float(2.0 * NearDistance / height);
            projection[2u][2u] = reversed
                ? float(NearDistance / depth)
                : float(
                      -(FarDistance + NearDistance) /
                      depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = reversed
                ? float(FarDistance * NearDistance / depth)
                : float(
                      -2.0 * FarDistance * NearDistance /
                      depth);
            return projection;
        }

        /** Builds the exact shared 30-degree axis-angle rotation at one frame. */
        glm::mat4 makeReversedDepthRotation(uint32_t targetFrame)
        {
            double now = 0.0;
            for (uint32_t frame = 0u;
                 frame < targetFrame;
                 ++frame)
            {
                now += FrameStepSeconds;
            }
            const glm::dvec3 axis(
                std::sin(now),
                std::cos(now),
                0.0);
            const double halfAngle =
                (30.0 * Pi / 180.0) * 0.5;
            const double sineHalfAngle =
                std::sin(halfAngle);
            const glm::dquat quaternion(
                std::cos(halfAngle),
                axis.x * sineHalfAngle,
                axis.y * sineHalfAngle,
                0.0);
            return glm::mat4_cast(glm::quat(quaternion));
        }

        /** Builds both target-frame transforms for all five Scene entities. */
        void buildReversedDepthObjects(
            uint32_t targetFrame,
            eastl::vector<ReversedDepthHostObjectData> &objects)
        {
            const glm::mat4 rotation =
                makeReversedDepthRotation(targetFrame);
            glm::mat4 view(1.0f);
            view[3u][2u] = -12.0f;
            const glm::mat4 normalProjection =
                makeReversedDepthProjection(false);
            const glm::mat4 reversedProjection =
                makeReversedDepthProjection(true);
            objects.clear();
            objects.reserve(MeshCount);
            for (uint32_t meshIndex = 0u;
                 meshIndex < MeshCount;
                 ++meshIndex)
            {
                const double z =
                    -800.0 * double(meshIndex);
                const double scale =
                    1.0 + 50.0 * double(meshIndex);
                const double y =
                    (4.0 - 0.2 * z) *
                    (double(meshIndex) - 1.5);
                glm::mat4 model = rotation;
                model[0u] *= float(scale);
                model[1u] *= float(scale);
                model[2u] *= float(scale);
                model[3u] =
                    glm::vec4(0.0f, float(y), float(z), 1.0f);
                const glm::mat4 modelView = view * model;
                objects.push_back({
                    .normalModelViewProjection =
                        normalProjection * modelView,
                    .reversedModelViewProjection =
                        reversedProjection * modelView,
                    .normalDepthControl = {
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f},
                });
            }
            const float initialBias[MeshCount] = {
                0.000001f,
                0.000001f,
                -0.000001f,
                0.000001f,
                -0.000001f};
            const float animatedBias[MeshCount] = {
                0.000001f,
                0.000001f,
                0.000001f,
                -0.000001f,
                -0.000001f};
            for (uint32_t meshIndex = 0u;
                 meshIndex < MeshCount;
                 ++meshIndex)
            {
                objects[meshIndex].normalDepthControl.x =
                    targetFrame == 0u
                        ? initialBias[meshIndex]
                        : animatedBias[meshIndex];
                objects[meshIndex].normalDepthControl.z =
                    targetFrame == 0u &&
                            meshIndex == 1u
                        ? 1.0f
                        : 0.0f;
            }
        }

        /** Appends one typed payload to a RenderSet entity allocation. */
        void appendWebglReversedDepthPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }
    } // namespace

    void WebglReversedDepthRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateWebglReversedDepthScenario(options);
        device = inDevice;
        buildReversedDepthGeometry(vertices, indices);
        buildReversedDepthObjects(
            options.targetFrame,
            objects);

        const ReversedDepthHostInstanceData instanceData = {
            .reserved = {0.0f, 0.0f, 0.0f, 0.0f}};
        const ReversedDepthHostMaterialData materialData = {
            .colorMultiplier = {1.0f, 1.0f, 1.0f, 1.0f}};
        const auto encoder =
            renderer.createRenderSetCommandEncoder(
                SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "WebGL reversed-depth could not create its Scene RenderSet encoder.");
        }
        for (uint32_t meshIndex = 0u;
             meshIndex < MeshCount;
             ++meshIndex)
        {
            char vertexName[96] = {};
            char indexName[96] = {};
            char objectName[96] = {};
            char instanceName[96] = {};
            char materialName[96] = {};
            std::snprintf(
                vertexName,
                sizeof(vertexName),
                "WebglReversedDepthBufferVertices-%u",
                meshIndex);
            std::snprintf(
                indexName,
                sizeof(indexName),
                "WebglReversedDepthBufferIndices-%u",
                meshIndex);
            std::snprintf(
                objectName,
                sizeof(objectName),
                "WebglReversedDepthBufferObject-%u",
                meshIndex);
            std::snprintf(
                instanceName,
                sizeof(instanceName),
                "WebglReversedDepthBufferInstance-%u",
                meshIndex);
            std::snprintf(
                materialName,
                sizeof(materialName),
                "WebglReversedDepthBufferMaterial-%u",
                meshIndex);
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount =
                static_cast<uint32_t>(vertices.size());
            allocation.indicesCount =
                static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            appendWebglReversedDepthPayload(
                allocation,
                WebglReversedDepthBufferSceneRenderSetComponents::
                    vertices,
                vertexName,
                vertices.data(),
                vertices.size() *
                    sizeof(ReversedDepthHostVertex),
                1u);
            appendWebglReversedDepthPayload(
                allocation,
                WebglReversedDepthBufferSceneRenderSetComponents::
                    indices,
                indexName,
                indices.data(),
                indices.size() * sizeof(uint32_t),
                1u);
            appendWebglReversedDepthPayload(
                allocation,
                WebglReversedDepthBufferSceneRenderSetComponents::
                    objects,
                objectName,
                &objects[meshIndex],
                sizeof(ReversedDepthHostObjectData),
                1u);
            appendWebglReversedDepthPayload(
                allocation,
                WebglReversedDepthBufferSceneRenderSetComponents::
                    instances,
                instanceName,
                &instanceData,
                sizeof(instanceData),
                1u);
            appendWebglReversedDepthPayload(
                allocation,
                WebglReversedDepthBufferSceneRenderSetComponents::
                    materials,
                materialName,
                &materialData,
                sizeof(materialData),
                1u);
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(
            SceneRenderSetHandle,
            encoder);
    }

    void WebglReversedDepthRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglReversedDepthRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten ||
            frameIndex != options.targetFrame)
        {
            return;
        }
        const uint64_t byteCount =
            uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount >
            std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "WebGL reversed-depth capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(
            static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(
                readbackTexture,
                rgba.data(),
                rgba.size())
            ->submit();
        writeArtifacts(
            options,
            frameIndex,
            width,
            height,
            rgba);
        captureWritten = true;
    }

    void WebglReversedDepthRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        objects.clear();
    }

    void WebglReversedDepthRuntimeAdapter::writeArtifacts(
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        uint32_t width,
        uint32_t height,
        const eastl::vector<uint8_t> &rgba) const
    {
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareReversedDepthOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::binary |
                    std::ios::out |
                    std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write WebGL reversed-depth RGBA capture.");
            }
        }
        if (!options.captureMetadataPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureMetadataPath.c_str());
            prepareReversedDepthOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_reversed_depth_buffer\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\"\n"
                << "}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.sceneSnapshotPath.c_str());
            prepareReversedDepthOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_reversed_depth_buffer\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":5,\n"
                << "  \"entityCount\":5,\n"
                << "  \"instanceCount\":1,\n"
                << "  \"scenePassCount\":3,\n"
                << "  \"screenPassCount\":3,\n"
                << "  \"drawCommandCount\":3,\n"
                << "  \"renderSetType\":\"WebglReversedDepthBufferSceneRenderSet\",\n"
                << "  \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
                << "  \"sceneRoots\":[{\n"
                << "    \"id\":\"scene\",\n"
                << "    \"renderSetCount\":1,\n"
                << "    \"renderSetId\":\"scene-set\",\n"
                << "    \"renderSetType\":\"WebglReversedDepthBufferSceneRenderSet\",\n"
                << "    \"renderableObjectCount\":5,\n"
                << "    \"entityCount\":5,\n"
                << "    \"entities\":["
                << "{\"entityId\":0,\"logicalRenderableId\":\"plane-0\",\"instanceCount\":1},"
                << "{\"entityId\":1,\"logicalRenderableId\":\"plane-1\",\"instanceCount\":1},"
                << "{\"entityId\":2,\"logicalRenderableId\":\"plane-2\",\"instanceCount\":1},"
                << "{\"entityId\":3,\"logicalRenderableId\":\"plane-3\",\"instanceCount\":1},"
                << "{\"entityId\":4,\"logicalRenderableId\":\"plane-4\",\"instanceCount\":1}],\n"
                << "    \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],\n"
                << "    \"drawCommandCount\":3,\n"
                << "    \"directDrawFallback\":false,\n"
                << "    \"scenePasses\":["
                << "{\"name\":\"normal-depth\",\"renderClass\":\"WebglReversedDepthBufferNormalPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                << "{\"name\":\"logarithmic-depth\",\"renderClass\":\"WebglReversedDepthBufferLogarithmicPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
                << "{\"name\":\"reversed-depth\",\"renderClass\":\"WebglReversedDepthBufferReversedPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
                << "  }],\n"
                << "  \"scenePassSequence\":["
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"normal-depth\",\"entityOrdinal\":0},"
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"logarithmic-depth\",\"entityOrdinal\":0},"
                << "{\"sceneRoot\":\"scene\",\"scenePass\":\"reversed-depth\",\"entityOrdinal\":0}],\n"
                << "  \"usesRenderEntityID\":true,\n"
                << "  \"usesRenderEntityInstanceID\":false,\n"
                << "  \"directDrawFallback\":false\n"
                << "}\n";
        }
        if (!options.semanticSnapshotPath.empty())
        {
            const std::filesystem::path outputPath(
                options.semanticSnapshotPath.c_str());
            prepareReversedDepthOutputPath(outputPath);
            std::ofstream output(
                outputPath,
                std::ios::out |
                    std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"caseId\":\"webgl_reversed_depth_buffer\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"depthModes\":[\"normal\",\"logarithmic\",\"reversed\"],\n"
                << "  \"columnWidth\":264,\n"
                << "  \"trailingGapWidth\":8,\n"
                << "  \"meshCount\":5\n"
                << "}\n";
        }
    }
} // namespace GVM::ThreeSamples
