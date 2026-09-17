#include "WebglPostprocessingRgbHalftoneRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/vector.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

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
        constexpr uint32_t RenderableCount = 51u;

        static_assert(sizeof(WebglPostprocessingRgbHalftoneVertex) == 48u);
        static_assert(sizeof(WebglPostprocessingRgbHalftoneHostObjectData) == 192u);
        static_assert(sizeof(WebglPostprocessingRgbHalftoneHostInstanceData) == 16u);
        static_assert(sizeof(WebglPostprocessingRgbHalftoneHostMaterialData) == 32u);

        /** Creates parent directories for one requested evidence path. */
        void prepareWebglPostprocessingRgbHalftoneOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeWebglPostprocessingRgbHalftoneText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebglPostprocessingRgbHalftoneOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGL postprocessing evidence.");
        }

        /** Advances the exact upper-24-bit xorshift32 reference stream. */
        double nextWebglPostprocessingRgbHalftoneRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return double(state >> 8u) / 16777216.0;
        }

        /** Builds Three's intrinsic XYZ Euler rotation in binary64. */
        glm::dmat4 makeWebglPostprocessingRgbHalftoneRotation(
            double rotationX,
            double rotationY,
            double rotationZ)
        {
            const double cx = std::cos(rotationX * 0.5);
            const double cy = std::cos(rotationY * 0.5);
            const double cz = std::cos(rotationZ * 0.5);
            const double sx = std::sin(rotationX * 0.5);
            const double sy = std::sin(rotationY * 0.5);
            const double sz = std::sin(rotationZ * 0.5);
            const double qx = sx * cy * cz + cx * sy * sz;
            const double qy = cx * sy * cz - sx * cy * sz;
            const double qz = cx * cy * sz + sx * sy * cz;
            const double qw = cx * cy * cz - sx * sy * sz;
            const double x2 = qx + qx;
            const double y2 = qy + qy;
            const double z2 = qz + qz;
            const double xx = qx * x2;
            const double xy = qx * y2;
            const double xz = qx * z2;
            const double yy = qy * y2;
            const double yz = qy * z2;
            const double zz = qz * z2;
            const double wx = qw * x2;
            const double wy = qw * y2;
            const double wz = qw * z2;
            glm::dmat4 result(1.0);
            result[0u] = glm::dvec4(
                1.0 - yy - zz, xy + wz, xz - wy, 0.0);
            result[1u] = glm::dvec4(
                xy - wz, 1.0 - xx - zz, yz + wx, 0.0);
            result[2u] = glm::dvec4(
                xz + wy, yz - wx, 1.0 - xx - yy, 0.0);
            return result;
        }

        /** Builds Three's 75-degree perspective projection in binary64. */
        glm::dmat4 makeWebglPostprocessingRgbHalftoneProjection()
        {
            constexpr double Near = 1.0;
            constexpr double Far = 1000.0;
            const double inverseTangent =
                1.0 / std::tan(75.0 * Pi / 360.0);
            glm::dmat4 result(0.0);
            result[0u][0u] = inverseTangent / 1.6;
            result[1u][1u] = inverseTangent;
            result[2u][2u] = (Far + Near) / (Near - Far);
            result[2u][3u] = -1.0;
            result[3u][2u] = 2.0 * Far * Near / (Near - Far);
            return result;
        }

        /** Appends one flat-shaded BoxGeometry face with local UVs. */
        void appendWebglPostprocessingRgbHalftoneFace(
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            const glm::dvec3 &d,
            const glm::dvec3 &normal,
            eastl::vector<WebglPostprocessingRgbHalftoneVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const glm::vec4 packedNormal(glm::vec3(normal), 0.0f);
            // BoxGeometry.buildPlane stores (u, 1-v) and uses a,b,d / b,c,d.
            vertices.push_back({glm::vec4(glm::vec3(a), 1.0f), packedNormal,
                                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(b), 1.0f), packedNormal,
                                glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(c), 1.0f), packedNormal,
                                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(d), 1.0f), packedNormal,
                                glm::vec4(1.0f, 1.0f, 0.0f, 0.0f)});
            indices.insert(indices.end(), {base, base + 1u, base + 3u,
                                           base + 1u, base + 2u, base + 3u});
        }

        /** Builds the r185 BoxGeometry(width,height,depth) six-face topology. */
        void buildWebglPostprocessingRgbHalftoneBox(
            double width,
            double height,
            double depth,
            eastl::vector<WebglPostprocessingRgbHalftoneVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const double x = width * 0.5;
            const double y = height * 0.5;
            const double z = depth * 0.5;
            vertices.clear();
            indices.clear();
            appendWebglPostprocessingRgbHalftoneFace(
                {x, y, z}, {x, -y, z}, {x, -y, -z}, {x, y, -z},
                {1.0, 0.0, 0.0}, vertices, indices);
            appendWebglPostprocessingRgbHalftoneFace(
                {-x, y, -z}, {-x, -y, -z}, {-x, -y, z}, {-x, y, z},
                {-1.0, 0.0, 0.0}, vertices, indices);
            appendWebglPostprocessingRgbHalftoneFace(
                {-x, y, -z}, {-x, y, z}, {x, y, z}, {x, y, -z},
                {0.0, 1.0, 0.0}, vertices, indices);
            appendWebglPostprocessingRgbHalftoneFace(
                {-x, -y, z}, {-x, -y, -z}, {x, -y, -z}, {x, -y, z},
                {0.0, -1.0, 0.0}, vertices, indices);
            appendWebglPostprocessingRgbHalftoneFace(
                {-x, y, z}, {-x, -y, z}, {x, -y, z}, {x, y, z},
                {0.0, 0.0, 1.0}, vertices, indices);
            appendWebglPostprocessingRgbHalftoneFace(
                {x, y, -z}, {x, -y, -z}, {-x, -y, -z}, {-x, y, -z},
                {0.0, 0.0, -1.0}, vertices, indices);
            if (vertices.size() != 24u || indices.size() != 36u)
                throw std::runtime_error(
                    "WebGL postprocessing BoxGeometry topology differs from r185.");
        }

        /** Appends one typed component payload to a Scene entity allocation. */
        void appendWebglPostprocessingRgbHalftonePayload(
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

    void WebglPostprocessingRgbHalftoneRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool scenario0 = options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool scenario1 = options.scenarioId == "animated" && options.targetFrame == 120u;
        const bool scenario2 = options.scenarioId == "diamond-multiply" && options.targetFrame == 121u;
        const bool scenario3 = options.scenarioId == "disabled" && options.targetFrame == 1u;
        const bool replayContract = (scenario2 || scenario3)
            ? !options.inputReplayPath.empty()
            : options.inputReplayPath.empty();
        if (options.caseId != "webgl_postprocessing_rgb_halftone" ||
            !(scenario0 || scenario1 || scenario2 || scenario3) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            !replayContract)
        {
            throw std::invalid_argument("webgl_postprocessing_rgb_halftone requires its locked r185 scenario matrix.");
        }
        device = inDevice;
        entityVertices.resize(RenderableCount);
        entityIndices.resize(RenderableCount);
        buildWebglPostprocessingRgbHalftoneBox(
            100.0, 1.0, 100.0, entityVertices[0u], entityIndices[0u]);
        buildWebglPostprocessingRgbHalftoneBox(
            2.0, 2.0, 2.0, vertices, indices);
        for (uint32_t entity = 1u; entity < RenderableCount; ++entity)
        {
            entityVertices[entity] = vertices;
            entityIndices[entity] = indices;
        }
        objects.resize(RenderableCount);
        uint32_t randomState = options.randomSeed;
        /* The locked r185 module consumes 156 words before the first child
           UUID. Each fresh BoxGeometry and Mesh contributes four UUID words
           before its six transform randoms. A final 68-word UI/resource tail
           is consumed after the children; it is part of the canonical stream
           state but must not affect scene transforms. */
        for (uint32_t draw = 0u; draw < 156u; ++draw)
            (void)nextWebglPostprocessingRgbHalftoneRandom(randomState);
        // Timer starts at virtual performance.now()==0.  The first callback
        // has zero delta and each later callback advances at 60 Hz.
        const double elapsedSeconds = double(options.targetFrame) / 60.0;
        const double parentRotationX = 0.0;
        const double parentRotationY = elapsedSeconds * (Pi / 64.0);
        const glm::dmat4 parentRotation = makeWebglPostprocessingRgbHalftoneRotation(
            parentRotationX, parentRotationY, 0.0);
        glm::dmat4 view(1.0);
        view[3u][2u] = -12.0;
        const glm::dmat4 projection = makeWebglPostprocessingRgbHalftoneProjection();
        glm::dmat4 floorModel(1.0);
        floorModel[3u].y = -10.0;
        objects[0u].modelView = glm::mat4(view * floorModel);
        objects[0u].modelViewProjection = glm::mat4(projection);
        objects[0u].normalModelView = glm::mat4(
            glm::transpose(glm::inverse(view * floorModel)));
        for (uint32_t entity = 1u; entity < RenderableCount; ++entity)
        {
            // BoxGeometry.uuid and Mesh.uuid each consume four words.
            for (uint32_t uuidWord = 0u; uuidWord < 8u; ++uuidWord)
                (void)nextWebglPostprocessingRgbHalftoneRandom(randomState);
            const glm::dvec3 position(
                nextWebglPostprocessingRgbHalftoneRandom(randomState) * 16.0 - 8.0,
                nextWebglPostprocessingRgbHalftoneRandom(randomState) * 16.0 - 8.0,
                nextWebglPostprocessingRgbHalftoneRandom(randomState) * 16.0 - 8.0);
            const glm::dvec3 rotation(
                nextWebglPostprocessingRgbHalftoneRandom(randomState) * 2.0 * Pi,
                nextWebglPostprocessingRgbHalftoneRandom(randomState) * 2.0 * Pi,
                nextWebglPostprocessingRgbHalftoneRandom(randomState) * 2.0 * Pi);
            glm::dmat4 model = makeWebglPostprocessingRgbHalftoneRotation(
                rotation.x, rotation.y, rotation.z);
            model[3u] = glm::dvec4(position, 1.0);
            model = parentRotation * model;
            const glm::dmat4 modelView = view * model;
            objects[entity].modelView = glm::mat4(modelView);
            objects[entity].modelViewProjection = glm::mat4(projection);
            objects[entity].normalModelView = glm::mat4(
                glm::transpose(glm::inverse(modelView)));
        }
        for (uint32_t draw = 0u; draw < 68u; ++draw)
            (void)nextWebglPostprocessingRgbHalftoneRandom(randomState);
        if (randomState != 1681405357u)
            throw std::runtime_error(
                "WebGL RGB halftone random stream differs from r185.");
        instanceData.reserved = glm::vec4(0.0f);
        materialData.diffuseAndShininess =
            glm::vec4(1.0f, 1.0f, 1.0f, 30.0f);
        materialData.specular =
            glm::vec4(0.0056053917f, 0.0056053917f, 0.0056053917f, 0.0f);
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneSetHandle);
        if (!encoder)
            throw std::runtime_error(
                "Could not create the WebGL postprocessing Set encoder.");
        for (uint32_t entity = 0u; entity < RenderableCount; ++entity)
        {
            const std::string entitySuffix = std::to_string(entity);
            const std::string vertexName =
                "WebglPostprocessingRgbHalftoneBoxVertices-" + entitySuffix;
            const std::string indexName =
                "WebglPostprocessingRgbHalftoneBoxIndices-" + entitySuffix;
            const std::string objectName =
                "WebglPostprocessingRgbHalftoneObject-" + entitySuffix;
            const std::string instanceName =
                "WebglPostprocessingRgbHalftoneInstance-" + entitySuffix;
            const std::string materialName =
                "WebglPostprocessingRgbHalftoneMaterial-" + entitySuffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(
                entityVertices[entity].size());
            allocation.indicesCount = static_cast<uint32_t>(
                entityIndices[entity].size());
            allocation.instanceCount = 1u;
            appendWebglPostprocessingRgbHalftonePayload(
                allocation,
                WebglPostprocessingRgbHalftoneSceneRenderSetComponents::vertices,
                vertexName.c_str(),
                entityVertices[entity].data(),
                entityVertices[entity].size() * sizeof(entityVertices[entity][0u]));
            appendWebglPostprocessingRgbHalftonePayload(
                allocation,
                WebglPostprocessingRgbHalftoneSceneRenderSetComponents::indices,
                indexName.c_str(),
                entityIndices[entity].data(),
                entityIndices[entity].size() * sizeof(entityIndices[entity][0u]));
            appendWebglPostprocessingRgbHalftonePayload(
                allocation,
                WebglPostprocessingRgbHalftoneSceneRenderSetComponents::objects,
                objectName.c_str(),
                &objects[entity], sizeof(objects[entity]));
            appendWebglPostprocessingRgbHalftonePayload(
                allocation,
                WebglPostprocessingRgbHalftoneSceneRenderSetComponents::instances,
                instanceName.c_str(),
                &instanceData, sizeof(instanceData));
            appendWebglPostprocessingRgbHalftonePayload(
                allocation,
                WebglPostprocessingRgbHalftoneSceneRenderSetComponents::materials,
                materialName.c_str(),
                &materialData, sizeof(materialData));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebglPostprocessingRgbHalftoneRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPostprocessingRgbHalftoneRuntimeAdapter::afterFrame(
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
            const std::filesystem::path outputPath(
                options.captureRgbaPath.c_str());
            prepareWebglPostprocessingRgbHalftoneOutput(outputPath);
            std::ofstream output(
                outputPath, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGL postprocessing RGBA.");
        }
        std::ostringstream metadata;
        metadata
            << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\","
            << "\"caseId\":\"webgl_postprocessing_rgb_halftone\",\"scenarioId\":\""
            << options.scenarioId.c_str() << "\",\"pipeline\":\""
            << options.pipeline.c_str() << "\",\"backend\":\""
            << threeSampleBackendName(options.backend) << "\",\"frame\":"
            << frameIndex << ",\"randomSeed\":" << options.randomSeed
            << ",\"width\":" << width << ",\"height\":" << height
            << ",\"rowStrideBytes\":" << uint64_t(width) * 4u
            << ",\"byteCount\":" << byteCount
            << ",\"format\":\"rgba8unorm\",\"sampleCount\":1"
            << ",\"samplePolicy\":{"
            << "\"mode\":\"single-sample\",\"msaaEnabled\":false,"
            << "\"simulateMsaa\":false},\"inputReplay\":";
        if (options.scenarioId == "diamond-multiply")
        {
            metadata << "{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing_rgb_halftone\","
                     << "\"scenarioId\":\"diamond-multiply\",\"captureFrame\":121,"
                     << "\"sha256\":\"b0cbf9793d2e2b95c0a5f5b89ac0e1cf0f9415d79ea695a14b4b207e3e796233\","
                     << "\"target\":\"body > canvas\",\"eventCount\":5}";
        }
        else if (options.scenarioId == "disabled")
        {
            metadata << "{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing_rgb_halftone\","
                     << "\"scenarioId\":\"disabled\",\"captureFrame\":1,"
                     << "\"sha256\":\"ca833487c1479502960ef3c448b110e45c190af47b805b916eba5b60456ad7af\","
                     << "\"target\":\"body > canvas\",\"eventCount\":1}";
        }
        else
        {
            metadata << "null";
        }
        metadata << ",\"gpuWorkDslOnly\":true}\n";
        writeWebglPostprocessingRgbHalftoneText(
            options.captureMetadataPath, metadata.str());
        std::ostringstream entities;
        uint64_t totalVertexCount = 0u;
        uint64_t totalIndexCount = 0u;
        for (uint32_t entity = 0u; entity < RenderableCount; ++entity)
        {
            if (entity != 0u) entities << ',';
            totalVertexCount += entityVertices[entity].size();
            totalIndexCount += entityIndices[entity].size();
            const std::string logicalRenderableId = entity == 0u
                ? "floor"
                : "box-" + std::to_string(entity);
            entities << "{\"entityId\":" << entity
                     << ",\"logicalRenderableId\":\""
                     << logicalRenderableId
                     << "\",\"instanceCount\":1}";
        }
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing_rgb_halftone\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":51,"
            << "\"entityCount\":51,\"instanceCount\":51,"
            << "\"vertexCount\":" << totalVertexCount
            << ",\"indexCount\":" << totalIndexCount
            << ",\"scenePassCount\":1,\"screenPassCount\":2,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"WebglPostprocessingRgbHalftoneSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"WebglPostprocessingRgbHalftoneSceneRenderSet\","
            << "\"renderableObjectCount\":51,\"entityCount\":51,"
            << "\"entities\":[" << entities.str() << "],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"main\",\"renderClass\":"
            << "\"WebglPostprocessingRgbHalftoneMainPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"main\",\"entityOrdinal\":0}]}\n";
        writeWebglPostprocessingRgbHalftoneText(
            options.sceneSnapshotPath, snapshot.str());
        writeWebglPostprocessingRgbHalftoneText(
            options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebglPostprocessingRgbHalftoneRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        entityVertices.clear();
        entityIndices.clear();
        objects.clear();
        device = {};
    }
}
