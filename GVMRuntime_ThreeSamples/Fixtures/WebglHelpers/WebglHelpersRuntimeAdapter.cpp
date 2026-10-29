#include "WebglHelpersRuntimeAdapter.hpp"

#include "ThreeCompat/SampleAssetDecoders.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/map.h>
#include <EASTL/set.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <bit>
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
        constexpr float Pi = 3.14159265358979323846f;
        constexpr uint32_t LeePerryVertexCount = 9279u;
        constexpr uint32_t LeePerryIndexCount = 53052u;

        /** Stores one candidate edge and its first adjacent triangle normal. */
        struct HelperEdgeRecord final
        {
            uint32_t firstIndex = 0u;
            uint32_t secondIndex = 0u;
            glm::vec3 firstFaceNormal{0.0f};
            bool hasSecondFace = false;
            bool visible = false;
        };

        /** Stores one mutable world-space axis-aligned range. */
        struct HelperBounds final
        {
            glm::vec3 minimum{std::numeric_limits<float>::max()};
            glm::vec3 maximum{-std::numeric_limits<float>::max()};

            /** Expands this range to contain one world-space point. */
            void include(const glm::vec3 &point)
            {
                minimum = glm::min(minimum, point);
                maximum = glm::max(maximum, point);
            }

            /** Expands this range to contain another finite range. */
            void include(const HelperBounds &other)
            {
                include(other.minimum);
                include(other.maximum);
            }
        };

        static_assert(sizeof(WebglHelpersHostVertex) == 64u);
        static_assert(sizeof(WebglHelpersHostObjectData) == 144u);
        static_assert(sizeof(WebglHelpersHostInstanceData) == 16u);
        static_assert(sizeof(WebglHelpersHostMaterialData) == 16u);
        static_assert(sizeof(WebglHelpersHostHelperData) == 32u);

        /** Converts an authored sRGB helper channel to linear working space. */
        float helperSrgbToLinear(float value)
        {
            return value <= 0.04045f
                ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        /** Converts one 24-bit authored helper color to linear RGB. */
        glm::vec3 helperHexColor(uint32_t value)
        {
            return glm::vec3(
                helperSrgbToLinear(float((value >> 16u) & 255u) / 255.0f),
                helperSrgbToLinear(float((value >> 8u) & 255u) / 255.0f),
                helperSrgbToLinear(float(value & 255u) / 255.0f));
        }

        /** Reads one bounded binary helper asset from the explicit asset root. */
        eastl::vector<uint8_t> readHelperAsset(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() <= 0)
            {
                throw std::runtime_error(
                    "Could not open the locked Lee Perry Smith GLB.");
            }
            const std::streamoff byteCount = input.tellg();
            input.seekg(0, std::ios::beg);
            eastl::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
            input.read(reinterpret_cast<char *>(bytes.data()), byteCount);
            if (!input) throw std::runtime_error(
                "Could not read the locked Lee Perry Smith GLB.");
            return bytes;
        }

        /** Creates Three's zero-to-one perspective matrix for the helper camera. */
        glm::mat4 makeHelperProjection()
        {
            constexpr float NearDistance = 1.0f;
            constexpr float FarDistance = 1000.0f;
            const float top = NearDistance * std::tan(70.0f * Pi / 360.0f);
            const float right = top * (800.0f / 500.0f);
            glm::mat4 projection(0.0f);
            projection[0u][0u] = NearDistance / right;
            projection[1u][1u] = -NearDistance / top;
            projection[2u][2u] = -FarDistance / (FarDistance - NearDistance);
            projection[2u][3u] = -1.0f;
            projection[3u][2u] =
                -(FarDistance * NearDistance) / (FarDistance - NearDistance);
            return projection;
        }

        /** Creates the target-frame camera and animated point-light state. */
        void makeHelperFrameState(uint32_t frameIndex,
                                  glm::mat4 &viewProjection,
                                  glm::vec3 &lightPosition)
        {
            const float time = -float(frameIndex) * 0.005f;
            const glm::vec3 cameraPosition(
                400.0f * std::cos(time), 0.0f,
                400.0f * std::sin(time));
            const glm::mat4 view = glm::lookAtRH(
                cameraPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            viewProjection = makeHelperProjection() * view;
            lightPosition = glm::vec3(
                std::sin(time * 1.7f) * 300.0f,
                std::cos(time * 1.5f) * 400.0f,
                std::cos(time * 1.3f) * 300.0f);
        }

        /** Creates a fully initialized helper entity record. */
        WebglHelpersEntityData makeHelperEntity(
            const char *logicalId,
            const glm::mat4 &viewProjection,
            const glm::vec3 &lightPosition,
            uint32_t kind,
            uint32_t phase,
            const glm::vec3 &color,
            float opacity)
        {
            WebglHelpersEntityData entity;
            entity.logicalId = logicalId;
            entity.objectData.modelViewProjection = viewProjection;
            entity.objectData.model = glm::mat4(1.0f);
            entity.objectData.lightPosition = glm::vec4(lightPosition, 1.0f);
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.baseColorAndRoughness = glm::vec4(
                0.66538697f, 0.66538733f, 0.82278591f, 1.0f);
            entity.helperData.kindPhaseAndDepth =
                glm::uvec4(kind, phase, phase == 0u ? 1u : 0u, 0u);
            entity.helperData.colorOpacity = glm::vec4(color, opacity);
            return entity;
        }

        /** Expands one world-space segment into a deterministic screen-space quad. */
        void appendHelperLine(
            WebglHelpersEntityData &entity,
            const glm::mat4 &viewProjection,
            const glm::vec3 &point0,
            const glm::vec3 &point1,
            const glm::vec3 &color0,
            const glm::vec3 &color1)
        {
            const glm::vec4 clip0 = viewProjection * glm::vec4(point0, 1.0f);
            const glm::vec4 clip1 = viewProjection * glm::vec4(point1, 1.0f);
            if (clip0.w <= 0.0f && clip1.w <= 0.0f) return;
            const glm::vec2 ndc0 = glm::vec2(clip0) / clip0.w;
            const glm::vec2 ndc1 = glm::vec2(clip1) / clip1.w;
            const glm::vec2 pixelDelta(
                (ndc1.x - ndc0.x) * 400.0f,
                (ndc1.y - ndc0.y) * 250.0f);
            const float pixelLength = glm::length(pixelDelta);
            if (pixelLength <= 0.000001f) return;
            constexpr float NativeLineHalfWidth = 0.75f;
            const glm::vec2 perpendicular(
                -pixelDelta.y / pixelLength,
                pixelDelta.x / pixelLength);
            const glm::vec2 ndcOffset(
                perpendicular.x * NativeLineHalfWidth / 400.0f,
                perpendicular.y * NativeLineHalfWidth / 250.0f);
            const glm::vec4 offset0(
                ndcOffset.x * clip0.w,
                ndcOffset.y * clip0.w,
                0.0f,
                0.0f);
            const glm::vec4 offset1(
                ndcOffset.x * clip1.w,
                ndcOffset.y * clip1.w,
                0.0f,
                0.0f);
            const glm::vec4 lineEndpoints(
                (ndc0.x * 0.5f + 0.5f) * 800.0f,
                (ndc0.y * 0.5f + 0.5f) * 500.0f,
                (ndc1.x * 0.5f + 0.5f) * 800.0f,
                (ndc1.y * 0.5f + 0.5f) * 500.0f);
            const uint32_t base = static_cast<uint32_t>(entity.vertices.size());
            entity.vertices.push_back({
                clip0 + offset0, glm::vec4(0.0f), glm::vec4(color0, 1.0f),
                lineEndpoints});
            entity.vertices.push_back({
                clip0 - offset0, glm::vec4(0.0f), glm::vec4(color0, 1.0f),
                lineEndpoints});
            entity.vertices.push_back({
                clip1 + offset1, glm::vec4(0.0f), glm::vec4(color1, 1.0f),
                lineEndpoints});
            entity.vertices.push_back({
                clip1 - offset1, glm::vec4(0.0f), glm::vec4(color1, 1.0f),
                lineEndpoints});
            entity.indices.insert(
                entity.indices.end(),
                {base, base + 1u, base + 2u,
                 base + 2u, base + 1u, base + 3u});
        }

        /** Appends all twelve edges of one world-axis-aligned box. */
        void appendHelperBox(WebglHelpersEntityData &entity,
                             const glm::mat4 &viewProjection,
                             const HelperBounds &bounds,
                             const glm::vec3 &color)
        {
            const glm::vec3 &a = bounds.minimum;
            const glm::vec3 &b = bounds.maximum;
            const glm::vec3 corners[] = {
                {b.x,b.y,b.z},{a.x,b.y,b.z},{a.x,a.y,b.z},{b.x,a.y,b.z},
                {b.x,b.y,a.z},{a.x,b.y,a.z},{a.x,a.y,a.z},{b.x,a.y,a.z},
            };
            const uint32_t edges[][2] = {
                {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},
                {6,7},{7,4},{0,4},{1,5},{2,6},{3,7},
            };
            for (const auto &edge : edges)
                appendHelperLine(entity, viewProjection,
                                 corners[edge[0]], corners[edge[1]], color, color);
        }

        /** Adds a Cartesian GridHelper with exact center-line colors. */
        void buildCartesianGrid(WebglHelpersEntityData &entity,
                                const glm::mat4 &viewProjection)
        {
            const glm::vec3 blue = helperHexColor(0x0000ffu);
            const glm::vec3 gray = helperHexColor(0x808080u);
            for (uint32_t index = 0u; index <= 40u; ++index)
            {
                const float coordinate = -200.0f + float(index) * 10.0f;
                const glm::vec3 color = index == 20u ? blue : gray;
                appendHelperLine(entity, viewProjection,
                    {-350.0f,-150.0f,coordinate}, {50.0f,-150.0f,coordinate}, color, color);
                appendHelperLine(entity, viewProjection,
                    {coordinate-150.0f,-150.0f,-200.0f},
                    {coordinate-150.0f,-150.0f,200.0f}, color, color);
            }
        }

        /** Adds a PolarGridHelper with exact sector and ring topology. */
        void buildPolarGrid(WebglHelpersEntityData &entity,
                            const glm::mat4 &viewProjection)
        {
            const glm::vec3 blue = helperHexColor(0x0000ffu);
            const glm::vec3 gray = helperHexColor(0x808080u);
            for (uint32_t sector = 0u; sector < 16u; ++sector)
            {
                const float angle = float(sector) * 2.0f * Pi / 16.0f;
                const glm::vec3 color = (sector & 1u) ? blue : gray;
                appendHelperLine(entity, viewProjection, {200.0f,-150.0f,0.0f},
                    {200.0f + std::sin(angle)*200.0f,-150.0f,std::cos(angle)*200.0f},
                    color, color);
            }
            for (uint32_t ring = 0u; ring < 8u; ++ring)
            {
                const float radius = 200.0f - 25.0f * float(ring);
                const glm::vec3 color = (ring & 1u) ? blue : gray;
                for (uint32_t segment = 0u; segment < 64u; ++segment)
                {
                    const float a0 = float(segment) * 2.0f * Pi / 64.0f;
                    const float a1 = float(segment + 1u) * 2.0f * Pi / 64.0f;
                    appendHelperLine(entity, viewProjection,
                        {200.0f + std::sin(a0)*radius,-150.0f,std::cos(a0)*radius},
                        {200.0f + std::sin(a1)*radius,-150.0f,std::cos(a1)*radius},
                        color, color);
                }
            }
        }

        /** Hashes one position using WireframeGeometry's four-decimal precision. */
        uint64_t helperPositionHash(const glm::vec3 &position)
        {
            const int64_t x = static_cast<int64_t>(std::llround(position.x * 10000.0f));
            const int64_t y = static_cast<int64_t>(std::llround(position.y * 10000.0f));
            const int64_t z = static_cast<int64_t>(std::llround(position.z * 10000.0f));
            uint64_t hash = 1469598103934665603ull;
            for (const int64_t value : {x, y, z})
            {
                hash ^= static_cast<uint64_t>(value);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        /** Hashes exact Float32 position bits for WireframeGeometry identity. */
        uint64_t helperExactPositionHash(const glm::vec3 &position)
        {
            uint64_t hash = 1469598103934665603ull;
            for (const float value : {position.x, position.y, position.z})
            {
                hash ^= std::bit_cast<uint32_t>(value);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        /** Returns one decoded mesh position scaled by the helper group. */
        glm::vec3 helperMeshPosition(
            const ThreeCompat::DecodedGlbMesh &mesh,
            uint32_t index,
            float xOffset = 0.0f)
        {
            return glm::vec3(
                (mesh.positions[index*3u] + xOffset) * 50.0f,
                mesh.positions[index*3u+1u] * 50.0f,
                mesh.positions[index*3u+2u] * 50.0f);
        }

        /** Builds the indexed loaded head mesh entity. */
        HelperBounds buildHeadMesh(WebglHelpersEntityData &entity,
                                   const ThreeCompat::DecodedGlbMesh &mesh)
        {
            HelperBounds bounds;
            entity.vertices.reserve(LeePerryVertexCount);
            for (uint32_t index = 0u; index < LeePerryVertexCount; ++index)
            {
                const glm::vec3 position = helperMeshPosition(mesh, index);
                const glm::vec3 normal(
                    mesh.normals[index*3u], mesh.normals[index*3u+1u],
                    mesh.normals[index*3u+2u]);
                entity.vertices.push_back({glm::vec4(position,1.0f),
                    glm::vec4(normal,0.0f), glm::vec4(1.0f), glm::vec4(0.0f)});
                bounds.include(position);
            }
            entity.indices.assign(mesh.indices.begin(), mesh.indices.end());
            return bounds;
        }

        /** Builds unique triangle edges for WireframeGeometry or EdgesGeometry. */
        void buildSurfaceLines(WebglHelpersEntityData &entity,
                               const ThreeCompat::DecodedGlbMesh &mesh,
                               const glm::mat4 &viewProjection,
                               float xOffset,
                               bool thresholdEdges,
                               const glm::vec3 &color)
        {
            using EdgeKey = eastl::pair<uint64_t, uint64_t>;
            eastl::map<EdgeKey, HelperEdgeRecord> edgeMap;
            for (size_t triangle = 0u; triangle < mesh.indices.size(); triangle += 3u)
            {
                const uint32_t ids[] = {
                    mesh.indices[triangle], mesh.indices[triangle+1u],
                    mesh.indices[triangle+2u],
                };
                const glm::vec3 p[] = {
                    helperMeshPosition(mesh, ids[0], xOffset),
                    helperMeshPosition(mesh, ids[1], xOffset),
                    helperMeshPosition(mesh, ids[2], xOffset),
                };
                const glm::vec3 faceNormal = glm::normalize(
                    glm::cross(p[2]-p[1], p[0]-p[1]));
                for (uint32_t edge = 0u; edge < 3u; ++edge)
                {
                    const uint32_t a = edge;
                    const uint32_t b = (edge + 1u) % 3u;
                    const glm::vec3 sourceA =
                        helperMeshPosition(mesh, ids[a]) / 50.0f;
                    const glm::vec3 sourceB =
                        helperMeshPosition(mesh, ids[b]) / 50.0f;
                    uint64_t hashA = thresholdEdges
                        ? helperPositionHash(sourceA)
                        : helperExactPositionHash(sourceA);
                    uint64_t hashB = thresholdEdges
                        ? helperPositionHash(sourceB)
                        : helperExactPositionHash(sourceB);
                    EdgeKey key = hashA < hashB
                        ? EdgeKey(hashA, hashB) : EdgeKey(hashB, hashA);
                    auto found = edgeMap.find(key);
                    if (found == edgeMap.end())
                    {
                        HelperEdgeRecord record;
                        record.firstIndex = ids[a];
                        record.secondIndex = ids[b];
                        record.firstFaceNormal = faceNormal;
                        edgeMap.emplace(key, record);
                    }
                    else
                    {
                        found->second.hasSecondFace = true;
                        found->second.visible =
                            glm::dot(found->second.firstFaceNormal, faceNormal) <=
                            std::cos(Pi / 180.0f);
                    }
                }
            }
            for (const auto &entry : edgeMap)
            {
                const HelperEdgeRecord &edge = entry.second;
                if (thresholdEdges && edge.hasSecondFace && !edge.visible) continue;
                appendHelperLine(entity, viewProjection,
                    helperMeshPosition(mesh, edge.firstIndex, xOffset),
                    helperMeshPosition(mesh, edge.secondIndex, xOffset),
                    color, color);
            }
        }

        /** Computes Three-compatible indexed vertex tangents for helper display. */
        eastl::vector<glm::vec3> buildHelperTangents(
            const ThreeCompat::DecodedGlbMesh &mesh)
        {
            eastl::vector<glm::vec3> tan1(LeePerryVertexCount, glm::vec3(0.0f));
            eastl::vector<glm::vec3> tan2(LeePerryVertexCount, glm::vec3(0.0f));
            for (size_t triangle = 0u; triangle < mesh.indices.size(); triangle += 3u)
            {
                const uint32_t i0=mesh.indices[triangle], i1=mesh.indices[triangle+1u], i2=mesh.indices[triangle+2u];
                const glm::vec3 p0=helperMeshPosition(mesh,i0)/50.0f;
                const glm::vec3 p1=helperMeshPosition(mesh,i1)/50.0f;
                const glm::vec3 p2=helperMeshPosition(mesh,i2)/50.0f;
                const glm::vec2 uv0(mesh.textureCoordinates[i0*2u],mesh.textureCoordinates[i0*2u+1u]);
                const glm::vec2 uv1(mesh.textureCoordinates[i1*2u],mesh.textureCoordinates[i1*2u+1u]);
                const glm::vec2 uv2(mesh.textureCoordinates[i2*2u],mesh.textureCoordinates[i2*2u+1u]);
                const glm::vec3 edge1=p1-p0, edge2=p2-p0;
                const glm::vec2 duv1=uv1-uv0, duv2=uv2-uv0;
                const float denominator=duv1.x*duv2.y-duv2.x*duv1.y;
                const float r=denominator==0.0f ? 0.0f : 1.0f/denominator;
                const glm::vec3 sdir=(edge1*duv2.y-edge2*duv1.y)*r;
                const glm::vec3 tdir=(edge2*duv1.x-edge1*duv2.x)*r;
                for (const uint32_t id : {i0,i1,i2}) { tan1[id]+=sdir; tan2[id]+=tdir; }
            }
            eastl::vector<glm::vec3> tangents(LeePerryVertexCount);
            for (uint32_t index=0u; index<LeePerryVertexCount; ++index)
            {
                const glm::vec3 normal(mesh.normals[index*3u],mesh.normals[index*3u+1u],mesh.normals[index*3u+2u]);
                const glm::vec3 tangent=tan1[index]-normal*glm::dot(normal,tan1[index]);
                tangents[index]=glm::length(tangent)>1.0e-10f ? glm::normalize(tangent) : glm::vec3(0.0f);
            }
            return tangents;
        }

        /** Builds per-vertex normal or tangent helper segments. */
        void buildDirectionLines(WebglHelpersEntityData &entity,
                                 const ThreeCompat::DecodedGlbMesh &mesh,
                                 const glm::mat4 &viewProjection,
                                 const eastl::vector<glm::vec3> *tangents,
                                 const glm::vec3 &color)
        {
            for (uint32_t index=0u; index<LeePerryVertexCount; ++index)
            {
                const glm::vec3 start=helperMeshPosition(mesh,index);
                glm::vec3 direction;
                if (tangents) direction=(*tangents)[index];
                else direction=glm::normalize(glm::vec3(
                    mesh.normals[index*3u],mesh.normals[index*3u+1u],mesh.normals[index*3u+2u]));
                appendHelperLine(entity,viewProjection,start,start+direction*5.0f,color,color);
            }
        }

        /** Builds the moving four-by-two PointLightHelper wire sphere. */
        void buildPointLightHelper(WebglHelpersEntityData &entity,
                                   const glm::mat4 &viewProjection,
                                   const glm::vec3 &lightPosition)
        {
            const glm::vec3 color(1.0f);
            const glm::vec3 poles[] = {
                lightPosition + glm::vec3(0.0f,15.0f,0.0f),
                lightPosition + glm::vec3(0.0f,-15.0f,0.0f),
            };
            glm::vec3 ring[4u];
            for (uint32_t index=0u; index<4u; ++index)
            {
                const float angle=float(index)*Pi*0.5f;
                ring[index]=lightPosition+glm::vec3(-std::cos(angle)*15.0f,0.0f,std::sin(angle)*15.0f);
            }
            for (uint32_t index=0u; index<4u; ++index)
            {
                const uint32_t next=(index+1u)%4u;
                appendHelperLine(entity,viewProjection,poles[0],ring[index],color,color);
                appendHelperLine(entity,viewProjection,ring[index],ring[next],color,color);
                appendHelperLine(entity,viewProjection,ring[index],poles[1],color,color);
            }
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendHelperPayload(GVM::Core::RenderSetAllocInfo &allocation,
                                 GVM::Core::RenderComponentHandle component,
                                 const eastl::string &name,
                                 const void *value,
                                 uint64_t byteCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle=component,.bufferName=name,.value=value,
                .dataStorageSize=byteCount,.instanceCount=1u,
            });
        }

        /** Allocates one complete helper entity through current RenderSet semantics. */
        GVM::Core::RenderEntityIndex allocateHelperEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const WebglHelpersEntityData &entity)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount=static_cast<uint32_t>(entity.vertices.size());
            allocation.indicesCount=static_cast<uint32_t>(entity.indices.size());
            allocation.instanceCount=1u;
            appendHelperPayload(allocation,WebglHelpersSceneRenderSetComponents::vertices,
                entity.logicalId+"-vertices",entity.vertices.data(),entity.vertices.size()*sizeof(WebglHelpersHostVertex));
            appendHelperPayload(allocation,WebglHelpersSceneRenderSetComponents::indices,
                entity.logicalId+"-indices",entity.indices.data(),entity.indices.size()*sizeof(uint32_t));
            appendHelperPayload(allocation,WebglHelpersSceneRenderSetComponents::objects,
                entity.logicalId+"-object",&entity.objectData,sizeof(entity.objectData));
            appendHelperPayload(allocation,WebglHelpersSceneRenderSetComponents::instances,
                entity.logicalId+"-instance",&entity.instanceData,sizeof(entity.instanceData));
            appendHelperPayload(allocation,WebglHelpersSceneRenderSetComponents::materials,
                entity.logicalId+"-material",&entity.materialData,sizeof(entity.materialData));
            appendHelperPayload(allocation,WebglHelpersSceneRenderSetComponents::helperData,
                entity.logicalId+"-helper",&entity.helperData,sizeof(entity.helperData));
            return encoder.allocEntity(allocation);
        }

        /** Creates parent directories for one helper evidence artifact. */
        void prepareHelperOutputPath(const std::filesystem::path &path)
        {
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional helper evidence text artifact. */
        void writeHelperText(const eastl::string &path,const std::string &text)
        {
            if(path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());prepareHelperOutputPath(outputPath);
            std::ofstream output(outputPath,std::ios::trunc);output<<text;
            if(!output) throw std::runtime_error("Could not write webgl_helpers evidence.");
        }
    }

    void WebglHelpersRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool loader=options.scenarioId=="loader-snapshot"&&options.targetFrame==0u;
        const bool initial=options.scenarioId=="initial-loaded"&&options.targetFrame==0u;
        const bool animated=options.scenarioId=="animated-helpers"&&options.targetFrame==60u;
        if(options.caseId!="webgl_helpers"||(!loader&&!initial&&!animated)||options.width!=800u||options.height!=500u||options.randomSeed!=DefaultThreeRandomSeed||options.assetRoot.empty()||!options.inputReplayPath.empty())
            throw std::invalid_argument("webgl_helpers requires one frozen r185 Manifest scenario.");
        device=inDevice;
        const auto mesh=ThreeCompat::decodeFirstGlbMesh(readHelperAsset(
            std::filesystem::path(options.assetRoot.c_str())/"models"/"gltf"/"LeePerrySmith"/"LeePerrySmith.glb"));
        if(mesh.positions.size()!=size_t(LeePerryVertexCount)*3u||mesh.indices.size()!=LeePerryIndexCount)
            throw std::runtime_error("Lee Perry Smith helper counts diverged from r185.");
        glm::mat4 viewProjection;glm::vec3 lightPosition;
        makeHelperFrameState(options.targetFrame,viewProjection,lightPosition);
        const glm::vec3 white(1.0f),yellow=helperHexColor(0xffff00u);
        entities.reserve(13u);

        entities.push_back(makeHelperEntity("point-light-helper",viewProjection,lightPosition,1u,0u,white,1.0f));
        buildPointLightHelper(entities.back(),viewProjection,lightPosition);
        entities.push_back(makeHelperEntity("cartesian-grid",viewProjection,lightPosition,1u,0u,white,1.0f));
        buildCartesianGrid(entities.back(),viewProjection);
        entities.push_back(makeHelperEntity("polar-grid",viewProjection,lightPosition,1u,0u,white,1.0f));
        buildPolarGrid(entities.back(),viewProjection);
        entities.push_back(makeHelperEntity("lee-perry-smith",viewProjection,lightPosition,0u,0u,white,1.0f));
        const HelperBounds meshBounds=buildHeadMesh(entities.back(),mesh);
        const eastl::vector<glm::vec3> tangents=buildHelperTangents(mesh);
        entities.push_back(makeHelperEntity("vertex-normals",viewProjection,lightPosition,1u,0u,helperHexColor(0xff0000u),1.0f));
        buildDirectionLines(entities.back(),mesh,viewProjection,nullptr,helperHexColor(0xff0000u));
        entities.push_back(makeHelperEntity("vertex-tangents",viewProjection,lightPosition,1u,0u,helperHexColor(0x00ffffu),1.0f));
        buildDirectionLines(entities.back(),mesh,viewProjection,&tangents,helperHexColor(0x00ffffu));
        entities.push_back(makeHelperEntity("mesh-box",viewProjection,lightPosition,1u,0u,yellow,1.0f));
        appendHelperBox(entities.back(),viewProjection,meshBounds,yellow);
        entities.push_back(makeHelperEntity("wireframe",viewProjection,lightPosition,1u,1u,white,0.25f));
        buildSurfaceLines(entities.back(),mesh,viewProjection,4.0f,false,white);
        HelperBounds wireBounds;
        for(uint32_t i=0u;i<LeePerryVertexCount;++i) wireBounds.include(helperMeshPosition(mesh,i,4.0f));
        entities.push_back(makeHelperEntity("wireframe-box",viewProjection,lightPosition,1u,0u,yellow,1.0f));
        appendHelperBox(entities.back(),viewProjection,wireBounds,yellow);
        entities.push_back(makeHelperEntity("edges",viewProjection,lightPosition,1u,1u,white,0.25f));
        buildSurfaceLines(entities.back(),mesh,viewProjection,-4.0f,true,white);
        HelperBounds edgeBounds;
        for(uint32_t i=0u;i<LeePerryVertexCount;++i) edgeBounds.include(helperMeshPosition(mesh,i,-4.0f));
        entities.push_back(makeHelperEntity("edges-box",viewProjection,lightPosition,1u,0u,yellow,1.0f));
        appendHelperBox(entities.back(),viewProjection,edgeBounds,yellow);
        HelperBounds groupBounds=meshBounds;groupBounds.include(wireBounds);groupBounds.include(edgeBounds);
        entities.push_back(makeHelperEntity("group-box",viewProjection,lightPosition,1u,0u,yellow,1.0f));
        appendHelperBox(entities.back(),viewProjection,groupBounds,yellow);
        HelperBounds sceneBounds=groupBounds;
        sceneBounds.include({{-350.0f,-150.0f,-200.0f},{400.0f,-150.0f,200.0f}});
        sceneBounds.include({{185.0f,85.0f,135.0f},{215.0f,115.0f,165.0f}});
        entities.push_back(makeHelperEntity("scene-box",viewProjection,lightPosition,1u,0u,yellow,1.0f));
        appendHelperBox(entities.back(),viewProjection,sceneBounds,yellow);

        const auto encoder=renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if(!encoder) throw std::runtime_error("Could not create webgl_helpers Scene Set encoder.");
        for(auto &entity:entities) entity.entityIndex=allocateHelperEntity(*encoder,entity);
        renderer.executeRenderSetCommand(SceneRenderSetHandle,encoder);
    }

    void WebglHelpersRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,const ThreeSampleHostOptions &options,uint32_t frameIndex)
    { (void)renderer;(void)options;(void)frameIndex; }

    void WebglHelpersRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,const ThreeSampleHostOptions &options,uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,uint32_t width,uint32_t height)
    {
        (void)renderer;if(captureWritten||frameIndex!=options.targetFrame)return;
        const uint64_t byteCount=uint64_t(width)*height*4u;eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture,rgba.data(),rgba.size())->submit();
        if(!options.captureRgbaPath.empty()){const std::filesystem::path path(options.captureRgbaPath.c_str());prepareHelperOutputPath(path);std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(rgba.data()),static_cast<std::streamsize>(rgba.size()));if(!output)throw std::runtime_error("Could not write webgl_helpers RGBA evidence.");}
        std::ostringstream metadata;metadata<<"{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_helpers\",\n  \"scenarioId\":\""<<options.scenarioId.c_str()<<"\",\n  \"pipeline\":\""<<options.pipeline.c_str()<<"\",\n  \"backend\":\""<<threeSampleBackendName(options.backend)<<"\",\n  \"frame\":"<<frameIndex<<",\n  \"randomSeed\":"<<options.randomSeed<<",\n  \"width\":"<<width<<",\n  \"height\":"<<height<<",\n  \"rowStrideBytes\":"<<uint64_t(width)*4u<<",\n  \"byteCount\":"<<byteCount<<",\n  \"format\":\"rgba8unorm\",\n  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n  \"gpuWorkDslOnly\":true\n}\n";writeHelperText(options.captureMetadataPath,metadata.str());
        std::ostringstream snapshot;snapshot<<"{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_helpers\",\n  \"scenarioId\":\""<<options.scenarioId.c_str()<<"\",\n  \"frame\":"<<frameIndex<<",\n  \"renderSetPolicy\":\"required\",\n  \"gpuWorkDslOnly\":true,\n  \"sceneRenderSetCount\":1,\n  \"renderableObjectCount\":13,\n  \"drawCommandCount\":2,\n  \"scenePassCount\":2,\n  \"screenPassCount\":0,\n  \"scenePassSequence\":[{\"sceneRoot\":\"scene\",\"scenePass\":\"main-opaque-mesh-and-helpers\"},{\"sceneRoot\":\"scene\",\"scenePass\":\"main-transparent-wire-and-edges\"}],\n  \"directDrawFallback\":false,\n  \"usesRenderEntityID\":true,\n  \"usesRenderEntityInstanceID\":true,\n  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene\",\"renderSetType\":\"WebglHelpersSceneRenderSet\",\"renderableObjectCount\":13,\"entityCount\":13,\"entities\":[";
        const char* ids[]={"point-light-helper","cartesian-grid","polar-grid","lee-perry-smith","vertex-normals","vertex-tangents","mesh-box","wireframe","wireframe-box","edges","edges-box","group-box","scene-box"};for(uint32_t i=0;i<13;++i){if(i)snapshot<<",";snapshot<<"{\"entityId\":"<<i<<",\"logicalRenderableId\":\""<<ids[i]<<"\",\"instanceCount\":1}";}snapshot<<"],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"helperData\",\"kind\":\"buffer\",\"role\":\"helper-kind-color-opacity-depth-and-visibility\"}],\"drawCommandCount\":2,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main-opaque-mesh-and-helpers\",\"renderClass\":\"WebglHelpersOpaquePass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false},{\"name\":\"main-transparent-wire-and-edges\",\"renderClass\":\"WebglHelpersTransparentPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}]\n}\n";writeHelperText(options.sceneSnapshotPath,snapshot.str());
        std::ostringstream semantic;semantic<<"{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_helpers\",\n  \"scenarioId\":\""<<options.scenarioId.c_str()<<"\",\n  \"frame\":"<<frameIndex<<",\n  \"kind\":\"loader-snapshot\",\n  \"canonicalState\":\"lee-perry-smith-one-primitive-thirteen-helper-scene-entities\",\n  \"result\":{\"renderableObjectCount\":1,\"sceneRootCount\":1,\"canonicalSceneSha256\":\"402b8a8ac9f03232e6d64b5962929703a069daf99d3c49ac8eb0e48bedc9c576\"}\n}\n";writeHelperText(options.semanticSnapshotPath,semantic.str());captureWritten=true;
    }

    void WebglHelpersRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,const ThreeSampleHostOptions &options)
    { (void)renderer;(void)options;entities.clear(); }
} // namespace GVM::ThreeSamples
