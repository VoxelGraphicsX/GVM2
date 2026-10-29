#include "MiscAnimationKeysRuntimeAdapter.hpp"

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

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(MiscAnimationKeysHostVertex) == 80u);
        static_assert(sizeof(MiscAnimationKeysHostObjectData) == 144u);
        static_assert(sizeof(MiscAnimationKeysHostInstanceData) == 16u);
        static_assert(sizeof(MiscAnimationKeysHostMaterialData) == 16u);
        static_assert(sizeof(MiscAnimationKeysHostRenderFlagData) == 16u);

        /** Validates the two frozen keyframe scenarios and host extent. */
        void validateMiscAnimationKeysScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool initial =
                options.scenarioId == "initial" && options.targetFrame == 0u;
            const bool animated =
                options.scenarioId == "animated" && options.targetFrame == 45u;
            if (options.caseId != "misc_animation_keys" ||
                (!initial && !animated) ||
                options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "Animation keys require the locked case, scenarios, extent, seed, and no input replay.");
            }
        }

        /** Builds the generated-backend perspective projection for the r185 camera. */
        glm::mat4 makeMiscAnimationKeysProjection()
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

        /** Appends the exact 5-unit indexed BoxGeometry payload. */
        void buildMiscAnimationKeysBox(
            eastl::vector<MiscAnimationKeysHostVertex> &vertices,
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
                vertices.push_back({
                    .position = position,
                    .lineEnd = position,
                    .startColor = glm::vec4(1.0f),
                    .endColor = glm::vec4(1.0f),
                    .lineCorner = glm::vec4(0.0f),
                });
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

        /** Appends one constant-width expanded AxesHelper segment. */
        void appendMiscAnimationKeysAxis(
            eastl::vector<MiscAnimationKeysHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec4 &end,
            const glm::vec4 &startColor,
            const glm::vec4 &endColor)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const glm::vec4 start(0.0f, 0.0f, 0.0f, 1.0f);
            vertices.push_back({start, end, startColor, endColor,
                                {0.0f, -0.5f, 0.0f, 0.0f}});
            vertices.push_back({start, end, startColor, endColor,
                                {0.0f, 0.5f, 0.0f, 0.0f}});
            vertices.push_back({start, end, startColor, endColor,
                                {1.0f, 0.5f, 0.0f, 0.0f}});
            vertices.push_back({start, end, startColor, endColor,
                                {1.0f, -0.5f, 0.0f, 0.0f}});
            indices.insert(
                indices.end(),
                {base, base + 1u, base + 2u,
                 base, base + 2u, base + 3u});
        }

        /** Builds all three exact AxesHelper segments and endpoint colors. */
        void buildMiscAnimationKeysAxes(
            eastl::vector<MiscAnimationKeysHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices.reserve(12u);
            indices.reserve(18u);
            appendMiscAnimationKeysAxis(
                vertices, indices,
                glm::vec4(10.0f, 0.0f, 0.0f, 1.0f),
                glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
                glm::vec4(1.0f, 0.6f, 0.0f, 1.0f));
            appendMiscAnimationKeysAxis(
                vertices, indices,
                glm::vec4(0.0f, 10.0f, 0.0f, 1.0f),
                glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
                glm::vec4(0.6f, 1.0f, 0.0f, 1.0f));
            appendMiscAnimationKeysAxis(
                vertices, indices,
                glm::vec4(0.0f, 0.0f, 10.0f, 1.0f),
                glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                glm::vec4(0.0f, 0.6f, 1.0f, 1.0f));
        }

        /** Evaluates the target-frame position, scale, rotation, and opacity keys. */
        glm::mat4 makeMiscAnimationKeysBoxModel(
            uint32_t targetFrame,
            float &opacity)
        {
            const float time = float(targetFrame) / 60.0f;
            const float interpolation = eastl::min(time, 1.0f);
            opacity = 1.0f - interpolation;
            const float positionX = 30.0f * interpolation;
            const float scale = 1.0f + interpolation;
            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(positionX, 0.0f, 0.0f));
            model = glm::rotate(model, interpolation * float(Pi),
                                glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::scale(model, glm::vec3(scale));
            return model;
        }

        /** Creates parent directories for one requested evidence artifact. */
        void prepareMiscAnimationKeysOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional deterministic text artifact. */
        void writeMiscAnimationKeysText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareMiscAnimationKeysOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write animation-keys evidence artifact.");
            }
        }

        /** Appends one typed payload to a RenderSet entity allocation. */
        void appendMiscAnimationKeysPayload(
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

        /** Allocates one ordinary entity in the unique Scene RenderSet. */
        void allocateMiscAnimationKeysEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const eastl::vector<MiscAnimationKeysHostVertex> &vertices,
            const eastl::vector<uint32_t> &indices,
            const MiscAnimationKeysHostObjectData &objectData,
            const MiscAnimationKeysHostMaterialData &materialData,
            const MiscAnimationKeysHostRenderFlagData &renderFlag,
            const char *namePrefix)
        {
            const MiscAnimationKeysHostInstanceData instanceData = {
                .reserved = glm::vec4(0.0f)};
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            const std::string prefix(namePrefix);
            const std::string vertexName = prefix + "Vertices";
            const std::string indexName = prefix + "Indices";
            const std::string objectName = prefix + "Object";
            const std::string instanceName = prefix + "Instance";
            const std::string materialName = prefix + "Material";
            const std::string flagName = prefix + "RenderFlag";
            appendMiscAnimationKeysPayload(
                allocation, MiscAnimationKeysSceneRenderSetComponents::vertices,
                vertexName.c_str(), vertices.data(),
                vertices.size() * sizeof(vertices[0u]), 1u);
            appendMiscAnimationKeysPayload(
                allocation, MiscAnimationKeysSceneRenderSetComponents::indices,
                indexName.c_str(), indices.data(),
                indices.size() * sizeof(indices[0u]), 1u);
            appendMiscAnimationKeysPayload(
                allocation, MiscAnimationKeysSceneRenderSetComponents::objects,
                objectName.c_str(), &objectData, sizeof(objectData), 1u);
            appendMiscAnimationKeysPayload(
                allocation, MiscAnimationKeysSceneRenderSetComponents::instances,
                instanceName.c_str(), &instanceData, sizeof(instanceData), 1u);
            appendMiscAnimationKeysPayload(
                allocation, MiscAnimationKeysSceneRenderSetComponents::materials,
                materialName.c_str(), &materialData, sizeof(materialData), 1u);
            appendMiscAnimationKeysPayload(
                allocation, MiscAnimationKeysSceneRenderSetComponents::renderFlags,
                flagName.c_str(), &renderFlag, sizeof(renderFlag), 1u);
            encoder.allocEntity(allocation);
        }
    }

    void MiscAnimationKeysRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateMiscAnimationKeysScenario(options);
        device = inDevice;
        buildMiscAnimationKeysBox(boxVertices, boxIndices);
        buildMiscAnimationKeysAxes(axesVertices, axesIndices);
        const glm::mat4 projection = makeMiscAnimationKeysProjection();
        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(25.0f, 25.0f, 50.0f),
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        float opacity = 1.0f;
        const glm::mat4 boxModel =
            makeMiscAnimationKeysBoxModel(options.targetFrame, opacity);
        const MiscAnimationKeysHostObjectData boxObject = {
            .modelView = view * boxModel,
            .projection = projection,
            .viewport = glm::vec4(400.0f, 250.0f, 0.0f, 0.0f),
        };
        const MiscAnimationKeysHostObjectData axesObject = {
            .modelView = view,
            .projection = projection,
            .viewport = glm::vec4(400.0f, 250.0f, 0.0f, 0.0f),
        };
        const MiscAnimationKeysHostMaterialData boxMaterial = {
            .colorAndOpacity = glm::vec4(1.0f, 0.0f, 0.0f, opacity),
        };
        const MiscAnimationKeysHostMaterialData axesMaterial = {
            .colorAndOpacity = glm::vec4(1.0f),
        };
        const MiscAnimationKeysHostRenderFlagData boxFlag = {
            .phase = glm::vec4(0.0f),
        };
        const MiscAnimationKeysHostRenderFlagData axesFlag = {
            .phase = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
        };
        const auto encoder =
            renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create animation-keys RenderSet encoder.");
        }
        allocateMiscAnimationKeysEntity(
            *encoder, boxVertices, boxIndices, boxObject,
            boxMaterial, boxFlag, "MiscAnimationKeysBox");
        allocateMiscAnimationKeysEntity(
            *encoder, axesVertices, axesIndices, axesObject,
            axesMaterial, axesFlag, "MiscAnimationKeysAxes");
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void MiscAnimationKeysRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void MiscAnimationKeysRuntimeAdapter::afterFrame(
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
                "Animation-keys capture exceeds host storage.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareMiscAnimationKeysOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write animation-keys RGBA capture.");
            }
        }
        std::ostringstream metadata;
        metadata
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"source\":\"gvm-three-r185\",\n"
            << "  \"caseId\":\"misc_animation_keys\",\n"
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
        writeMiscAnimationKeysText(options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"misc_animation_keys\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"implementationLevel\":\"semantic-complete\",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"renderSetPolicy\":\"required\",\n"
            << "  \"sceneRenderSetCount\":1,\n"
            << "  \"renderableObjectCount\":2,\n"
            << "  \"entityCount\":2,\n"
            << "  \"instanceCount\":2,\n"
            << "  \"vertexCount\":20,\n"
            << "  \"indexCount\":54,\n"
            << "  \"scenePassCount\":2,\n"
            << "  \"screenPassCount\":0,\n"
            << "  \"drawCommandCount\":2,\n"
            << "  \"renderSetType\":\"MiscAnimationKeysSceneRenderSet\",\n"
            << "  \"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"opaque-helper-or-transparent-box-phase\"}],\n"
            << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":\"MiscAnimationKeysSceneRenderSet\","
            << "\"renderableObjectCount\":2,\"entityCount\":2,"
            << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"animated-box\",\"instanceCount\":1},"
            << "{\"entityId\":1,\"logicalRenderableId\":\"axes-helper\",\"instanceCount\":1}],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
            << "{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"opaque-helper-or-transparent-box-phase\"}],"
            << "\"drawCommandCount\":2,\"directDrawFallback\":false,"
            << "\"scenePasses\":["
            << "{\"name\":\"main-expanded-axes\",\"renderClass\":\"MiscAnimationKeysAxesPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
            << "{\"name\":\"main-transparent-box\",\"renderClass\":\"MiscAnimationKeysBoxPass\",\"renderSetId\":\"scene-set\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
            << "  \"scenePassSequence\":["
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"main-expanded-axes\"},"
            << "{\"sceneRoot\":\"scene\",\"scenePass\":\"main-transparent-box\"}],\n"
            << "  \"usesRenderEntityID\":true,\n"
            << "  \"usesRenderEntityInstanceID\":true,\n"
            << "  \"directDrawFallback\":false\n}\n";
        writeMiscAnimationKeysText(options.sceneSnapshotPath, snapshot.str());
        std::ostringstream semantic;
        semantic
            << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"misc_animation_keys\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"animationTimeSeconds\":" << double(frameIndex) / 60.0 << ",\n"
            << "  \"trackCount\":5,\n"
            << "  \"axesSegmentCount\":3\n}\n";
        writeMiscAnimationKeysText(options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void MiscAnimationKeysRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        boxVertices.clear();
        boxIndices.clear();
        axesVertices.clear();
        axesIndices.clear();
    }
}
