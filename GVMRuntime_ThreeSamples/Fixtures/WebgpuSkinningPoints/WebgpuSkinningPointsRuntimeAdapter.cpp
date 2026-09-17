#include "WebgpuSkinningPointsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

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
        constexpr uint32_t PointCount = 16340u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;

        /** Converts one sRGB channel to linear light. */
        float skinningPointsSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Composes one immutable Michelle node transform. */
        glm::mat4 composeSkinningPointsNode(const MichelleGlbNode &node)
        {
            return glm::translate(glm::mat4(1.0f), node.translation) *
                glm::toMat4(node.rotation) *
                glm::scale(glm::mat4(1.0f), node.scale);
        }

        /** Computes a rest-world transform through the validated parent chain. */
        glm::mat4 computeSkinningPointsRestWorld(
            const MichelleGlbAsset &asset,
            uint32_t nodeIndex)
        {
            eastl::vector<uint32_t> chain;
            int32_t current = static_cast<int32_t>(nodeIndex);
            while (current >= 0)
            {
                chain.push_back(static_cast<uint32_t>(current));
                current = asset.nodes[static_cast<uint32_t>(current)].parent;
            }
            glm::mat4 world(1.0f);
            for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator)
            {
                world *= composeSkinningPointsNode(asset.nodes[*iterator]);
            }
            return world;
        }

        /** Appends one typed component payload to the point entity. */
        void appendSkinningPointsPayload(
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

        /** Creates parent directories for one optional evidence artifact. */
        void prepareSkinningPointsPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }

        /** Writes one optional UTF-8 evidence artifact. */
        void writeSkinningPointsText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareSkinningPointsPath(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write webgpu_skinning_points evidence.");
            }
        }
    } // namespace

    void WebgpuSkinningPointsRuntimeAdapter::evaluatePointTargets(
        float animationTimeSeconds)
    {
        sampleMichelleSkinnedVertices(
            asset, animationTimeSeconds, skinnedPositions, skinnedNormals);
        const glm::mat4 restWorld = computeSkinningPointsRestWorld(
            asset, asset.meshNodeIndex);
        pointTargets.resize(skinnedPositions.size());
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        for (size_t index = 0u; index < skinnedPositions.size(); ++index)
        {
            const glm::vec4 restPosition = restWorld * skinnedPositions[index];
            const glm::vec4 worldPosition(
                restPosition.x * 100.0f,
                restPosition.z * 100.0f,
                -restPosition.y * 100.0f,
                1.0f);
            pointTargets[index] = worldPosition;
            minimum = glm::min(minimum, glm::vec3(worldPosition));
            maximum = glm::max(maximum, glm::vec3(worldPosition));
        }
        if (pointTargets.size() != PointCount ||
            maximum.x - minimum.x < 40.0f ||
            maximum.y - minimum.y < 40.0f)
        {
            std::ostringstream message;
            message << "Michelle point targets violate the locked geometry bounds: "
                    << minimum.x << ',' << minimum.y << ',' << minimum.z
                    << " to " << maximum.x << ',' << maximum.y << ','
                    << maximum.z << '.';
            throw std::runtime_error(message.str());
        }
    }

    void WebgpuSkinningPointsRuntimeAdapter::uploadInitialPointState()
    {
        zeroSpeeds.assign(PointCount, glm::vec4(0.0f));
        auto queue = device->graphicsQueue(0);
        queue
            ->writeBuffer(
                GVM::RHI::BufferRange(targetBuffer),
                pointTargets.data(),
                pointTargets.size() * sizeof(pointTargets[0u]))
            ->writeBuffer(
                GVM::RHI::BufferRange(positionBuffer),
                pointTargets.data(),
                pointTargets.size() * sizeof(pointTargets[0u]))
            ->writeBuffer(
                GVM::RHI::BufferRange(speedBuffer),
                zeroSpeeds.data(),
                zeroSpeeds.size() * sizeof(zeroSpeeds[0u]))
            ->submit();
    }

    void WebgpuSkinningPointsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool validScenario =
            (options.scenarioId == "loader-snapshot" &&
             options.targetFrame == 0u) ||
            (options.scenarioId == "initial-loader" &&
             options.targetFrame == 0u) ||
            (options.scenarioId == "animated" &&
             options.targetFrame == 60u);
        if (options.caseId != "webgpu_skinning_points" || !validScenario ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty() || !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgpu_skinning_points requires its three frozen r185 scenarios.");
        }
        device = inDevice;
        asset = loadMichelleGlbAsset(
            std::filesystem::path(options.assetRoot.c_str()) /
            "models/gltf/Michelle.glb");
        evaluatePointTargets(0.0f);
        instances.resize(PointCount);
        for (uint32_t index = 0u; index < PointCount; ++index)
        {
            instances[index].ordinal =
                glm::vec4(float(index), 0.0f, 0.0f, 0.0f);
        }

        const WebgpuSkinningPointsHostVertex vertices[4u] = {
            {glm::vec4(-0.5f, -0.5f, 0.0f, 0.0f)},
            {glm::vec4( 0.5f, -0.5f, 1.0f, 0.0f)},
            {glm::vec4( 0.5f,  0.5f, 1.0f, 1.0f)},
            {glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f)},
        };
        constexpr uint32_t indices[6u] = {0u, 1u, 2u, 0u, 2u, 3u};
        const WebgpuSkinningPointsHostObjectData objectData = {
            .viewProjectionColumn0 =
                glm::vec4(1.3403168253f, 0.0f, 0.0f, 0.0f),
            .viewProjectionColumn1 =
                glm::vec4(0.0f, 0.0002144507f, -1.0020019970f, -0.9999999950f),
            .viewProjectionColumn2 =
                glm::vec4(0.0f, -2.1445069098f, -0.0001002002f, -0.0001000000f),
            .viewProjectionColumn3 =
                glm::vec4(0.0f, -182.3474225392f, 298.5900800786f, 299.9914985000f),
            .viewport = glm::vec4(800.0f, 500.0f, 0.0f, 0.0f),
        };
        const WebgpuSkinningPointsHostMaterialData materialData = {
            .slowColor = glm::vec4(
                0.0f,
                skinningPointsSrgbToLinear(102.0f / 255.0f),
                1.0f,
                1.0f),
            .fastColor = glm::vec4(
                1.0f,
                skinningPointsSrgbToLinear(144.0f / 255.0f),
                0.0f,
                1.0f),
        };

        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create webgpu_skinning_points Set encoder.");
        }
        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = 4u;
        allocation.indicesCount = 6u;
        allocation.instanceCount = PointCount;
        appendSkinningPointsPayload(
            allocation,
            WebgpuSkinningPointsSceneRenderSetComponents::vertices,
            "WebgpuSkinningPointsVertices", vertices, sizeof(vertices), 1u);
        appendSkinningPointsPayload(
            allocation,
            WebgpuSkinningPointsSceneRenderSetComponents::indices,
            "WebgpuSkinningPointsIndices", indices, sizeof(indices), 1u);
        appendSkinningPointsPayload(
            allocation,
            WebgpuSkinningPointsSceneRenderSetComponents::objects,
            "WebgpuSkinningPointsObject", &objectData,
            sizeof(objectData), 1u);
        appendSkinningPointsPayload(
            allocation,
            WebgpuSkinningPointsSceneRenderSetComponents::instances,
            "WebgpuSkinningPointsInstances", instances.data(),
            instances.size() * sizeof(instances[0u]), PointCount);
        appendSkinningPointsPayload(
            allocation,
            WebgpuSkinningPointsSceneRenderSetComponents::materials,
            "WebgpuSkinningPointsMaterial", &materialData,
            sizeof(materialData), 1u);
        encoder->allocEntity(allocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuSkinningPointsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        evaluatePointTargets(static_cast<float>(frameIndex) / 60.0f);
        device->graphicsQueue(0)
            ->writeBuffer(
                GVM::RHI::BufferRange(targetBuffer),
                pointTargets.data(),
                pointTargets.size() * sizeof(pointTargets[0u]))
            ->submit();
    }

    void WebgpuSkinningPointsRuntimeAdapter::afterFrame(
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
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            prepareSkinningPointsPath(path);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write webgpu_skinning_points RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgpu_skinning_points\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"randomSeed\":" << options.randomSeed << ",\n"
                 << "  \"width\":" << width << ",\n"
                 << "  \"height\":" << height << ",\n"
                 << "  \"rowStrideBytes\":" << width * 4u << ",\n"
                 << "  \"byteCount\":" << byteCount << ",\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"sampleCount\":1,\n"
                 << "  \"msaaEnabled\":false,\n"
                 << "  \"inputReplay\":null\n}\n";
        writeSkinningPointsText(options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene << "{\n  \"schemaVersion\":1,\n"
              << "  \"caseId\":\"webgpu_skinning_points\",\n"
              << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
              << "  \"frame\":" << frameIndex << ",\n"
              << "  \"renderSetPolicy\":\"required\",\n"
              << "  \"gpuWorkDslOnly\":true,\n"
              << "  \"sceneRenderSetCount\":1,\n"
              << "  \"renderableObjectCount\":1,\n"
              << "  \"entityCount\":1,\n"
              << "  \"instanceCount\":16340,\n"
              << "  \"containsInstancing\":true,\n"
              << "  \"scenePassCount\":1,\n"
              << "  \"screenPassCount\":2,\n"
              << "  \"screenPasses\":[\"skinning-point-state-initialize-compute\",\"skinning-point-state-update-compute\"],\n"
              << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-expanded-skinning-points\"}],\n"
              << "  \"drawCommandCount\":1,\n"
              << "  \"directDrawFallback\":false,\n"
              << "  \"computeDispatchThreads\":16340,\n"
              << "  \"usesRenderEntityID\":true,\n"
              << "  \"usesRenderEntityInstanceID\":true,\n"
              << "  \"sampleCount\":1,\n"
              << "  \"msaaEnabled\":false,\n"
              << "  \"sceneRoots\":[{\n"
              << "    \"id\":\"scene\",\n"
              << "    \"renderSetCount\":1,\n"
              << "    \"renderSetId\":\"scene\",\n"
              << "    \"renderSetType\":\"WebgpuSkinningPointsSceneRenderSet\",\n"
              << "    \"renderableObjectCount\":1,\n"
              << "    \"entityCount\":1,\n"
              << "    \"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"michelle-points\",\"instanceCount\":16340}],\n"
              << "    \"componentSchema\":[\n"
              << "      {\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},\n"
              << "      {\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},\n"
              << "      {\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},\n"
              << "      {\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},\n"
              << "      {\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},\n"
              << "      {\"name\":\"sourceSkinVertices\",\"kind\":\"buffer\",\"role\":\"michelle-position-joints-weights-source-data\"},\n"
              << "      {\"name\":\"skinPalettes\",\"kind\":\"buffer\",\"role\":\"michelle-65-joint-palette\"},\n"
              << "      {\"name\":\"pointStates\",\"kind\":\"buffer\",\"role\":\"per-instance-position-and-speed-addressing\"}\n"
              << "    ],\n"
              << "    \"drawCommandCount\":1,\n"
              << "    \"directDrawFallback\":false,\n"
              << "    \"scenePasses\":[{\"name\":\"main-expanded-skinning-points\",\"renderClass\":\"WebgpuSkinningPointsMainPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
              << "  }]\n}\n";
        writeSkinningPointsText(options.sceneSnapshotPath, scene.str());
        if (options.scenarioId == "loader-snapshot")
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\":1,\n"
                     << "  \"caseId\":\"webgpu_skinning_points\",\n"
                     << "  \"scenarioId\":\"loader-snapshot\",\n"
                     << "  \"frame\":0,\n"
                     << "  \"kind\":\"loader-snapshot\",\n"
                     << "  \"canonicalState\":\"michelle-one-source-primitive-16340-point-instances-65-joints\",\n"
                     << "  \"result\":{\n"
                     << "    \"renderableObjectCount\":1,\n"
                     << "    \"sceneRootCount\":1,\n"
                     << "    \"canonicalSceneSha256\":\"12f5c3d8d790ebb8f376a80d12d95610fd8bdfffd1ec31e0844bebcd9bd2896a\",\n"
                     << "    \"assetSha256\":\"" << asset.sha256.c_str() << "\",\n"
                     << "    \"vertexCount\":16340,\n"
                     << "    \"indexCount\":84318,\n"
                     << "    \"pointInstanceCount\":16340,\n"
                     << "    \"nodeCount\":67,\n"
                     << "    \"jointCount\":65,\n"
                     << "    \"animationChannelCount\":195\n"
                     << "  }\n}\n";
            writeSkinningPointsText(
                options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebgpuSkinningPointsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        skinnedPositions.clear();
        skinnedNormals.clear();
        pointTargets.clear();
        zeroSpeeds.clear();
        instances.clear();
        asset = {};
    }
} // namespace GVM::ThreeSamples
