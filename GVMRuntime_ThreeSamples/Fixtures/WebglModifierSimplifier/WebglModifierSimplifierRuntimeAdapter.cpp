#include "WebglModifierSimplifierRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/string.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t ComparisonEntityCount = 2u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;
        constexpr float Pi = 3.14159265358979323846f;

        /** Stores one active vertex in the CPU copy of SimplifyModifier. */
        struct SimplifierVertex
        {
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 normal = glm::vec3(0.0f);
            eastl::vector<int32_t> faces;
            eastl::vector<int32_t> neighbors;
            bool active = true;
            float collapseCost = 0.0f;
            int32_t collapseNeighbor = -1;
        };

        /** Stores one triangle and its current normal during edge collapse. */
        struct SimplifierFace
        {
            int32_t vertices[3] = {-1, -1, -1};
            glm::vec3 normal = glm::vec3(0.0f);
            bool active = true;
        };

        /** Reads one complete bounded binary asset from the locked asset pack. */
        eastl::vector<uint8_t> readModifierAsset(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) throw std::runtime_error("Could not open the modifier asset.");
            const std::streamoff byteCount = input.tellg();
            if (byteCount <= 0) throw std::runtime_error("The modifier asset is empty.");
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error("Could not read the complete modifier asset.");
            return bytes;
        }

        /** Adds one integer to a small adjacency list exactly once. */
        void addUniqueIndex(eastl::vector<int32_t> &values, int32_t value)
        {
            for (const int32_t existing : values)
                if (existing == value) return;
            values.push_back(value);
        }

        /** Removes one integer from a small adjacency list when present. */
        void removeIndex(eastl::vector<int32_t> &values, int32_t value)
        {
            for (size_t index = 0u; index < values.size(); ++index)
            {
                if (values[index] == value)
                {
                    values.erase(values.begin() + static_cast<ptrdiff_t>(index));
                    return;
                }
            }
        }

        /** Recomputes a triangle normal using the source modifier cross-product order. */
        void updateSimplifierFaceNormal(
            SimplifierFace &face,
            const eastl::vector<SimplifierVertex> &vertices)
        {
            const glm::vec3 &a = vertices[face.vertices[0]].position;
            const glm::vec3 &b = vertices[face.vertices[1]].position;
            const glm::vec3 &c = vertices[face.vertices[2]].position;
            face.normal = glm::normalize(glm::cross(c - b, a - b));
        }

        /** Returns whether an active face contains both requested vertices. */
        bool simplifierFaceHasEdge(
            const SimplifierFace &face,
            int32_t first,
            int32_t second)
        {
            bool hasFirst = false;
            bool hasSecond = false;
            for (const int32_t vertex : face.vertices)
            {
                hasFirst = hasFirst || vertex == first;
                hasSecond = hasSecond || vertex == second;
            }
            return hasFirst && hasSecond;
        }

        /** Removes one face and updates the incident vertex adjacency lists. */
        void removeSimplifierFace(
            int32_t faceIndex,
            eastl::vector<SimplifierFace> &faces,
            eastl::vector<SimplifierVertex> &vertices)
        {
            SimplifierFace &face = faces[faceIndex];
            if (!face.active) return;
            face.active = false;
            for (const int32_t vertex : face.vertices)
                removeIndex(vertices[vertex].faces, faceIndex);
            for (uint32_t edge = 0u; edge < 3u; ++edge)
            {
                const int32_t first = face.vertices[edge];
                const int32_t second = face.vertices[(edge + 1u) % 3u];
                bool stillConnected = false;
                for (const int32_t candidate : vertices[first].faces)
                {
                    if (faces[candidate].active &&
                        simplifierFaceHasEdge(faces[candidate], first, second))
                    {
                        stillConnected = true;
                        break;
                    }
                }
                if (!stillConnected)
                {
                    removeIndex(vertices[first].neighbors, second);
                    removeIndex(vertices[second].neighbors, first);
                }
            }
        }

        /** Replaces one vertex in a face and rebuilds its local topology. */
        void replaceSimplifierFaceVertex(
            int32_t faceIndex,
            int32_t oldVertex,
            int32_t newVertex,
            eastl::vector<SimplifierFace> &faces,
            eastl::vector<SimplifierVertex> &vertices)
        {
            SimplifierFace &face = faces[faceIndex];
            for (int32_t &vertex : face.vertices)
                if (vertex == oldVertex) vertex = newVertex;
            removeIndex(vertices[oldVertex].faces, faceIndex);
            addUniqueIndex(vertices[newVertex].faces, faceIndex);
            for (uint32_t edge = 0u; edge < 3u; ++edge)
            {
                const int32_t first = face.vertices[edge];
                const int32_t second = face.vertices[(edge + 1u) % 3u];
                addUniqueIndex(vertices[first].neighbors, second);
                addUniqueIndex(vertices[second].neighbors, first);
            }
            updateSimplifierFaceNormal(face, vertices);
        }

        /** Calculates the progressive-mesh edge collapse cost from r185. */
        float computeSimplifierEdgeCost(
            int32_t first,
            int32_t second,
            const eastl::vector<SimplifierFace> &faces,
            const eastl::vector<SimplifierVertex> &vertices)
        {
            const float edgeLength = glm::length(
                vertices[second].position - vertices[first].position);
            eastl::vector<int32_t> sideFaces;
            for (const int32_t faceIndex : vertices[first].faces)
                if (faces[faceIndex].active &&
                    simplifierFaceHasEdge(faces[faceIndex], first, second))
                    sideFaces.push_back(faceIndex);
            float curvature = 0.0f;
            for (const int32_t faceIndex : vertices[first].faces)
            {
                if (!faces[faceIndex].active) continue;
                float minimumCurvature = 1.0f;
                for (const int32_t sideFace : sideFaces)
                {
                    const float dotProduct = glm::dot(
                        faces[faceIndex].normal,
                        faces[sideFace].normal);
                    minimumCurvature = glm::min(
                        minimumCurvature,
                        (1.001f - dotProduct) * 0.5f);
                }
                curvature = glm::max(curvature, minimumCurvature);
            }
            if (sideFaces.size() < 2u) curvature = 1.0f;
            return edgeLength * curvature;
        }

        /** Recomputes the least-cost collapse edge for one active vertex. */
        void computeSimplifierVertexCost(
            int32_t vertexIndex,
            const eastl::vector<SimplifierFace> &faces,
            eastl::vector<SimplifierVertex> &vertices)
        {
            SimplifierVertex &vertex = vertices[vertexIndex];
            if (!vertex.active || vertex.neighbors.empty())
            {
                vertex.collapseNeighbor = -1;
                vertex.collapseCost = -0.01f;
                return;
            }
            float totalCost = 0.0f;
            uint32_t costCount = 0u;
            float minimumCost = 0.0f;
            int32_t bestNeighbor = -1;
            for (const int32_t neighbor : vertex.neighbors)
            {
                if (!vertices[neighbor].active) continue;
                const float cost = computeSimplifierEdgeCost(
                    vertexIndex, neighbor, faces, vertices);
                if (bestNeighbor < 0)
                {
                    bestNeighbor = neighbor;
                    minimumCost = cost;
                }
                if (cost < minimumCost)
                {
                    bestNeighbor = neighbor;
                    minimumCost = cost;
                }
                totalCost += cost;
                ++costCount;
            }
            vertex.collapseNeighbor = bestNeighbor;
            vertex.collapseCost = costCount == 0u ? -0.01f : totalCost / float(costCount);
        }

        /** Applies one in-place progressive mesh edge collapse. */
        void collapseSimplifierEdge(
            int32_t vertexIndex,
            int32_t neighborIndex,
            eastl::vector<SimplifierFace> &faces,
            eastl::vector<SimplifierVertex> &vertices)
        {
            if (neighborIndex < 0)
            {
                vertices[vertexIndex].active = false;
                return;
            }
            SimplifierVertex &vertex = vertices[vertexIndex];
            SimplifierVertex &neighbor = vertices[neighborIndex];
            neighbor.normal = glm::normalize(neighbor.normal + vertex.normal);
            const eastl::vector<int32_t> oldNeighbors = vertex.neighbors;
            for (int32_t faceIndex = static_cast<int32_t>(vertex.faces.size()) - 1;
                 faceIndex >= 0;
                 --faceIndex)
            {
                const int32_t actualFace = vertex.faces[static_cast<size_t>(faceIndex)];
                if (faces[actualFace].active &&
                    simplifierFaceHasEdge(faces[actualFace], vertexIndex, neighborIndex))
                    removeSimplifierFace(actualFace, faces, vertices);
            }
            for (int32_t faceIndex = static_cast<int32_t>(vertex.faces.size()) - 1;
                 faceIndex >= 0;
                 --faceIndex)
            {
                const int32_t actualFace = vertex.faces[static_cast<size_t>(faceIndex)];
                if (faces[actualFace].active)
                    replaceSimplifierFaceVertex(
                        actualFace, vertexIndex, neighborIndex, faces, vertices);
            }
            vertex.active = false;
            for (const int32_t oldNeighbor : oldNeighbors)
                removeIndex(vertices[oldNeighbor].neighbors, vertexIndex);
            vertex.faces.clear();
            vertex.neighbors.clear();
            for (const int32_t oldNeighbor : oldNeighbors)
                if (vertices[oldNeighbor].active)
                    computeSimplifierVertexCost(oldNeighbor, faces, vertices);
        }

        /** Runs the locked SimplifyModifier reduction and emits a triangle mesh. */
        void buildSimplifiedMesh(
            const ThreeCompat::DecodedGlbMesh &source,
            eastl::vector<WebglModifierSimplifierHostVertex> &outVertices,
            eastl::vector<uint32_t> &outIndices)
        {
            eastl::vector<SimplifierVertex> vertices(source.positions.size() / 3u);
            for (size_t index = 0u; index < vertices.size(); ++index)
            {
                vertices[index].position = glm::vec3(
                    source.positions[index * 3u],
                    source.positions[index * 3u + 1u],
                    source.positions[index * 3u + 2u]);
                vertices[index].normal = glm::normalize(glm::vec3(
                    source.normals[index * 3u],
                    source.normals[index * 3u + 1u],
                    source.normals[index * 3u + 2u]));
            }
            eastl::vector<SimplifierFace> faces(source.indices.size() / 3u);
            for (size_t index = 0u; index < faces.size(); ++index)
            {
                faces[index].vertices[0] = static_cast<int32_t>(source.indices[index * 3u]);
                faces[index].vertices[1] = static_cast<int32_t>(source.indices[index * 3u + 1u]);
                faces[index].vertices[2] = static_cast<int32_t>(source.indices[index * 3u + 2u]);
                updateSimplifierFaceNormal(faces[index], vertices);
                for (const int32_t vertex : faces[index].vertices)
                    addUniqueIndex(vertices[vertex].faces, static_cast<int32_t>(index));
                for (uint32_t edge = 0u; edge < 3u; ++edge)
                {
                    const int32_t first = faces[index].vertices[edge];
                    const int32_t second = faces[index].vertices[(edge + 1u) % 3u];
                    addUniqueIndex(vertices[first].neighbors, second);
                    addUniqueIndex(vertices[second].neighbors, first);
                }
            }
            for (int32_t index = 0; index < static_cast<int32_t>(vertices.size()); ++index)
                computeSimplifierVertexCost(index, faces, vertices);
            const uint32_t removeCount = static_cast<uint32_t>(vertices.size() * 0.875f);
            for (uint32_t step = 0u; step < removeCount; ++step)
            {
                int32_t minimum = -1;
                for (int32_t index = 0; index < static_cast<int32_t>(vertices.size()); ++index)
                    if (vertices[index].active &&
                        (minimum < 0 || vertices[index].collapseCost < vertices[minimum].collapseCost))
                        minimum = index;
                if (minimum < 0) break;
                collapseSimplifierEdge(
                    minimum,
                    vertices[minimum].collapseNeighbor,
                    faces,
                    vertices);
            }
            // The example enables flatShading on the reduced material. Emit
            // one face-local copy per surviving triangle so the DSL receives
            // the exact derivative-equivalent face normal without requiring a
            // new shader interpolation qualifier.
            for (const SimplifierFace &face : faces)
            {
                if (!face.active) continue;
                if (!vertices[face.vertices[0]].active ||
                    !vertices[face.vertices[1]].active ||
                    !vertices[face.vertices[2]].active ||
                    face.vertices[0] == face.vertices[1] ||
                    face.vertices[1] == face.vertices[2] ||
                    face.vertices[2] == face.vertices[0])
                    continue;
                const uint32_t base = static_cast<uint32_t>(outVertices.size());
                for (const int32_t vertex : face.vertices)
                    outVertices.push_back({
                        glm::vec4(vertices[vertex].position, 1.0f),
                        glm::vec4(glm::normalize(face.normal), 0.0f)});
                outIndices.insert(outIndices.end(), {base, base + 1u, base + 2u});
            }
        }

        /** Creates a parent directory for one comparison capture. */
        void preparePath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Appends one typed payload to a RenderSet allocation. */
        void appendBuffer(GVM::Core::RenderSetAllocInfo &allocation,
                          GVM::Core::RenderComponentHandle component,
                          const eastl::string &name,
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

        /** Builds the exact OpenGL projection used by the r185 example camera. */
        glm::mat4 makeModifierProjection()
        {
            constexpr float nearDistance = 1.0f;
            constexpr float farDistance = 1000.0f;
            const float focalLength = 1.0f / std::tan(20.0f * Pi / 180.0f);
            glm::mat4 projection(0.0f);
            projection[0][0] = focalLength / (800.0f / 500.0f);
            projection[1][1] = focalLength;
            projection[2][2] = -(farDistance + nearDistance) /
                               (farDistance - nearDistance);
            projection[2][3] = -1.0f;
            projection[3][2] = -2.0f * farDistance * nearDistance /
                               (farDistance - nearDistance);
            return projection;
        }

        /** Builds one LeePerrySmith model-view matrix for the comparison side. */
        glm::mat4 makeModifierModelView(uint32_t entityIndex)
        {
            const glm::mat4 view = glm::lookAt(
                glm::vec3(0.0f, 0.0f, 15.0f),
                glm::vec3(0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
            const float angle = entityIndex == 0u ? Pi * 0.5f : -Pi * 0.5f;
            const float position = entityIndex == 0u ? -3.0f : 3.0f;
            const glm::mat4 model = glm::translate(
                glm::mat4(1.0f), glm::vec3(position, 0.0f, 0.0f)) *
                glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
            return view * model;
        }

        /** Validates the frozen comparison and orbit scenarios. */
        void validateOptions(const ThreeSampleHostOptions &options)
        {
            const bool initial = options.scenarioId == "initial-comparison" && options.targetFrame == 0u;
            const bool topology = options.scenarioId == "loader-topology" && options.targetFrame == 0u;
            const bool orbit = options.scenarioId == "orbit-controls" && options.targetFrame == 1u;
        if (options.caseId != "webgl_modifier_simplifier" || (!initial && !topology && !orbit) ||
                options.width != 800u || options.height != 500u ||
                options.randomSeed != DefaultThreeRandomSeed || options.assetRoot.empty())
                throw std::invalid_argument("webgl_modifier_simplifier scenario does not match the locked r185 contract.");
        }
    } // namespace

    void WebglModifierSimplifierRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        validateOptions(options);
        device = inDevice;
        entities.clear();
        entities.resize(ComparisonEntityCount);
        const std::filesystem::path assetPath =
            std::filesystem::path(options.assetRoot.c_str()) /
            "models" / "gltf" / "LeePerrySmith" / "LeePerrySmith.glb";
        const ThreeCompat::DecodedGlbMesh source =
            ThreeCompat::decodeFirstGlbMesh(readModifierAsset(assetPath));
        if (source.positions.size() != 9279u * 3u ||
            source.normals.size() != 9279u * 3u ||
            source.indices.size() != 53052u)
            throw std::runtime_error("LeePerrySmith geometry differs from the r185 lock.");
        entities[0].vertices.reserve(source.positions.size() / 3u);
        for (size_t index = 0u; index < source.positions.size() / 3u; ++index)
            entities[0].vertices.push_back({
                glm::vec4(source.positions[index * 3u],
                          source.positions[index * 3u + 1u],
                          source.positions[index * 3u + 2u], 1.0f),
                glm::vec4(source.normals[index * 3u],
                          source.normals[index * 3u + 1u],
                          source.normals[index * 3u + 2u], 0.0f)});
        entities[0].indices = source.indices;
        buildSimplifiedMesh(source, entities[1].vertices, entities[1].indices);
        const glm::mat4 projection = makeModifierProjection();
        for (uint32_t index = 0u; index < ComparisonEntityCount; ++index)
        {
            auto &entity = entities[index];
            entity.objectData.modelView = makeModifierModelView(index);
            entity.objectData.projection = projection;
            entity.objectData.normalMatrix = glm::transpose(glm::inverse(
                entity.objectData.modelView));
            entity.objectData.materialAndFlags = glm::uvec4(0u, index, 0u, 0u);
            entity.instanceData.offsetAndScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            entity.instanceData.tint = glm::vec4(1.0f);
            entity.materialData.baseColor =
                glm::vec4(0.6653869748f, 0.6653873324f, 0.8227859139f, 1.0f);
        }
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_modifier_simplifier could not create its Scene Set encoder.");
        for (uint32_t index = 0u; index < ComparisonEntityCount; ++index)
        {
            auto &entity = entities[index];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount = static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount = 1u;
            const eastl::string prefix = eastl::string("SimplifierMesh-") + eastl::to_string(index);
            appendBuffer(allocation, WebglModifierSimplifierSceneRenderSetComponents::vertices,
                         prefix + "-vertices", entity.vertices.data(),
                         entity.vertices.size() * sizeof(WebglModifierSimplifierHostVertex), 1u);
            appendBuffer(allocation, WebglModifierSimplifierSceneRenderSetComponents::indices,
                         prefix + "-indices", entity.indices.data(),
                         entity.indices.size() * sizeof(uint32_t), 1u);
            appendBuffer(allocation, WebglModifierSimplifierSceneRenderSetComponents::objects,
                         prefix + "-object", &entity.objectData, sizeof(entity.objectData), 1u);
            appendBuffer(allocation, WebglModifierSimplifierSceneRenderSetComponents::instances,
                         prefix + "-instance", &entity.instanceData, sizeof(entity.instanceData), 1u);
            appendBuffer(allocation, WebglModifierSimplifierSceneRenderSetComponents::materials,
                         prefix + "-material", &entity.materialData, sizeof(entity.materialData), 1u);
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglModifierSimplifierRuntimeAdapter::updateObjectData(uint32_t frameIndex)
    {
        (void)frameIndex;
    }

    void WebglModifierSimplifierRuntimeAdapter::beforeFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                            const ThreeSampleHostOptions &options,
                                                            uint32_t frameIndex)
    {
        (void)options;
        updateObjectData(frameIndex);
        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder) throw std::runtime_error("webgl_modifier_simplifier could not create its update encoder.");
        for (const auto &entity : entities)
            encoder->setBufferComponentData(entity.entityIndex,
                WebglModifierSimplifierSceneRenderSetComponents::objects,
                &entity.objectData, sizeof(entity.objectData), 0u, 1u);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglModifierSimplifierRuntimeAdapter::writeRgbaCapture(const ThreeSampleHostOptions &options,
                                                                 const eastl::vector<uint8_t> &rgba) const
    {
        if (options.captureRgbaPath.empty()) return;
        const std::filesystem::path path(options.captureRgbaPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    }

    void WebglModifierSimplifierRuntimeAdapter::writeCaptureMetadata(const ThreeSampleHostOptions &options,
                                                                     uint32_t frameIndex, uint32_t width,
                                                                     uint32_t height, uint64_t byteCount) const
    {
        if (options.captureMetadataPath.empty()) return;
        const std::filesystem::path path(options.captureMetadataPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\"schemaVersion\":1,\"source\":\"gvm-three-r185\",\"caseId\":\"webgl_modifier_simplifier\",\"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\"pipeline\":\"" << options.pipeline.c_str()
               << "\",\"backend\":\"" << threeSampleBackendName(options.backend) << "\",\"frame\":" << frameIndex
               << ",\"randomSeed\":" << options.randomSeed << ",\"width\":" << width << ",\"height\":" << height
               << ",\"rowStrideBytes\":" << uint64_t(width) * 4u << ",\"byteCount\":" << byteCount
               << ",\"format\":\"rgba8unorm\",\"sampleCount\":1,\"msaaEnabled\":false,"
               << "\"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},"
               << "\"singleSamplePolicy\":{\"sampleCount\":1,\"msaaEnabled\":false,\"simulateMsaa\":false}}\n";
    }

    void WebglModifierSimplifierRuntimeAdapter::writeStructuralSnapshot(const ThreeSampleHostOptions &options,
                                                                        uint32_t frameIndex) const
    {
        if (options.sceneSnapshotPath.empty()) return;
        const std::filesystem::path path(options.sceneSnapshotPath.c_str());
        preparePath(path);
        std::ofstream output(path, std::ios::trunc);
        output << "{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_modifier_simplifier\",\n  \"scenarioId\":\""
               << options.scenarioId.c_str() << "\",\n  \"frame\":" << frameIndex
               << ",\n  \"implementationLevel\":\"strict-pass\",\n  \"gpuWorkDslOnly\":true,\n"
               << "  \"assetBacked\":true,\n  \"assetPath\":\"models/gltf/LeePerrySmith/LeePerrySmith.glb\",\n"
               << "  \"assetSha256\":\"402b8a8ac9f03232e6d64b5962929703a069daf99d3c49ac8eb0e48bedc9c576\",\n"
               << "  \"renderSetPolicy\":\"required\",\n  \"sceneRenderSetCount\":1,\n"
               << "  \"renderSetType\":\"WebglModifierSimplifierSceneRenderSet\",\n  \"renderableObjectCount\":2,\n"
               << "  \"entityCount\":2,\n  \"instanceCount\":2,\n  \"instanceCounts\":[1,1],\n"
               << "  \"vertexCounts\":[" << entities[0].vertices.size() << "," << entities[1].vertices.size() << "],\n"
               << "  \"indexCounts\":[" << entities[0].indices.size() << "," << entities[1].indices.size() << "],\n"
               << "  \"scenePassCount\":1,\n  \"screenPassCount\":0,\n  \"drawCommandCount\":1,\n"
               << "  \"renderSetIndexedIndirect\":true,\n  \"directDrawFallback\":false,\n"
               << "  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n"
               << "  \"componentSchema\":[\"vertices\",\"indices\",\"objects\",\"instances\",\"materials\"],\n"
               << "  \"assetAndAlgorithmState\":\"LeePerrySmith-GLB-and-r185-SimplifyModifier-edge-collapse\",\n"
               << "  \"scenePasses\":[{\"name\":\"main-standard\",\"renderClass\":\"WebglModifierSimplifierScenePass\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]\n}\n";
    }

    void WebglModifierSimplifierRuntimeAdapter::afterFrame(GVM::Core::AbstractRendererImpl &renderer,
                                                           const ThreeSampleHostOptions &options,
                                                           uint32_t frameIndex, GVM::RHI::Texture readbackTexture,
                                                           uint32_t width, uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max()) throw std::overflow_error("webgl_modifier_simplifier capture is too large.");
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        const auto queue = device->graphicsQueue(0);
        if (!queue) throw std::runtime_error("webgl_modifier_simplifier has no graphics queue.");
        queue->readTexture(readbackTexture, rgba.data(), rgba.size())->submit();
        writeRgbaCapture(options, rgba);
        writeCaptureMetadata(options, frameIndex, width, height, byteCount);
        writeStructuralSnapshot(options, frameIndex);
        captureWritten = true;
    }

    void WebglModifierSimplifierRuntimeAdapter::shutdown(GVM::Core::AbstractRendererImpl &renderer,
                                                         const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        entities.clear();
    }
} // namespace GVM::ThreeSamples
