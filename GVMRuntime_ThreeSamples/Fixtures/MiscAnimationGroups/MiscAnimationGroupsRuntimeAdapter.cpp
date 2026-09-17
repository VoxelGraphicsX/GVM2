#include "MiscAnimationGroupsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;
        constexpr uint32_t BoxCount = 25u;

        static_assert(sizeof(MiscAnimationGroupsHostVertex) == 16u);
        static_assert(sizeof(MiscAnimationGroupsHostObjectData) == 128u);
        static_assert(sizeof(MiscAnimationGroupsHostInstanceData) == 16u);
        static_assert(sizeof(MiscAnimationGroupsHostMaterialData) == 16u);
        static_assert(sizeof(MiscAnimationGroupsHostRenderFlagData) == 16u);

        /** Validates the two frozen group-animation scenarios and host extent. */
        void validateMiscAnimationGroupsScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 45u;
            if (options.caseId != "misc_animation_groups" ||
                (!initial && !animated) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "Animation groups require the locked case, scenarios, extent, seed, and no input replay.");
            }
        }

        /** Builds the generated-backend perspective projection for the r185 camera. */
        glm::mat4 makeMiscAnimationGroupsProjection()
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 1000.0;
            const double top =
                NearDistance * std::tan(40.0 * Pi / 360.0);
            const double height = top * 2.0;
            const double width = height * (800.0 / 500.0);
            const double depth = FarDistance - NearDistance;
            glm::mat4 result(0.0f);
            result[0u][0u] = float(2.0 * NearDistance / width);
            result[1u][1u] = float(-2.0 * NearDistance / height);
            result[2u][2u] = float(-FarDistance / depth);
            result[2u][3u] = -1.0f;
            result[3u][2u] = float(-FarDistance * NearDistance / depth);
            return result;
        }

        /** Appends the shared five-unit indexed BoxGeometry payload. */
        void buildMiscAnimationGroupsBox(
            eastl::vector<MiscAnimationGroupsHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            constexpr float HalfExtent = 2.5f;
            const glm::vec4 positions[8u] = {
                {-HalfExtent, -HalfExtent, HalfExtent, 1.0f},
                {HalfExtent, -HalfExtent, HalfExtent, 1.0f},
                {HalfExtent, HalfExtent, HalfExtent, 1.0f},
                {-HalfExtent, HalfExtent, HalfExtent, 1.0f},
                {-HalfExtent, -HalfExtent, -HalfExtent, 1.0f},
                {HalfExtent, -HalfExtent, -HalfExtent, 1.0f},
                {HalfExtent, HalfExtent, -HalfExtent, 1.0f},
                {-HalfExtent, HalfExtent, -HalfExtent, 1.0f},
            };
            vertices.reserve(8u);
            for (const glm::vec4 &position : positions)
            {
                vertices.push_back({.position = position});
            }
            const uint32_t boxIndices[36u] = {
                0u, 1u, 2u, 0u, 2u, 3u,
                5u, 4u, 7u, 5u, 7u, 6u,
                4u, 0u, 3u, 4u, 3u, 7u,
                1u, 5u, 6u, 1u, 6u, 2u,
                3u, 2u, 6u, 3u, 6u, 7u,
                4u, 5u, 1u, 4u, 1u, 0u,
            };
            indices.assign(boxIndices, boxIndices + 36u);
        }

        /** Evaluates the exact quaternion, discrete color, and opacity tracks. */
        glm::mat4 makeMiscAnimationGroupsLocalModel(
            uint32_t targetFrame,
            glm::vec4 &colorAndOpacity)
        {
            const float time = float(targetFrame) / 60.0f;
            const float firstKeyAlpha = glm::clamp(time, 0.0f, 1.0f);
            const float rotation = firstKeyAlpha * float(Pi);
            const float opacity = 1.0f - firstKeyAlpha;
            colorAndOpacity = glm::vec4(1.0f, 0.0f, 0.0f, opacity);
            return glm::rotate(
                glm::mat4(1.0f), rotation, glm::vec3(1.0f, 0.0f, 0.0f));
        }

        /** Creates parent directories for one requested evidence artifact. */
        void prepareMiscAnimationGroupsOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeMiscAnimationGroupsText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareMiscAnimationGroupsOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write animation-groups evidence artifact.");
            }
        }

        /** Appends one typed payload to a RenderSet entity allocation. */
        void appendMiscAnimationGroupsPayload(
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

        /** Allocates one ordinary box entity in the unique Scene RenderSet. */
        void allocateMiscAnimationGroupsEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const eastl::vector<MiscAnimationGroupsHostVertex> &vertices,
            const eastl::vector<uint32_t> &indices,
            const MiscAnimationGroupsHostObjectData &objectData,
            const MiscAnimationGroupsHostMaterialData &materialData,
            uint32_t ordinal)
        {
            const MiscAnimationGroupsHostInstanceData instanceData = {
                .reserved = glm::vec4(0.0f)};
            const MiscAnimationGroupsHostRenderFlagData renderFlag = {
                .phase = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)};
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            const std::string prefix =
                "MiscAnimationGroupsBox" + std::to_string(ordinal);
            const std::string vertexName = prefix + "Vertices";
            const std::string indexName = prefix + "Indices";
            const std::string objectName = prefix + "Object";
            const std::string instanceName = prefix + "Instance";
            const std::string materialName = prefix + "Material";
            const std::string flagName = prefix + "RenderFlag";
            appendMiscAnimationGroupsPayload(
                allocation, MiscAnimationGroupsSceneRenderSetComponents::vertices,
                vertexName.c_str(), vertices.data(),
                vertices.size() * sizeof(vertices[0u]), 1u);
            appendMiscAnimationGroupsPayload(
                allocation, MiscAnimationGroupsSceneRenderSetComponents::indices,
                indexName.c_str(), indices.data(),
                indices.size() * sizeof(indices[0u]), 1u);
            appendMiscAnimationGroupsPayload(
                allocation, MiscAnimationGroupsSceneRenderSetComponents::objects,
                objectName.c_str(), &objectData, sizeof(objectData), 1u);
            appendMiscAnimationGroupsPayload(
                allocation, MiscAnimationGroupsSceneRenderSetComponents::instances,
                instanceName.c_str(), &instanceData, sizeof(instanceData), 1u);
            appendMiscAnimationGroupsPayload(
                allocation, MiscAnimationGroupsSceneRenderSetComponents::materials,
                materialName.c_str(), &materialData, sizeof(materialData), 1u);
            appendMiscAnimationGroupsPayload(
                allocation, MiscAnimationGroupsSceneRenderSetComponents::renderFlags,
                flagName.c_str(), &renderFlag, sizeof(renderFlag), 1u);
            encoder.allocEntity(allocation);
        }
    }

    void MiscAnimationGroupsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateMiscAnimationGroupsScenario(options);
        device = inDevice;
        buildMiscAnimationGroupsBox(boxVertices, boxIndices);
        const glm::mat4 projection = makeMiscAnimationGroupsProjection();
        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(50.0f, 50.0f, 100.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec4 colorAndOpacity(1.0f);
        const glm::mat4 localModel =
            makeMiscAnimationGroupsLocalModel(
                options.targetFrame, colorAndOpacity);
        const MiscAnimationGroupsHostMaterialData materialData = {
            .colorAndOpacity = colorAndOpacity,
        };
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create animation-groups RenderSet encoder.");
        }
        uint32_t ordinal = 0u;
        for (uint32_t i = 0u; i < 5u; ++i)
        {
            for (uint32_t j = 0u; j < 5u; ++j)
            {
                glm::mat4 model(1.0f);
                model = glm::translate(
                    model,
                    glm::vec3(
                        32.0f - 16.0f * float(i),
                        0.0f,
                        32.0f - 16.0f * float(j)));
                model *= localModel;
                const MiscAnimationGroupsHostObjectData objectData = {
                    .modelView = view * model,
                    .projection = projection,
                };
                allocateMiscAnimationGroupsEntity(
                    *encoder, boxVertices, boxIndices,
                    objectData, materialData, ordinal++);
            }
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void MiscAnimationGroupsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void MiscAnimationGroupsRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * height * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Animation-groups capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareMiscAnimationGroupsOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write animation-groups RGBA capture.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"source\":\"gvm-three-r185\",\n"
            << "  \"caseId\":\"misc_animation_groups\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
            << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"randomSeed\":" << options.randomSeed << ",\n"
            << "  \"width\":" << width << ",\n"
            << "  \"height\":" << height << ",\n"
            << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
            << "  \"byteCount\":" << byteCount << ",\n"
            << "  \"format\":\"rgba8unorm\"\n}\n";
        writeMiscAnimationGroupsText(
            options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"misc_animation_groups\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"implementationLevel\":\"semantic-complete\",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"renderSetPolicy\":\"required\",\n"
            << "  \"sceneRenderSetCount\":1,\n"
            << "  \"renderableObjectCount\":25,\n"
            << "  \"entityCount\":25,\n"
            << "  \"instanceCount\":25,\n"
            << "  \"vertexCount\":200,\n"
            << "  \"indexCount\":900,\n"
            << "  \"scenePassCount\":1,\n"
            << "  \"screenPassCount\":0,\n"
            << "  \"drawCommandCount\":1,\n"
            << "  \"renderSetType\":\"MiscAnimationGroupsSceneRenderSet\",\n"
            << "  \"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"transparent-material-phase-and-order\"}],\n"
            << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":\"MiscAnimationGroupsSceneRenderSet\","
            << "\"renderableObjectCount\":25,\"entityCount\":25,\"instanceCounts\":["
            << "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],"
            << "\"entities\":[";
        for (uint32_t ordinal = 0u; ordinal < BoxCount; ++ordinal)
        {
            if (ordinal != 0u) snapshot << ',';
            snapshot
                << "{\"entityId\":" << ordinal
                << ",\"logicalRenderableId\":\"animation-group-box-"
                << ordinal << "\",\"instanceCount\":1}";
        }
        snapshot
            << "],\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"transparent-material-phase-and-order\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"main-transparent-boxes\","
            << "\"renderClass\":\"MiscAnimationGroupsMainPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\","
            << "\"invocationCount\":1,\"drawCommandCount\":1,"
            << "\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
            << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"main-transparent-boxes\"}],\n"
            << "  \"usesRenderEntityID\":true,\n"
            << "  \"usesRenderEntityInstanceID\":true,\n"
            << "  \"directDrawFallback\":false\n}\n";
        writeMiscAnimationGroupsText(options.sceneSnapshotPath, snapshot.str());
        std::ostringstream semantic;
        semantic
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"misc_animation_groups\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"animationTimeSeconds\":" << double(frameIndex) / 60.0 << ",\n"
            << "  \"animationGroupObjectCount\":" << BoxCount << ",\n"
            << "  \"trackCount\":3\n}\n";
        writeMiscAnimationGroupsText(
            options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void MiscAnimationGroupsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        boxVertices.clear();
        boxIndices.clear();
    }
}
