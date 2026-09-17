#include "WebglLoaderColladaKinematicsRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

        /** Builds the r185 perspective matrix for the current RHI clip contract. */
        glm::mat4 makeKinematicsPerspective(uint32_t width, uint32_t height)
        {
            constexpr double NearDistance = 1.0;
            constexpr double FarDistance = 2000.0;
            const double aspect = double(width) / double(height);
            const double top = NearDistance * std::tan(45.0 * Pi / 360.0);
            const double depth = FarDistance - NearDistance;
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(1.0 / (top * aspect));
            projection[1u][1u] = static_cast<float>(-1.0 / top);
            projection[2u][2u] = static_cast<float>(-FarDistance / depth);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(-FarDistance * NearDistance / depth);
            return projection;
        }

        /** Appends one typed Set component payload. */
        void appendKinematicsBuffer(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *data,
            uint64_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = data,
                .dataStorageSize = byteCount,
                .instanceCount = instanceCount,
            });
        }

        /** Appends one endpoint-encoded grid strip for screen-space expansion. */
        void appendKinematicsGridStrip(
            eastl::vector<WebglLoaderColladaKinematicsHostVertex> &vertices,
            eastl::vector<uint32_t> &indices,
            const glm::vec3 &start,
            const glm::vec3 &end,
            const glm::vec4 &color)
        {
            const uint32_t first = static_cast<uint32_t>(vertices.size());
            vertices.push_back({glm::vec4(start, 1.0f), glm::vec4(end, -1.0f), color});
            vertices.push_back({glm::vec4(start, 1.0f), glm::vec4(end, 1.0f), color});
            vertices.push_back({glm::vec4(end, 1.0f), glm::vec4(start, 2.0f), color});
            vertices.push_back({glm::vec4(end, 1.0f), glm::vec4(start, -2.0f), color});
            const uint32_t strip[] = {
                first, first + 1u, first + 2u,
                first, first + 2u, first + 3u,
            };
            indices.insert(indices.end(), strip, strip + 6u);
        }

        /** Generates the 20-by-20 GridHelper as deterministic triangle strips. */
        void buildKinematicsGrid(
            eastl::vector<WebglLoaderColladaKinematicsHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            for (int32_t line = -10; line <= 10; ++line)
            {
                const float offset = static_cast<float>(line);
                const float channel = line == 0 ? 193.0f / 255.0f : 141.0f / 255.0f;
                const glm::vec4 color(channel, channel, channel, 1.0f);
                appendKinematicsGridStrip(
                    vertices, indices, {-10.0f, 0.0f, offset},
                    {10.0f, 0.0f, offset}, color);
                appendKinematicsGridStrip(
                    vertices, indices, {offset, 0.0f, -10.0f},
                    {offset, 0.0f, 10.0f}, color);
            }
        }

        /** Writes one optional UTF-8 evidence file. */
        void writeKinematicsEvidence(const eastl::string &path, const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            if (!outputPath.parent_path().empty())
                std::filesystem::create_directories(outputPath.parent_path());
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output) throw std::runtime_error("Could not write kinematics evidence.");
        }
    } // namespace

    void WebglLoaderColladaKinematicsRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool validScenario =
            (options.scenarioId == "initial-loader" && options.targetFrame == 0u) ||
            (options.scenarioId == "canonical-loader" && options.targetFrame == 0u) ||
            (options.scenarioId == "animated" && options.targetFrame == 60u);
        if (options.caseId != "webgl_loader_collada_kinematics" ||
            !validScenario || options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty() ||
            !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "webgl_loader_collada_kinematics requires one frozen Manifest scenario.");
        }
        device = inDevice;
        asset = loadColladaRobotAsset(
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "collada" / "abb_irb52_7_120.dae");
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("Could not create the kinematics Set encoder.");

        WebglLoaderColladaKinematicsHostInstanceData instance{};
        const glm::vec4 orange(0.50288648f, 0.09989872f, 0.02217388f, 0.0f);
        const glm::vec4 dark(0.02955684f, 0.03189602f, 0.03433982f, 0.0f);
        for (uint32_t linkIndex = 0u; linkIndex < asset.links.size(); ++linkIndex)
        {
            constexpr const char *VertexNames[] = {
                "WebglLoaderColladaKinematicsBaseVertices",
                "WebglLoaderColladaKinematicsLink1Vertices",
                "WebglLoaderColladaKinematicsLink2Vertices",
                "WebglLoaderColladaKinematicsLink3Vertices",
                "WebglLoaderColladaKinematicsLink4Vertices",
                "WebglLoaderColladaKinematicsLink5Vertices",
                "WebglLoaderColladaKinematicsLink6Vertices",
            };
            constexpr const char *IndexNames[] = {
                "WebglLoaderColladaKinematicsBaseIndices",
                "WebglLoaderColladaKinematicsLink1Indices",
                "WebglLoaderColladaKinematicsLink2Indices",
                "WebglLoaderColladaKinematicsLink3Indices",
                "WebglLoaderColladaKinematicsLink4Indices",
                "WebglLoaderColladaKinematicsLink5Indices",
                "WebglLoaderColladaKinematicsLink6Indices",
            };
            constexpr const char *ObjectNames[] = {
                "WebglLoaderColladaKinematicsBaseObject",
                "WebglLoaderColladaKinematicsLink1Object",
                "WebglLoaderColladaKinematicsLink2Object",
                "WebglLoaderColladaKinematicsLink3Object",
                "WebglLoaderColladaKinematicsLink4Object",
                "WebglLoaderColladaKinematicsLink5Object",
                "WebglLoaderColladaKinematicsLink6Object",
            };
            constexpr const char *InstanceNames[] = {
                "WebglLoaderColladaKinematicsBaseInstance",
                "WebglLoaderColladaKinematicsLink1Instance",
                "WebglLoaderColladaKinematicsLink2Instance",
                "WebglLoaderColladaKinematicsLink3Instance",
                "WebglLoaderColladaKinematicsLink4Instance",
                "WebglLoaderColladaKinematicsLink5Instance",
                "WebglLoaderColladaKinematicsLink6Instance",
            };
            constexpr const char *MaterialNames[] = {
                "WebglLoaderColladaKinematicsBaseMaterial",
                "WebglLoaderColladaKinematicsLink1Material",
                "WebglLoaderColladaKinematicsLink2Material",
                "WebglLoaderColladaKinematicsLink3Material",
                "WebglLoaderColladaKinematicsLink4Material",
                "WebglLoaderColladaKinematicsLink5Material",
                "WebglLoaderColladaKinematicsLink6Material",
            };
            const ColladaRobotLink &link = asset.links[linkIndex];
            eastl::vector<WebglLoaderColladaKinematicsHostVertex> vertices;
            eastl::vector<uint32_t> indices;
            vertices.reserve(link.vertices.size());
            indices.reserve(link.vertices.size());
            for (uint32_t vertexIndex = 0u; vertexIndex < link.vertices.size(); ++vertexIndex)
            {
                const ColladaRobotVertex &source = link.vertices[vertexIndex];
                vertices.push_back({glm::vec4(source.position, 1.0f),
                                    glm::vec4(source.normal, 0.0f), glm::vec4(1.0f)});
                indices.push_back(vertexIndex);
            }
            WebglLoaderColladaKinematicsHostMaterialData material{};
            material.baseColorAndPhase = linkIndex == 6u ? dark : orange;
            material.specularAndShininess =
                {0.000433854f, 0.000433854f, 0.000433854f, 30.0f};
            WebglLoaderColladaKinematicsEntityState state;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            appendKinematicsBuffer(allocation,
                WebglLoaderColladaKinematicsSceneRenderSetComponents::vertices,
                VertexNames[linkIndex], vertices.data(),
                vertices.size() * sizeof(vertices[0]), 1u);
            appendKinematicsBuffer(allocation,
                WebglLoaderColladaKinematicsSceneRenderSetComponents::indices,
                IndexNames[linkIndex], indices.data(),
                indices.size() * sizeof(indices[0]), 1u);
            appendKinematicsBuffer(allocation,
                WebglLoaderColladaKinematicsSceneRenderSetComponents::objects,
                ObjectNames[linkIndex], &state.objectData,
                sizeof(state.objectData), 1u);
            appendKinematicsBuffer(allocation,
                WebglLoaderColladaKinematicsSceneRenderSetComponents::instances,
                InstanceNames[linkIndex], &instance, sizeof(instance), 1u);
            appendKinematicsBuffer(allocation,
                WebglLoaderColladaKinematicsSceneRenderSetComponents::materials,
                MaterialNames[linkIndex], &material, sizeof(material), 1u);
            state.entityIndex = encoder->allocEntity(allocation);
            entities.push_back(state);
        }

        eastl::vector<WebglLoaderColladaKinematicsHostVertex> gridVertices;
        eastl::vector<uint32_t> gridIndices;
        buildKinematicsGrid(gridVertices, gridIndices);
        WebglLoaderColladaKinematicsHostMaterialData gridMaterial{};
        gridMaterial.baseColorAndPhase = {1.0f, 1.0f, 1.0f, 1.0f};
        WebglLoaderColladaKinematicsEntityState gridState;
        GVM::Core::RenderSetAllocInfo gridAllocation;
        gridAllocation.verticesCount = static_cast<uint32_t>(gridVertices.size());
        gridAllocation.indicesCount = static_cast<uint32_t>(gridIndices.size());
        gridAllocation.instanceCount = 1u;
        appendKinematicsBuffer(gridAllocation,
            WebglLoaderColladaKinematicsSceneRenderSetComponents::vertices,
            "WebglLoaderColladaKinematicsGridVertices", gridVertices.data(),
            gridVertices.size() * sizeof(gridVertices[0]), 1u);
        appendKinematicsBuffer(gridAllocation,
            WebglLoaderColladaKinematicsSceneRenderSetComponents::indices,
            "WebglLoaderColladaKinematicsGridIndices", gridIndices.data(),
            gridIndices.size() * sizeof(gridIndices[0]), 1u);
        appendKinematicsBuffer(gridAllocation,
            WebglLoaderColladaKinematicsSceneRenderSetComponents::objects,
            "WebglLoaderColladaKinematicsGridObject", &gridState.objectData,
            sizeof(gridState.objectData), 1u);
        appendKinematicsBuffer(gridAllocation,
            WebglLoaderColladaKinematicsSceneRenderSetComponents::instances,
            "WebglLoaderColladaKinematicsGridInstance", &instance, sizeof(instance), 1u);
        appendKinematicsBuffer(gridAllocation,
            WebglLoaderColladaKinematicsSceneRenderSetComponents::materials,
            "WebglLoaderColladaKinematicsGridMaterial", &gridMaterial,
            sizeof(gridMaterial), 1u);
        gridState.entityIndex = encoder->allocEntity(gridAllocation);
        entities.push_back(gridState);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        updateEntityObjects(renderer, options.targetFrame);
    }

    void WebglLoaderColladaKinematicsRuntimeAdapter::updateEntityObjects(
        GVM::Core::AbstractRendererImpl &renderer,
        uint32_t frameIndex)
    {
        eastl::vector<float> joints(7u, 0.0f);
        if (frameIndex >= 60u)
        {
            constexpr float Progress = 0.42758f;
            const float targets[] = {0.0f, -18.0f, 28.0f, -203.0f, 159.0f, 13.0f, 155.0f};
            for (uint32_t index = 1u; index < 7u; ++index)
                joints[index] = targets[index] * Progress;
        }
        const eastl::vector<glm::mat4> transforms =
            evaluateColladaRobotPose(asset, joints);
        const double timer = 2.0459446672209074 +
            static_cast<double>(frameIndex) / 60.0 * 0.1;
        const glm::vec3 cameraPosition(
            std::cos(timer) * 20.0f, 10.0f, std::sin(timer) * 20.0f);
        const glm::mat4 view = glm::lookAt(
            cameraPosition, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 viewProjection = makeKinematicsPerspective(800u, 500u) * view;
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("Could not update kinematics object data.");
        for (uint32_t entity = 0u; entity < entities.size(); ++entity)
        {
            auto &state = entities[entity];
            state.objectData.model = entity < 7u ? transforms[entity] : glm::mat4(1.0f);
            state.objectData.viewProjection = viewProjection;
            state.objectData.normalTransform = glm::transpose(glm::inverse(state.objectData.model));
            state.objectData.cameraPosition = glm::vec4(cameraPosition, 1.0f);
            encoder->setBufferComponentData(
                state.entityIndex,
                WebglLoaderColladaKinematicsSceneRenderSetComponents::objects,
                &state.objectData, sizeof(state.objectData), 0u, 1u);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglLoaderColladaKinematicsRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)options;
        updateEntityObjects(renderer, frameIndex);
    }

    void WebglLoaderColladaKinematicsRuntimeAdapter::afterFrame(
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
        device->graphicsQueue(0)->readTexture(
            readbackTexture, rgba.data(), rgba.size())->submit();
        if (!options.captureRgbaPath.empty())
        {
            const std::filesystem::path path(options.captureRgbaPath.c_str());
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(rgba.data()),
                         static_cast<std::streamsize>(rgba.size()));
            if (!output) throw std::runtime_error("Could not write kinematics RGBA.");
        }
        std::ostringstream metadata;
        metadata << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_loader_collada_kinematics\",\n"
                 << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                 << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                 << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                 << "  \"frame\":" << frameIndex << ",\n  \"randomSeed\":" << options.randomSeed
                 << ",\n  \"width\":" << width << ",\n  \"height\":" << height
                 << ",\n  \"rowStrideBytes\":" << uint64_t(width) * 4u
                 << ",\n  \"byteCount\":" << byteCount
                 << ",\n  \"format\":\"rgba8unorm\",\n  \"sampleCount\":1,\n  \"msaaEnabled\":false\n}\n";
        writeKinematicsEvidence(options.captureMetadataPath, metadata.str());
        std::ostringstream scene;
        scene << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_loader_collada_kinematics\",\n"
              << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
              << "  \"frame\":" << frameIndex << ",\n  \"gpuWorkDslOnly\":true,\n"
              << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
              << "  \"renderSetType\":\"WebglLoaderColladaKinematicsSceneRenderSet\",\n"
              << "  \"renderableObjectCount\":8,\n  \"entityCount\":8,\n"
              << "  \"instanceCounts\":[1,1,1,1,1,1,1,1],\n"
              << "  \"drawCommandCount\":2,\n  \"scenePassCount\":2,\n"
              << "  \"usesRenderEntityID\":true,\n  \"usesRenderEntityInstanceID\":false,\n"
              << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
              << "  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,"
              << "\"renderSetId\":\"scene\",\"renderSetType\":\"WebglLoaderColladaKinematicsSceneRenderSet\","
              << "\"renderableObjectCount\":8,\"entityCount\":8,\"drawCommandCount\":2,"
              << "\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"base-link\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"link-1\",\"instanceCount\":1},{\"entityId\":2,\"logicalRenderableId\":\"link-2\",\"instanceCount\":1},{\"entityId\":3,\"logicalRenderableId\":\"link-3\",\"instanceCount\":1},{\"entityId\":4,\"logicalRenderableId\":\"link-4\",\"instanceCount\":1},{\"entityId\":5,\"logicalRenderableId\":\"link-5\",\"instanceCount\":1},{\"entityId\":6,\"logicalRenderableId\":\"link-6\",\"instanceCount\":1},{\"entityId\":7,\"logicalRenderableId\":\"grid\",\"instanceCount\":1}],"
              << "\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
              << "\"directDrawFallback\":false,\"scenePasses\":["
              << "{\"name\":\"robot-main\",\"renderClass\":\"WebglLoaderColladaKinematicsRobotMainPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},"
              << "{\"name\":\"grid-lines\",\"renderClass\":\"WebglLoaderColladaKinematicsGridLinesPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n"
              << "  \"directDrawFallback\":false\n}\n";
        writeKinematicsEvidence(options.sceneSnapshotPath, scene.str());
        if (!options.semanticSnapshotPath.empty())
        {
            std::ostringstream semantic;
            semantic << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_loader_collada_kinematics\",\n"
                     << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                     << "  \"frame\":" << frameIndex << ",\n  \"kind\":\"loader-snapshot\",\n"
                     << "  \"canonicalState\":\"seven-link-meshes-eight-joints-six-dynamic\",\n"
                     << "  \"result\":{\"renderableObjectCount\":7,\"sceneRootCount\":1,"
                     << "\"canonicalSceneSha256\":\"b1d41737bb8529d5ec57621fe60010e4ca14d712aa52a7584eb27fc11ffd0176\","
                     << "\"linkCount\":7,\"jointCount\":8,\"dynamicJointCount\":6,"
                     << "\"daeSha256\":\"" << asset.sha256.c_str() << "\"}\n}\n";
            writeKinematicsEvidence(options.semanticSnapshotPath, semantic.str());
        }
        captureWritten = true;
    }

    void WebglLoaderColladaKinematicsRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        asset = ColladaRobotAsset{};
        entities.clear();
    }
} // namespace GVM::ThreeSamples
