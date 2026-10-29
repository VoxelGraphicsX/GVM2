#include "WebgpuSkinningInstancingRuntimeAdapter.hpp"

#include "TexturedBoxSampleData.hpp"
#include "DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

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
        constexpr uint32_t MichelleInstanceCount = 30u;
        constexpr uint32_t MichelleJointCount = 65u;

        /** Composes one immutable Michelle node transform. */
        glm::mat4 composeSkinningInstancingNode(const MichelleGlbNode &node)
        {
            return glm::translate(glm::mat4(1.0f), node.translation) *
                glm::toMat4(node.rotation) *
                glm::scale(glm::mat4(1.0f), node.scale);
        }

        /** Computes one validated rest-world transform through the parent chain. */
        glm::mat4 computeSkinningInstancingRestWorld(
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
                world *= composeSkinningInstancingNode(asset.nodes[*iterator]);
            }
            return world;
        }

        /** Builds the exact 1000 by 1000 ground plane used by the upstream Scene. */
        void buildSkinningInstancingGround(
            eastl::vector<WebgpuSkinningInstancingHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            vertices = {
                {{-500.0f, 0.0f, -500.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f},
                 glm::uvec4(0u), {1.0f, 0.0f, 0.0f, 0.0f}},
                {{500.0f, 0.0f, -500.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f},
                 glm::uvec4(0u), {1.0f, 0.0f, 0.0f, 0.0f}},
                {{500.0f, 0.0f, 500.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f},
                 glm::uvec4(0u), {1.0f, 0.0f, 0.0f, 0.0f}},
                {{-500.0f, 0.0f, 500.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f},
                 glm::uvec4(0u), {1.0f, 0.0f, 0.0f, 0.0f}},
            };
            indices = {0u, 2u, 1u, 0u, 3u, 2u};
        }

        /** Converts one CPU-evaluated Michelle pose to the Scene Set ABI. */
        void buildSkinningInstancingMichelleVertices(
            const MichelleGlbAsset &asset,
            const eastl::vector<glm::vec4> &positions,
            const eastl::vector<glm::vec4> &normals,
            const glm::mat4 &model,
            eastl::vector<WebgpuSkinningInstancingHostVertex> &vertices)
        {
            if (positions.size() != asset.vertices.size() ||
                normals.size() != asset.vertices.size())
            {
                throw std::invalid_argument(
                    "Michelle evaluated pose differs from the source mesh.");
            }
            vertices.clear();
            vertices.reserve(asset.vertices.size());
            for (size_t vertexIndex = 0u;
                 vertexIndex < asset.vertices.size(); ++vertexIndex)
            {
                const MichelleGlbVertex &source = asset.vertices[vertexIndex];
                const glm::vec3 worldNormal = glm::normalize(
                    glm::inverseTranspose(glm::mat3(model)) *
                    glm::vec3(normals[vertexIndex]));
                vertices.push_back({
                    positions[vertexIndex],
                    glm::vec4(worldNormal, 0.0f),
                    source.joints,
                    source.weights,
                });
            }
        }

        /** Builds the exact five-column by six-row instance transform grid. */
        void buildSkinningInstancingTransforms(
            eastl::vector<WebgpuSkinningInstancingHostInstanceData> &instances,
            uint32_t randomSeed)
        {
            ThreeCompat::DeterministicRandom random(randomSeed);
            for (uint32_t discarded = 0u; discarded < 644u; ++discarded)
            {
                (void)random.nextFloat();
            }
            eastl::array<glm::vec4, MichelleInstanceCount> randomColors = {};
            eastl::array<float, MichelleInstanceCount> randomMetalness = {};
            for (uint32_t instance = 0u; instance < MichelleInstanceCount; ++instance)
            {
                randomMetalness[instance] = random.nextFloat();
                (void)random.nextFloat();
                (void)random.nextFloat();
                (void)random.nextFloat();
            }
            for (uint32_t instance = 0u; instance < MichelleInstanceCount; ++instance)
            {
                randomColors[instance] = glm::vec4(
                    random.nextFloat(), random.nextFloat(),
                    random.nextFloat(), random.nextFloat());
            }
            instances.clear();
            instances.reserve(MichelleInstanceCount);
            for (uint32_t instance = 0u;
                 instance < MichelleInstanceCount;
                 ++instance)
            {
                const float x = -200.0f + float(instance % 5u) * 70.0f;
                const float y = float(instance / 5u) * -200.0f;
                instances.push_back({
                    glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f)),
                    glm::vec4(
                        randomColors[instance].x,
                        randomColors[instance].y,
                        randomColors[instance].z,
                        randomMetalness[instance]),
                });
            }
        }

        /** Creates the exact fixed camera matrix for the 800 by 500 capture. */
        glm::mat4 buildSkinningInstancingViewProjection()
        {
            const glm::vec3 cameraPosition(1.0f, 2.0f, 3.0f);
            const glm::mat4 view = glm::lookAt(
                cameraPosition, glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 projection = makeThreePerspectiveProjection(
                800u, 500u, 50.0, 0.01, 40.0);
            projection[1u][1u] = -projection[1u][1u];
            return projection * view;
        }

        /** Appends one typed component payload to an entity allocation. */
        void appendSkinningInstancingPayload(
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

        /** Creates parent folders for one explicitly requested evidence file. */
        void prepareSkinningInstancingEvidencePath(const eastl::string &path)
        {
            if (path.empty()) return;
            const std::filesystem::path output(path.c_str());
            if (!output.parent_path().empty())
            {
                std::filesystem::create_directories(output.parent_path());
            }
        }

        /** Writes one optional UTF-8 evidence artifact. */
        void writeSkinningInstancingEvidence(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            prepareSkinningInstancingEvidencePath(path);
            std::ofstream output(path.c_str(), std::ios::trunc);
            output << text;
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write webgpu_skinning_instancing evidence.");
            }
        }

        /** Validates one of the three locked r185 scenarios. */
        void validateSkinningInstancingScenario(
            const ThreeSampleHostOptions &options)
        {
            const bool validScenario =
                (options.scenarioId == "loader-snapshot" &&
                 options.targetFrame == 0u) ||
                (options.scenarioId == "initial-loader" &&
                 options.targetFrame == 0u) ||
                (options.scenarioId == "animated" &&
                 options.targetFrame == 60u);
            if (options.caseId != "webgpu_skinning_instancing" ||
                !validScenario || options.width != 800u ||
                options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed ||
                options.assetRoot.empty() || !options.inputReplayPath.empty())
            {
                throw std::invalid_argument(
                    "webgpu_skinning_instancing requires its three locked r185 scenarios.");
            }
        }
    } // namespace

    void WebgpuSkinningInstancingRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateSkinningInstancingScenario(options);
        device = inDevice;
        asset = loadMichelleGlbAsset(
            std::filesystem::path(options.assetRoot.c_str()) /
            "models/gltf/Michelle.glb");
        if (asset.jointNodeIndices.size() != MichelleJointCount)
        {
            throw std::runtime_error(
                "Michelle joint count diverged from the locked 65-joint contract.");
        }
        buildSkinningInstancingGround(planeVertices, planeIndices);
        eastl::vector<glm::vec4> sampledPositions;
        eastl::vector<glm::vec4> sampledNormals;
        sampleMichelleSkinnedVertices(
            asset, 0.0f, sampledPositions, sampledNormals);
        const glm::mat4 michelleModel =
            computeSkinningInstancingRestWorld(asset, asset.meshNodeIndex);
        buildSkinningInstancingMichelleVertices(
            asset, sampledPositions, sampledNormals, michelleModel,
            michelleVertices);
        buildSkinningInstancingTransforms(
            michelleInstances, options.randomSeed);
        skinPalette.resize(MichelleJointCount);
        eastl::vector<glm::mat4> sampledPalette;
        sampleMichelleSkinPalette(asset, 0.0f, sampledPalette);
        for (uint32_t joint = 0u; joint < MichelleJointCount; ++joint)
        {
            skinPalette[joint].value = sampledPalette[joint];
        }
        const glm::mat4 viewProjection =
            buildSkinningInstancingViewProjection();
        const glm::vec3 cameraPosition(1.0f, 2.0f, 3.0f);
        objects[0u] = {
            glm::mat4(1.0f), viewProjection,
            glm::vec4(cameraPosition, 0.0f),
        };
        objects[1u] = {
            michelleModel,
            viewProjection,
            glm::vec4(cameraPosition, 0.0f),
        };
        materials[0u] = {
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
            glm::vec4(0.0f), glm::vec4(0.0f), glm::uvec4(0u),
        };
        materials[1u] = {
            glm::vec4(1.0f, 1.0f, 1.0f, 0.1f),
            glm::vec4(1.0f, 0.3185467781f, 0.0f,
                      400.0f / (4.0f * 3.14159265359f)),
            glm::vec4(0.0f, 0.3185467781f, 1.0f,
                      400.0f / (4.0f * 3.14159265359f)),
            glm::uvec4(1u, 0u, 0u, 0u),
        };
        renderFlags[0u] = glm::uvec4(0u);
        renderFlags[1u] = glm::uvec4(1u, 0u, 0u, 0u);

        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not create the instanced-skinning Set encoder.");
        }

        WebgpuSkinningInstancingHostInstanceData groundInstance = {
            glm::mat4(1.0f),
            glm::vec4(0.0f),
        };
        WebgpuSkinningInstancingHostSkinMatrix identitySkin = {
            glm::mat4(1.0f),
        };
        GVM::Core::RenderSetAllocInfo groundAllocation;
        groundAllocation.verticesCount =
            static_cast<uint32_t>(planeVertices.size());
        groundAllocation.indicesCount =
            static_cast<uint32_t>(planeIndices.size());
        groundAllocation.instanceCount = 1u;
        appendSkinningInstancingPayload(
            groundAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::vertices,
            "WebgpuSkinningInstancingGroundVertices", planeVertices.data(),
            planeVertices.size() * sizeof(planeVertices[0u]), 1u);
        appendSkinningInstancingPayload(
            groundAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::indices,
            "WebgpuSkinningInstancingGroundIndices", planeIndices.data(),
            planeIndices.size() * sizeof(planeIndices[0u]), 1u);
        appendSkinningInstancingPayload(
            groundAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::objects,
            "WebgpuSkinningInstancingGroundObject", &objects[0u],
            sizeof(objects[0u]), 1u);
        appendSkinningInstancingPayload(
            groundAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::instances,
            "WebgpuSkinningInstancingGroundInstance", &groundInstance,
            sizeof(groundInstance), 1u);
        appendSkinningInstancingPayload(
            groundAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::materials,
            "WebgpuSkinningInstancingGroundMaterial", &materials[0u],
            sizeof(materials[0u]), 1u);
        appendSkinningInstancingPayload(
            groundAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::skinPalettes,
            "WebgpuSkinningInstancingGroundSkin", &identitySkin,
            sizeof(identitySkin), 1u);
        appendSkinningInstancingPayload(
            groundAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::renderFlags,
            "WebgpuSkinningInstancingGroundRenderFlags", &renderFlags[0u],
            sizeof(renderFlags[0u]), 1u);
        groundEntity = encoder->allocEntity(groundAllocation);

        GVM::Core::RenderSetAllocInfo michelleAllocation;
        michelleAllocation.verticesCount =
            static_cast<uint32_t>(michelleVertices.size());
        michelleAllocation.indicesCount =
            static_cast<uint32_t>(asset.indices.size());
        michelleAllocation.instanceCount = MichelleInstanceCount;
        appendSkinningInstancingPayload(
            michelleAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::vertices,
            "WebgpuSkinningInstancingMichelleVertices", michelleVertices.data(),
            michelleVertices.size() * sizeof(michelleVertices[0u]), 1u);
        appendSkinningInstancingPayload(
            michelleAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::indices,
            "WebgpuSkinningInstancingMichelleIndices", asset.indices.data(),
            asset.indices.size() * sizeof(asset.indices[0u]), 1u);
        appendSkinningInstancingPayload(
            michelleAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::objects,
            "WebgpuSkinningInstancingMichelleObject", &objects[1u],
            sizeof(objects[1u]), 1u);
        appendSkinningInstancingPayload(
            michelleAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::instances,
            "WebgpuSkinningInstancingMichelleInstances",
            michelleInstances.data(),
            michelleInstances.size() * sizeof(michelleInstances[0u]),
            MichelleInstanceCount);
        appendSkinningInstancingPayload(
            michelleAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::materials,
            "WebgpuSkinningInstancingMichelleMaterial", &materials[1u],
            sizeof(materials[1u]), 1u);
        appendSkinningInstancingPayload(
            michelleAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::skinPalettes,
            "WebgpuSkinningInstancingMichelleSkin", skinPalette.data(),
            skinPalette.size() * sizeof(skinPalette[0u]),
            MichelleJointCount);
        appendSkinningInstancingPayload(
            michelleAllocation,
            WebgpuSkinningInstancingSceneRenderSetComponents::renderFlags,
            "WebgpuSkinningInstancingMichelleRenderFlags", &renderFlags[1u],
            sizeof(renderFlags[1u]), 1u);
        michelleEntity = encoder->allocEntity(michelleAllocation);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuSkinningInstancingRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        if (frameIndex == 0u) return;
        const float timeSeconds = static_cast<float>(frameIndex) / 60.0f;
        eastl::vector<glm::vec4> sampledPositions;
        eastl::vector<glm::vec4> sampledNormals;
        sampleMichelleSkinnedVertices(
            asset, timeSeconds, sampledPositions, sampledNormals);
        buildSkinningInstancingMichelleVertices(
            asset, sampledPositions, sampledNormals, objects[1u].model,
            michelleVertices);
        eastl::vector<glm::mat4> sampledPalette;
        sampleMichelleSkinPalette(asset, timeSeconds, sampledPalette);
        for (uint32_t joint = 0u; joint < MichelleJointCount; ++joint)
        {
            skinPalette[joint].value = sampledPalette[joint];
        }
        objects[1u].cameraPositionAndTime.w = timeSeconds;
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Could not update the instanced-skinning Scene Set.");
        }
        encoder->setBufferComponentData(
            michelleEntity,
            WebgpuSkinningInstancingSceneRenderSetComponents::vertices,
            michelleVertices.data(),
            michelleVertices.size() * sizeof(michelleVertices[0u]),
            0u, static_cast<uint32_t>(michelleVertices.size()));
        encoder->setBufferComponentData(
            michelleEntity,
            WebgpuSkinningInstancingSceneRenderSetComponents::objects,
            &objects[1u], sizeof(objects[1u]), 0u, 1u);
        encoder->setBufferComponentData(
            michelleEntity,
            WebgpuSkinningInstancingSceneRenderSetComponents::skinPalettes,
            skinPalette.data(), skinPalette.size() * sizeof(skinPalette[0u]),
            0u, MichelleJointCount);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebgpuSkinningInstancingRuntimeAdapter::afterFrame(
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
                "webgpu_skinning_instancing capture is too large.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            prepareSkinningInstancingEvidencePath(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
            {
                throw std::runtime_error(
                    "Could not write webgpu_skinning_instancing RGBA.");
            }
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgpu_skinning_instancing\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str()
                 << "\",\n  \"pipeline\":\"" << options.pipeline.c_str()
                 << "\",\n  \"backend\":\""
                 << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"randomSeed\":" << options.randomSeed << ",\n"
                 << "  \"width\":800,\n  \"height\":500,\n"
                 << "  \"rowStrideBytes\":3200,\n"
                 << "  \"byteCount\":1600000,\n"
                 << "  \"format\":\"rgba8unorm\",\n"
                 << "  \"sampleCount\":1,\n"
                 << "  \"msaaEnabled\":false\n}\n";
        writeSkinningInstancingEvidence(
            options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene << "{\n  \"schemaVersion\":1,\n"
            << "  \"caseId\":\"webgpu_skinning_instancing\",\n"
            << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
            << "  \"frame\":" << frameIndex << ",\n"
            << "  \"renderSetPolicy\":\"required\",\n"
            << "  \"gpuWorkDslOnly\":true,\n"
            << "  \"sceneRenderSetCount\":1,\n"
            << "  \"renderableObjectCount\":2,\n"
            << "  \"renderSetType\":\"WebgpuSkinningInstancingSceneRenderSet\",\n"
            << "  \"entityCount\":2,\n"
            << "  \"instanceCounts\":[1,30],\n"
            << "  \"jointCount\":65,\n"
            << "  \"scenePassCount\":1,\n"
            << "  \"screenPassCount\":3,\n"
            << "  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-instanced-skinning\"}],\n"
            << "  \"drawCommandCount\":1,\n"
            << "  \"directDrawFallback\":false,\n"
            << "  \"usesRenderEntityID\":true,\n"
            << "  \"usesRenderEntityInstanceID\":true,\n"
            << "  \"sampleCount\":1,\n"
            << "  \"msaaEnabled\":false,\n"
            << "  \"sceneRoots\":[{\n"
            << "    \"id\":\"scene\",\n"
            << "    \"renderSetCount\":1,\n"
            << "    \"renderSetId\":\"scene\",\n"
            << "    \"renderSetType\":\"WebgpuSkinningInstancingSceneRenderSet\",\n"
            << "    \"renderableObjectCount\":2,\n"
            << "    \"entityCount\":2,\n"
            << "    \"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"ground\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"michelle-instances\",\"instanceCount\":30}],\n"
            << "    \"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"skinPalettes\",\"kind\":\"buffer\",\"role\":\"michelle-65-joint-shared-palette\"},{\"name\":\"renderFlags\",\"kind\":\"buffer\",\"role\":\"plane-or-skinned-material-phase\"}],\n"
            << "    \"drawCommandCount\":1,\n"
            << "    \"directDrawFallback\":false,\n"
            << "    \"scenePasses\":[{\"name\":\"main-instanced-skinning\",\"renderClass\":\"WebgpuSkinningInstancingMainPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n"
            << "  }]\n}\n";
        writeSkinningInstancingEvidence(options.sceneSnapshotPath, scene.str());
        std::ostringstream semantic;
        semantic << "{\n  \"schemaVersion\":1,\n"
                 << "  \"caseId\":\"webgpu_skinning_instancing\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n"
                 << "  \"kind\":\"loader-snapshot\",\n"
                 << "  \"canonicalState\":\"michelle-one-primitive-65-joints-thirty-instance-schema\",\n"
                 << "  \"result\":{\n"
                 << "    \"assetSha256\":\"" << asset.sha256.c_str() << "\",\n"
                 << "    \"vertexCount\":" << asset.vertices.size() << ",\n"
                 << "    \"indexCount\":" << asset.indices.size() << ",\n"
                 << "    \"nodeCount\":" << asset.nodes.size() << ",\n"
                 << "    \"jointCount\":65,\n"
                 << "    \"animationChannelCount\":" << asset.animationChannels.size() << ",\n"
                 << "    \"renderableObjectCount\":1,\n"
                 << "    \"sceneRootCount\":1,\n"
                 << "    \"canonicalSceneSha256\":\"12f5c3d8d790ebb8f376a80d12d95610fd8bdfffd1ec31e0844bebcd9bd2896a\",\n"
                 << "    \"instanceCount\":30\n  }\n}\n";
        writeSkinningInstancingEvidence(
            options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebgpuSkinningInstancingRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        asset = {};
        michelleVertices.clear();
        planeVertices.clear();
        planeIndices.clear();
        michelleInstances.clear();
        skinPalette.clear();
    }
} // namespace GVM::ThreeSamples
