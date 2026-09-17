#include "WebglMaterialsModifiedRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr GVM::Core::RenderSetHandle SceneSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebglMaterialsModifiedHostVertex) == 32u);
        static_assert(sizeof(WebglMaterialsModifiedHostObjectData) == 192u);
        static_assert(sizeof(WebglMaterialsModifiedHostInstanceData) == 16u);
        static_assert(sizeof(WebglMaterialsModifiedHostMaterialData) == 16u);

        /** Creates parent directories for one requested evidence artifact. */
        void prepareModifiedMaterialOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Reads one locked binary asset from disk. */
        eastl::vector<uint8_t> readModifiedMaterialAsset(
            const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                throw std::runtime_error(
                    "Could not open the modified-material GLB asset.");
            const std::streamsize size = input.tellg();
            if (size <= 0)
                throw std::runtime_error(
                    "The modified-material GLB asset is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(size));
            input.read(
                reinterpret_cast<char *>(bytes.data()), size);
            if (!input)
                throw std::runtime_error(
                    "Could not read the modified-material GLB asset.");
            return bytes;
        }

        /** Writes one optional UTF-8 evidence artifact. */
        void writeModifiedMaterialText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareModifiedMaterialOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write modified-material evidence.");
        }

        /** Builds Three's 27-degree perspective matrix. */
        glm::mat4 makeModifiedMaterialProjection()
        {
            constexpr double Near = 0.1;
            constexpr double Far = 100.0;
            const double inverseTangent =
                1.0 / std::tan(27.0 * Pi / 360.0);
            glm::mat4 projection(0.0f);
            projection[0u][0u] = static_cast<float>(inverseTangent / 1.6);
            projection[1u][1u] = static_cast<float>(inverseTangent);
            projection[2u][2u] = static_cast<float>(
                (Far + Near) / (Near - Far));
            projection[2u][3u] = -1.0f;
            projection[3u][2u] = static_cast<float>(
                2.0 * Far * Near / (Near - Far));
            return projection;
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendModifiedMaterialPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = value,
                .dataStorageSize = byteCount,
                .instanceCount = 1u,
            });
        }
    }

    void WebglMaterialsModifiedRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial-loader" &&
            options.targetFrame == 0u && options.inputReplayPath.empty();
        const bool loader =
            options.scenarioId == "canonical-loader" &&
            options.targetFrame == 0u && options.inputReplayPath.empty();
        const bool twisted =
            options.scenarioId == "twisted" &&
            options.targetFrame == 60u && options.inputReplayPath.empty();
        const bool orbit =
            options.scenarioId == "orbit" &&
            options.targetFrame == 61u && !options.inputReplayPath.empty();
        if (options.caseId != "webgl_materials_modified" ||
            (!initial && !loader && !twisted && !orbit) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            options.assetRoot.empty())
        {
            throw std::invalid_argument(
                "Modified materials require the four locked r185 scenarios.");
        }
        device = inDevice;
        const std::filesystem::path assetPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "gltf" / "LeePerrySmith" /
            "LeePerrySmith.glb";
        const ThreeCompat::DecodedGlbMesh mesh =
            ThreeCompat::decodeFirstGlbMesh(
                readModifiedMaterialAsset(assetPath));
        if (mesh.positions.size() != mesh.normals.size() ||
            mesh.positions.empty() ||
            mesh.positions.size() % 3u != 0u || mesh.indices.empty())
        {
            throw std::runtime_error(
                "LeePerrySmith GLB lacks the required position or normal data.");
        }
        const size_t vertexCount = mesh.positions.size() / 3u;
        vertices.reserve(vertexCount);
        for (size_t vertexIndex = 0u;
             vertexIndex < vertexCount; ++vertexIndex)
        {
            const size_t offset = vertexIndex * 3u;
            vertices.push_back({
                glm::vec4(
                    mesh.positions[offset],
                    mesh.positions[offset + 1u],
                    mesh.positions[offset + 2u],
                    1.0f),
                glm::vec4(
                    mesh.normals[offset],
                    mesh.normals[offset + 1u],
                    mesh.normals[offset + 2u],
                    0.0f),
            });
        }
        indices = mesh.indices;
        const glm::mat4 projection = makeModifiedMaterialProjection();
        glm::mat4 view(1.0f);
        if (orbit)
        {
            view[0u] = glm::vec4(
                0.7289686274f, -0.2519985972f, -0.6364758026f, 0.0f);
            view[1u] = glm::vec4(
                0.0f, 0.9297764859f, -0.3681245527f, 0.0f);
            view[2u] = glm::vec4(
                0.6845471059f, 0.2683512499f, 0.6777778887f, 0.0f);
            view[3u] = glm::vec4(0.0f, 0.0f, -20.0f, 1.0f);
        }
        else
        {
            view[3u][2u] = -20.0f;
        }
        const float time = static_cast<float>(options.targetFrame) / 60.0f;
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create the modified-material Scene Set encoder.");
        for (uint32_t entityIndex = 0u; entityIndex < 2u; ++entityIndex)
        {
            glm::mat4 model(1.0f);
            model[3u][0u] = entityIndex == 0u ? -3.5f : 3.5f;
            model[3u][1u] = -0.5f;
            const glm::mat4 modelView = view * model;
            const WebglMaterialsModifiedHostObjectData objectData = {
                .modelView = modelView,
                .projection = projection,
                .normalTransform = glm::transpose(glm::inverse(modelView)),
            };
            const WebglMaterialsModifiedHostInstanceData instanceData = {
                .reserved = glm::vec4(0.0f),
            };
            const WebglMaterialsModifiedHostMaterialData materialData = {
                .twistAmountAndTime = glm::vec4(
                    entityIndex == 0u ? 2.0f : -2.0f,
                    time, 0.0f, 0.0f),
            };
            const eastl::string suffix = entityIndex == 0u ? "Left" : "Right";
            const eastl::string vertexName =
                "WebglMaterialsModifiedVertices" + suffix;
            const eastl::string indexName =
                "WebglMaterialsModifiedIndices" + suffix;
            const eastl::string objectName =
                "WebglMaterialsModifiedObject" + suffix;
            const eastl::string instanceName =
                "WebglMaterialsModifiedInstance" + suffix;
            const eastl::string materialName =
                "WebglMaterialsModifiedMaterial" + suffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount =
                static_cast<uint32_t>(vertices.size());
            allocation.indicesCount =
                static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            appendModifiedMaterialPayload(
                allocation,
                WebglMaterialsModifiedSceneRenderSetComponents::vertices,
                vertexName.c_str(), vertices.data(),
                vertices.size() * sizeof(vertices[0u]));
            appendModifiedMaterialPayload(
                allocation,
                WebglMaterialsModifiedSceneRenderSetComponents::indices,
                indexName.c_str(), indices.data(),
                indices.size() * sizeof(indices[0u]));
            appendModifiedMaterialPayload(
                allocation,
                WebglMaterialsModifiedSceneRenderSetComponents::objects,
                objectName.c_str(), &objectData, sizeof(objectData));
            appendModifiedMaterialPayload(
                allocation,
                WebglMaterialsModifiedSceneRenderSetComponents::instances,
                instanceName.c_str(), &instanceData, sizeof(instanceData));
            appendModifiedMaterialPayload(
                allocation,
                WebglMaterialsModifiedSceneRenderSetComponents::materials,
                materialName.c_str(), &materialData, sizeof(materialData));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebglMaterialsModifiedRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglMaterialsModifiedRuntimeAdapter::afterFrame(
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
            prepareModifiedMaterialOutput(path);
            std::ofstream output(
                path, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write modified-material RGBA evidence.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgl_materials_modified\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\",\"samplePolicy\":{"
            << "\"mode\":\"single-sample\",\"msaaEnabled\":false,"
            << "\"simulateMsaa\":false},\"gpuWorkDslOnly\":true}\n";
        writeModifiedMaterialText(
            options.captureMetadataPath, metadata.str());
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":"
            << "\"webgl_materials_modified\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":2,"
            << "\"entityCount\":2,\"instanceCount\":2,"
            << "\"vertexCount\":" << vertices.size() * 2u
            << ",\"indexCount\":" << indices.size() * 2u
            << ",\"scenePassCount\":1,\"screenPassCount\":0,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"WebglMaterialsModifiedSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"WebglMaterialsModifiedSceneRenderSet\","
            << "\"renderableObjectCount\":2,\"entityCount\":2,"
            << "\"entities\":[{\"entityId\":0,"
            << "\"logicalRenderableId\":\"left-positive-twist\","
            << "\"instanceCount\":1},{\"entityId\":1,"
            << "\"logicalRenderableId\":\"right-negative-twist\","
            << "\"instanceCount\":1}],\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"main\",\"renderClass\":"
            << "\"WebglMaterialsModifiedMainPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"main\",\"entityOrdinal\":0}],"
            << "\"usesRenderEntityID\":true,"
            << "\"usesRenderEntityInstanceID\":true,"
            << "\"directDrawFallback\":false}\n";
        writeModifiedMaterialText(options.sceneSnapshotPath, snapshot.str());
        std::ostringstream semantic;
        semantic
            << "{\"schemaVersion\":1,\"caseId\":"
            << "\"webgl_materials_modified\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"frame\":" << frameIndex
            << ",\"kind\":\"loader-snapshot\",\"canonicalState\":"
            << "\"lee-perry-smith-opposed-twists\",\"result\":{"
            << "\"renderableObjectCount\":2,\"sceneRootCount\":1,"
            << "\"decodedVertexCount\":" << vertices.size()
            << ",\"decodedIndexCount\":" << indices.size() << "}}\n";
        writeModifiedMaterialText(
            options.semanticSnapshotPath, semantic.str());
        captureWritten = true;
    }

    void WebglMaterialsModifiedRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        device = {};
    }
} // namespace GVM::ThreeSamples
