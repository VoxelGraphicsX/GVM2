#include "WebglPostprocessingRuntimeAdapter.hpp"

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
        constexpr uint32_t RenderableCount = 100u;

        static_assert(sizeof(WebglPostprocessingVertex) == 32u);
        static_assert(sizeof(WebglPostprocessingHostObjectData) == 192u);
        static_assert(sizeof(WebglPostprocessingHostInstanceData) == 16u);
        static_assert(sizeof(WebglPostprocessingHostMaterialData) == 32u);

        /** Creates parent directories for one requested evidence path. */
        void prepareWebglPostprocessingOutput(
            const std::filesystem::path &path)
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional deterministic text artifact. */
        void writeWebglPostprocessingText(
            const eastl::string &path,
            const std::string &text)
        {
            if (path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareWebglPostprocessingOutput(outputPath);
            std::ofstream output(outputPath, std::ios::trunc);
            output << text;
            if (!output)
                throw std::runtime_error(
                    "Could not write WebGL postprocessing evidence.");
        }

        /** Advances the exact upper-24-bit xorshift32 reference stream. */
        double nextWebglPostprocessingRandom(uint32_t &state)
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            return double(state >> 8u) / 16777216.0;
        }

        /** Builds Three's intrinsic XYZ Euler rotation in binary64. */
        glm::dmat4 makeWebglPostprocessingRotation(
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

        /** Builds Three's 70-degree perspective projection in binary64. */
        glm::dmat4 makeWebglPostprocessingProjection()
        {
            constexpr double Near = 1.0;
            constexpr double Far = 1000.0;
            const double inverseTangent =
                1.0 / std::tan(70.0 * Pi / 360.0);
            glm::dmat4 result(0.0);
            result[0u][0u] = inverseTangent / 1.6;
            result[1u][1u] = inverseTangent;
            result[2u][2u] = (Far + Near) / (Near - Far);
            result[2u][3u] = -1.0;
            result[3u][2u] = 2.0 * Far * Near / (Near - Far);
            return result;
        }

        /** Appends one expanded triangle with its exact flat face normal. */
        void appendWebglPostprocessingTriangle(
            const glm::dvec3 &a,
            const glm::dvec3 &b,
            const glm::dvec3 &c,
            eastl::vector<WebglPostprocessingVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            const glm::dvec3 normal = glm::normalize(glm::cross(b - a, c - a));
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({glm::vec4(glm::vec3(a), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(b), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f)});
            vertices.push_back({glm::vec4(glm::vec3(c), 1.0f),
                                glm::vec4(glm::vec3(normal), 0.0f)});
            indices.insert(indices.end(), {base, base + 1u, base + 2u});
        }

        /** Builds exact expanded SphereGeometry(1,4,4) topology. */
        void buildWebglPostprocessingSphere(
            eastl::vector<WebglPostprocessingVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            glm::dvec3 grid[5u][5u];
            for (uint32_t y = 0u; y <= 4u; ++y)
            {
                const double v = double(y) / 4.0;
                const double theta = v * Pi;
                const double verticalPosition = std::cos(theta);
                const double ringRadius = std::sqrt(
                    1.0 - verticalPosition * verticalPosition);
                for (uint32_t x = 0u; x <= 4u; ++x)
                {
                    const double u = double(x) / 4.0;
                    const double phi = u * Pi * 2.0;
                    grid[y][x] = glm::dvec3(
                        -ringRadius * std::cos(phi),
                        verticalPosition,
                        ringRadius * std::sin(phi));
                }
            }
            vertices.clear();
            indices.clear();
            for (uint32_t y = 0u; y < 4u; ++y)
            {
                for (uint32_t x = 0u; x < 4u; ++x)
                {
                    const glm::dvec3 &a = grid[y][x + 1u];
                    const glm::dvec3 &b = grid[y][x];
                    const glm::dvec3 &c = grid[y + 1u][x];
                    const glm::dvec3 &d = grid[y + 1u][x + 1u];
                    if (y != 0u)
                        appendWebglPostprocessingTriangle(
                            a, b, d, vertices, indices);
                    if (y != 3u)
                        appendWebglPostprocessingTriangle(
                            b, c, d, vertices, indices);
                }
            }
            if (vertices.size() != 72u || indices.size() != 72u)
                throw std::runtime_error(
                    "WebGL postprocessing SphereGeometry topology differs from r185.");
        }

        /** Appends one typed component payload to a Scene entity allocation. */
        void appendWebglPostprocessingPayload(
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

    void WebglPostprocessingRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial =
            options.scenarioId == "initial" && options.targetFrame == 0u;
        const bool animated =
            options.scenarioId == "animated" && options.targetFrame == 120u;
        if (options.caseId != "webgl_postprocessing" ||
            (!initial && !animated) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed ||
            !options.inputReplayPath.empty())
        {
            throw std::invalid_argument(
                "WebGL postprocessing requires its two locked scenarios.");
        }
        device = inDevice;
        buildWebglPostprocessingSphere(vertices, indices);
        objects.resize(RenderableCount);
        uint32_t randomState = options.randomSeed;
        /* The locked r185 module consumes 140 UUID/resource random words
           before the first child Mesh in the current host bootstrap. */
        for (uint32_t draw = 0u; draw < 140u; ++draw)
            (void)nextWebglPostprocessingRandom(randomState);
        // The deterministic runner advances frame + 1 callbacks, including
        // the first animation callback for the nominal frame-zero capture.
        const double renderedFrame = double(options.targetFrame + 1u);
        const double parentRotationX = renderedFrame * 0.005;
        const double parentRotationY = renderedFrame * 0.01;
        const glm::dmat4 parentRotation = makeWebglPostprocessingRotation(
            parentRotationX, parentRotationY, 0.0);
        glm::dmat4 view(1.0);
        view[3u][2u] = -400.0;
        const glm::dmat4 projection = makeWebglPostprocessingProjection();
        for (uint32_t entity = 0u; entity < RenderableCount; ++entity)
        {
            // THREE.Mesh inherits Object3D and consumes four UUID random
            // values before its transform in the locked browser bootstrap.
            for (uint32_t uuidWord = 0u; uuidWord < 4u; ++uuidWord)
                (void)nextWebglPostprocessingRandom(randomState);
            glm::dvec3 direction(
                nextWebglPostprocessingRandom(randomState) - 0.5,
                nextWebglPostprocessingRandom(randomState) - 0.5,
                nextWebglPostprocessingRandom(randomState) - 0.5);
            direction = glm::normalize(direction);
            const glm::dvec3 position = direction *
                (nextWebglPostprocessingRandom(randomState) * 400.0);
            const glm::dvec3 rotation(
                nextWebglPostprocessingRandom(randomState) * 2.0,
                nextWebglPostprocessingRandom(randomState) * 2.0,
                nextWebglPostprocessingRandom(randomState) * 2.0);
            const double scale =
                nextWebglPostprocessingRandom(randomState) * 50.0;
            glm::dmat4 model = makeWebglPostprocessingRotation(
                rotation.x, rotation.y, rotation.z);
            model[0u] *= scale;
            model[1u] *= scale;
            model[2u] *= scale;
            model[3u] = glm::dvec4(position, 1.0);
            model = parentRotation * model;
            const glm::dmat4 modelView = view * model;
            objects[entity].modelView = glm::mat4(modelView);
            objects[entity].modelViewProjection = glm::mat4(projection);
            objects[entity].normalModelView = glm::mat4(
                glm::transpose(glm::inverse(modelView)));
        }
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
                "WebglPostprocessingSphereVertices-" + entitySuffix;
            const std::string indexName =
                "WebglPostprocessingSphereIndices-" + entitySuffix;
            const std::string objectName =
                "WebglPostprocessingObject-" + entitySuffix;
            const std::string instanceName =
                "WebglPostprocessingInstance-" + entitySuffix;
            const std::string materialName =
                "WebglPostprocessingMaterial-" + entitySuffix;
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(indices.size());
            allocation.instanceCount = 1u;
            appendWebglPostprocessingPayload(
                allocation,
                WebglPostprocessingSceneRenderSetComponents::vertices,
                vertexName.c_str(),
                vertices.data(), vertices.size() * sizeof(vertices[0u]));
            appendWebglPostprocessingPayload(
                allocation,
                WebglPostprocessingSceneRenderSetComponents::indices,
                indexName.c_str(),
                indices.data(), indices.size() * sizeof(indices[0u]));
            appendWebglPostprocessingPayload(
                allocation,
                WebglPostprocessingSceneRenderSetComponents::objects,
                objectName.c_str(),
                &objects[entity], sizeof(objects[entity]));
            appendWebglPostprocessingPayload(
                allocation,
                WebglPostprocessingSceneRenderSetComponents::instances,
                instanceName.c_str(),
                &instanceData, sizeof(instanceData));
            appendWebglPostprocessingPayload(
                allocation,
                WebglPostprocessingSceneRenderSetComponents::materials,
                materialName.c_str(),
                &materialData, sizeof(materialData));
            encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneSetHandle, encoder);
    }

    void WebglPostprocessingRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglPostprocessingRuntimeAdapter::afterFrame(
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
            prepareWebglPostprocessingOutput(outputPath);
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
            << "\"caseId\":\"webgl_postprocessing\",\"scenarioId\":\""
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
            << "\"simulateMsaa\":false},\"inputReplay\":null,"
            << "\"gpuWorkDslOnly\":true}\n";
        writeWebglPostprocessingText(
            options.captureMetadataPath, metadata.str());
        std::ostringstream entities;
        for (uint32_t entity = 0u; entity < RenderableCount; ++entity)
        {
            if (entity != 0u) entities << ',';
            entities << "{\"entityId\":" << entity
                     << ",\"logicalRenderableId\":\"sphere-" << entity
                     << "\",\"instanceCount\":1}";
        }
        std::ostringstream snapshot;
        snapshot
            << "{\"schemaVersion\":1,\"caseId\":\"webgl_postprocessing\","
            << "\"scenarioId\":\"" << options.scenarioId.c_str()
            << "\",\"frame\":" << frameIndex
            << ",\"implementationLevel\":\"semantic-complete\","
            << "\"gpuWorkDslOnly\":true,\"renderSetPolicy\":\"required\","
            << "\"sceneRenderSetCount\":1,\"renderableObjectCount\":100,"
            << "\"entityCount\":100,\"instanceCount\":100,"
            << "\"vertexCount\":" << vertices.size() * RenderableCount
            << ",\"indexCount\":" << indices.size() * RenderableCount
            << ",\"scenePassCount\":1,\"screenPassCount\":3,"
            << "\"drawCommandCount\":1,\"renderSetType\":"
            << "\"WebglPostprocessingSceneRenderSet\",\"sceneRoots\":[{"
            << "\"id\":\"scene\",\"renderSetCount\":1,"
            << "\"renderSetId\":\"scene-set\",\"renderSetType\":"
            << "\"WebglPostprocessingSceneRenderSet\","
            << "\"renderableObjectCount\":100,\"entityCount\":100,"
            << "\"entities\":[" << entities.str() << "],"
            << "\"componentSchema\":["
            << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
            << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
            << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
            << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
            << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"}],"
            << "\"drawCommandCount\":1,\"directDrawFallback\":false,"
            << "\"scenePasses\":[{\"name\":\"main\",\"renderClass\":"
            << "\"WebglPostprocessingMainPass\",\"renderSetId\":\"scene-set\","
            << "\"renderSetBindingCount\":1,\"drawMode\":"
            << "\"render-set-indexed-indirect\",\"invocationCount\":1,"
            << "\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,"
            << "\"usesExplicitDrawCount\":false}]}],"
            << "\"scenePassSequence\":[{\"sceneRoot\":\"scene\","
            << "\"scenePass\":\"main\",\"entityOrdinal\":0}]}\n";
        writeWebglPostprocessingText(
            options.sceneSnapshotPath, snapshot.str());
        writeWebglPostprocessingText(
            options.semanticSnapshotPath, snapshot.str());
        captureWritten = true;
    }

    void WebglPostprocessingRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        vertices.clear();
        indices.clear();
        objects.clear();
        device = {};
    }
}
