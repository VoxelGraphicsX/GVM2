#include "WebglGeometryTerrainRaycastRuntimeAdapter.hpp"

#include "ThreeCompat/DeterministicRandom.hpp"
#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <EASTL/array.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t WorldWidth = 256u;
        constexpr uint32_t WorldDepth = 256u;
        constexpr uint32_t TextureExtent = 1024u;
        constexpr double Pi = 3.14159265358979323846264338327950288;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr const char *PointerReplaySha256 =
            "ecfcf5e158794095f23cf3bdb38998083ca3d90a4d254df2cb2e6050e1ca69ef";

        static_assert(sizeof(TerrainRaycastHostVertex) == 48u);
        static_assert(sizeof(TerrainRaycastHostObjectData) == 128u);
        static_assert(sizeof(TerrainRaycastHostInstanceData) == 16u);
        static_assert(sizeof(TerrainRaycastHostMaterialData) == 16u);

        /** Returns one duplicated r185 ImprovedNoise permutation entry. */
        uint32_t terrainRaycastPermutation(uint32_t index)
        {
            static constexpr eastl::array<uint32_t, 256u> values = {
                151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
                140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
                247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
                57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
                74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
                60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
                65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,
                196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,
                217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,
                206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,
                248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,129,
                22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,
                246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
                81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
                184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
                222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
            };
            return values[index & 255u];
        }

        /** Evaluates the ImprovedNoise quintic interpolation weight. */
        double terrainRaycastFade(double value)
        {
            return value * value * value *
                   (value * (value * 6.0 - 15.0) + 10.0);
        }

        /** Evaluates one selected ImprovedNoise lattice gradient. */
        double terrainRaycastGradient(
            uint32_t hash,
            double x,
            double y,
            double z)
        {
            const uint32_t h = hash & 15u;
            const double u = h < 8u ? x : y;
            const double v = h < 4u
                ? y
                : ((h == 12u || h == 14u) ? x : z);
            return ((h & 1u) == 0u ? u : -u) +
                   ((h & 2u) == 0u ? v : -v);
        }

        /** Reproduces JavaScript MathUtils.lerp operation ordering. */
        double terrainRaycastLerp(double x, double y, double t)
        {
            return (1.0 - t) * x + t * y;
        }

        /** Evaluates one exact JavaScript-double ImprovedNoise sample. */
        double terrainRaycastImprovedNoise(
            double x,
            double y,
            double z)
        {
            const double floorX = std::floor(x);
            const double floorY = std::floor(y);
            const double floorZ = std::floor(z);
            const uint32_t X = uint32_t(int64_t(floorX) & 255ll);
            const uint32_t Y = uint32_t(int64_t(floorY) & 255ll);
            const uint32_t Z = uint32_t(int64_t(floorZ) & 255ll);
            x -= floorX;
            y -= floorY;
            z -= floorZ;
            const double u = terrainRaycastFade(x);
            const double v = terrainRaycastFade(y);
            const double w = terrainRaycastFade(z);
            const uint32_t A = terrainRaycastPermutation(X) + Y;
            const uint32_t AA = terrainRaycastPermutation(A) + Z;
            const uint32_t AB = terrainRaycastPermutation(A + 1u) + Z;
            const uint32_t B = terrainRaycastPermutation(X + 1u) + Y;
            const uint32_t BA = terrainRaycastPermutation(B) + Z;
            const uint32_t BB = terrainRaycastPermutation(B + 1u) + Z;
            return terrainRaycastLerp(
                terrainRaycastLerp(
                    terrainRaycastLerp(
                        terrainRaycastGradient(terrainRaycastPermutation(AA), x, y, z),
                        terrainRaycastGradient(terrainRaycastPermutation(BA), x - 1.0, y, z),
                        u),
                    terrainRaycastLerp(
                        terrainRaycastGradient(terrainRaycastPermutation(AB), x, y - 1.0, z),
                        terrainRaycastGradient(terrainRaycastPermutation(BB), x - 1.0, y - 1.0, z),
                        u),
                    v),
                terrainRaycastLerp(
                    terrainRaycastLerp(
                        terrainRaycastGradient(terrainRaycastPermutation(AA + 1u), x, y, z - 1.0),
                        terrainRaycastGradient(terrainRaycastPermutation(BA + 1u), x - 1.0, y, z - 1.0),
                        u),
                    terrainRaycastLerp(
                        terrainRaycastGradient(terrainRaycastPermutation(AB + 1u), x, y - 1.0, z - 1.0),
                        terrainRaycastGradient(terrainRaycastPermutation(BB + 1u), x - 1.0, y - 1.0, z - 1.0),
                        u),
                    v),
                w);
        }

        /** Builds the source four-octave Uint8 heightfield from the shared stream. */
        eastl::vector<uint32_t> buildTerrainRaycastHeights(
            ThreeCompat::DeterministicRandom &random)
        {
            eastl::vector<uint32_t> result(
                WorldWidth * WorldDepth,
                0u);
            const double noiseZ =
                double(random.nextUint32() >> 8u) /
                16777216.0 * 100.0;
            double quality = 1.0;
            for (uint32_t octave = 0u; octave < 4u; ++octave)
            {
                for (uint32_t index = 0u;
                     index < uint32_t(result.size());
                     ++index)
                {
                    const uint32_t x = index % WorldWidth;
                    const uint32_t y = index / WorldWidth;
                    const double contribution = std::abs(
                        terrainRaycastImprovedNoise(
                            double(x) / quality,
                            double(y) / quality,
                            noiseZ) *
                        quality * 1.75);
                    result[index] = uint32_t(uint8_t(
                        double(result[index]) + contribution));
                }
                quality *= 5.0;
            }
            return result;
        }

        /** Builds the exact post-scale five-level Canvas grain stream. */
        eastl::vector<uint32_t> buildTerrainRaycastGrain(
            ThreeCompat::DeterministicRandom &random)
        {
            eastl::vector<uint32_t> result(
                TextureExtent * TextureExtent,
                0u);
            for (uint32_t &value : result)
            {
                value = uint32_t(std::floor(
                    double(random.nextUint32() >> 8u) /
                    16777216.0 * 5.0));
            }
            return result;
        }

        /** Creates exact indexed PlaneGeometry attributes for the terrain entity. */
        TerrainRaycastEntityState buildTerrainRaycastMesh(
            const eastl::vector<uint32_t> &heightValues)
        {
            TerrainRaycastEntityState entity;
            entity.logicalId = "terrain";
            entity.vertices.reserve(WorldWidth * WorldDepth);
            for (uint32_t y = 0u; y < WorldDepth; ++y)
            {
                for (uint32_t x = 0u; x < WorldWidth; ++x)
                {
                    const uint32_t index = y * WorldWidth + x;
                    const float u = float(x) / float(WorldWidth - 1u);
                    const float v = float(y) / float(WorldDepth - 1u);
                    entity.vertices.push_back({
                        glm::vec4(
                            (u - 0.5f) * 7500.0f,
                            float(heightValues[index]) * 10.0f,
                            (v - 0.5f) * 7500.0f,
                            1.0f),
                        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
                        glm::vec4(u, v, 0.0f, 0.0f),
                    });
                }
            }
            entity.indices.reserve(
                (WorldWidth - 1u) * (WorldDepth - 1u) * 6u);
            for (uint32_t y = 0u; y < WorldDepth - 1u; ++y)
            {
                for (uint32_t x = 0u; x < WorldWidth - 1u; ++x)
                {
                    const uint32_t a = y * WorldWidth + x;
                    const uint32_t b = (y + 1u) * WorldWidth + x;
                    const uint32_t c = b + 1u;
                    const uint32_t d = a + 1u;
                    entity.indices.insert(
                        entity.indices.end(),
                        {a, b, d, b, c, d});
                }
            }
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.phaseAndReserved = glm::vec4(0.0f);
            return entity;
        }

        /** Creates a deterministic triangular ConeGeometry helper entity. */
        TerrainRaycastEntityState buildTerrainRaycastCone()
        {
            TerrainRaycastEntityState entity;
            entity.logicalId = "normal-cone";
            const glm::vec3 tip(0.0f, 0.0f, 100.0f);
            const glm::vec3 base[3u] = {
                glm::vec3(20.0f, 0.0f, 0.0f),
                glm::vec3(-10.0f, 17.320508f, 0.0f),
                glm::vec3(-10.0f, -17.320508f, 0.0f),
            };
            for (uint32_t side = 0u; side < 3u; ++side)
            {
                const glm::vec3 first = base[side];
                const glm::vec3 second = base[(side + 1u) % 3u];
                const glm::vec3 normal = glm::normalize(
                    glm::cross(second - first, tip - first));
                const uint32_t start = uint32_t(entity.vertices.size());
                entity.vertices.push_back({glm::vec4(first, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0.0f)});
                entity.vertices.push_back({glm::vec4(second, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0.0f)});
                entity.vertices.push_back({glm::vec4(tip, 1.0f), glm::vec4(normal, 0.0f), glm::vec4(0.0f)});
                entity.indices.insert(entity.indices.end(), {start, start + 1u, start + 2u});
            }
            const uint32_t capStart = uint32_t(entity.vertices.size());
            for (uint32_t index = 0u; index < 3u; ++index)
            {
                entity.vertices.push_back({
                    glm::vec4(base[index], 1.0f),
                    glm::vec4(0.0f, 0.0f, -1.0f, 0.0f),
                    glm::vec4(0.0f)});
            }
            entity.indices.insert(
                entity.indices.end(),
                {capStart, capStart + 2u, capStart + 1u});
            entity.instanceData.reserved = glm::vec4(0.0f);
            entity.materialData.phaseAndReserved = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            return entity;
        }

        /** Computes the canonical center-pointer terrain hit and helper transform. */
        glm::mat4 buildTerrainRaycastHelperModel(
            const TerrainRaycastEntityState &terrain,
            const glm::vec3 &rayOrigin,
            const glm::vec3 &rayDirection)
        {
            float nearestDistance = std::numeric_limits<float>::max();
            glm::vec3 nearestNormal(0.0f, 1.0f, 0.0f);
            for (size_t triangle = 0u;
                 triangle + 2u < terrain.indices.size();
                 triangle += 3u)
            {
                const glm::vec3 first = glm::vec3(
                    terrain.vertices[terrain.indices[triangle]].position);
                const glm::vec3 second = glm::vec3(
                    terrain.vertices[terrain.indices[triangle + 1u]].position);
                const glm::vec3 third = glm::vec3(
                    terrain.vertices[terrain.indices[triangle + 2u]].position);
                const glm::vec3 firstEdge = second - first;
                const glm::vec3 secondEdge = third - first;
                const glm::vec3 crossDirection =
                    glm::cross(rayDirection, secondEdge);
                const float determinant =
                    glm::dot(firstEdge, crossDirection);
                if (std::abs(determinant) < 1.0e-7f) continue;
                const float inverseDeterminant = 1.0f / determinant;
                const glm::vec3 originDelta = rayOrigin - first;
                const float barycentricU =
                    glm::dot(originDelta, crossDirection) *
                    inverseDeterminant;
                if (barycentricU < 0.0f || barycentricU > 1.0f) continue;
                const glm::vec3 crossOrigin =
                    glm::cross(originDelta, firstEdge);
                const float barycentricV =
                    glm::dot(rayDirection, crossOrigin) *
                    inverseDeterminant;
                if (barycentricV < 0.0f ||
                    barycentricU + barycentricV > 1.0f)
                {
                    continue;
                }
                const float distance =
                    glm::dot(secondEdge, crossOrigin) *
                    inverseDeterminant;
                if (distance <= 0.0f || distance >= nearestDistance) continue;
                nearestDistance = distance;
                nearestNormal = glm::normalize(
                    glm::cross(firstEdge, secondEdge));
            }
            if (nearestDistance == std::numeric_limits<float>::max())
            {
                return glm::mat4(1.0f);
            }
            const glm::vec3 hitPosition =
                rayOrigin + rayDirection * nearestDistance;
            const glm::vec3 zAxis = nearestNormal;
            const glm::vec3 xAxis = glm::normalize(
                glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), zAxis));
            const glm::vec3 yAxis = glm::cross(zAxis, xAxis);
            glm::mat4 rotation(1.0f);
            rotation[0u] = glm::vec4(xAxis, 0.0f);
            rotation[1u] = glm::vec4(yAxis, 0.0f);
            rotation[2u] = glm::vec4(zAxis, 0.0f);
            return glm::translate(glm::mat4(1.0f), hitPosition) * rotation;
        }

        /** Appends one typed component payload to a RenderSet allocation. */
        void appendTerrainRaycastBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
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

        /** Creates parent directories for one requested capture artifact. */
        void prepareTerrainRaycastOutput(const eastl::string &pathValue)
        {
            if (pathValue.empty()) return;
            const std::filesystem::path path(pathValue.c_str());
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }
    } // namespace

    void WebglGeometryTerrainRaycastRuntimeAdapter::initializeCpuState(
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial = options.scenarioId == "initial" &&
            options.targetFrame == 0u && options.inputReplayPath.empty();
        const bool animated = options.scenarioId == "animated" &&
            options.targetFrame == 60u && options.inputReplayPath.empty();
        const bool raycast = options.scenarioId == "raycast-input" &&
            options.targetFrame == 61u && !options.inputReplayPath.empty();
        if (options.caseId != "webgl_geometry_terrain_raycast" ||
            (!initial && !animated && !raycast) ||
            options.width != 800u || options.height != 500u ||
            options.randomSeed != DefaultThreeRandomSeed)
        {
            throw std::invalid_argument(
                "Terrain raycast requires one locked Manifest scenario.");
        }
        device = inDevice;
        ThreeCompat::DeterministicRandom random(options.randomSeed);
        for (uint32_t rendererRandomCall = 0u;
             rendererRandomCall < 120u;
             ++rendererRandomCall)
        {
            (void)random.nextUint32();
        }
        heights = buildTerrainRaycastHeights(random);
        for (uint32_t textureRandomCall = 0u;
             textureRandomCall < 4u;
             ++textureRandomCall)
        {
            (void)random.nextUint32();
        }
        grain = buildTerrainRaycastGrain(random);
        entities[0u] = buildTerrainRaycastMesh(heights);
        entities[1u] = buildTerrainRaycastCone();

        const float targetHeight =
            float(heights[WorldWidth / 2u +
                (WorldDepth / 2u) * WorldWidth]) +
            500.0f;
        const glm::vec3 cameraPosition(
            2000.0f,
            targetHeight + 2000.0f,
            0.0f);
        const glm::vec3 cameraTarget(0.0f, targetHeight, 0.0f);
        const glm::mat4 view = glm::lookAtRH(
            cameraPosition,
            cameraTarget,
            glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspectiveRH_ZO(
            float(60.0 * Pi / 180.0),
            float(options.width) / float(options.height),
            10.0f,
            20000.0f);
        projection[1u][1u] *= -1.0f;
        entities[0u].objectData.modelView = view;
        entities[0u].objectData.projection = projection;
        const glm::mat4 helperModel = raycast
            ? buildTerrainRaycastHelperModel(
                entities[0u],
                cameraPosition,
                glm::normalize(cameraTarget - cameraPosition))
            : glm::mat4(1.0f);
        entities[1u].objectData.modelView = view * helperModel;
        entities[1u].objectData.projection = projection;
    }

    void WebglGeometryTerrainRaycastRuntimeAdapter::allocateScene(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)options;
        const auto encoder = renderer.createRenderSetCommandEncoder(
            SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error(
                "Terrain raycast could not create its Scene Set encoder.");
        }
        for (uint32_t entityOrdinal = 0u;
             entityOrdinal < entities.size();
             ++entityOrdinal)
        {
            TerrainRaycastEntityState &entity = entities[entityOrdinal];
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount = uint32_t(entity.vertices.size());
            allocation.indicesCount = uint32_t(entity.indices.size());
            allocation.instanceCount = 1u;
            appendTerrainRaycastBufferPayload(
                allocation,
                WebglGeometryTerrainRaycastSceneRenderSetComponents::vertices,
                entity.logicalId + "-vertices",
                entity.vertices.data(),
                entity.vertices.size() * sizeof(TerrainRaycastHostVertex),
                1u);
            appendTerrainRaycastBufferPayload(
                allocation,
                WebglGeometryTerrainRaycastSceneRenderSetComponents::indices,
                entity.logicalId + "-indices",
                entity.indices.data(),
                entity.indices.size() * sizeof(uint32_t),
                1u);
            appendTerrainRaycastBufferPayload(
                allocation,
                WebglGeometryTerrainRaycastSceneRenderSetComponents::objects,
                entity.logicalId + "-object",
                &entity.objectData,
                sizeof(entity.objectData),
                1u);
            appendTerrainRaycastBufferPayload(
                allocation,
                WebglGeometryTerrainRaycastSceneRenderSetComponents::instances,
                entity.logicalId + "-instance",
                &entity.instanceData,
                sizeof(entity.instanceData),
                1u);
            appendTerrainRaycastBufferPayload(
                allocation,
                WebglGeometryTerrainRaycastSceneRenderSetComponents::materials,
                entity.logicalId + "-material",
                &entity.materialData,
                sizeof(entity.materialData),
                1u);
            if (entityOrdinal == 0u)
            {
                GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
                textureInfo.textureComponentHandle =
                    WebglGeometryTerrainRaycastSceneRenderSetComponents::textures;
                textureInfo.textures.push_back({
                    .textureName = "WebglGeometryTerrainRaycastTexture",
                    .format = GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                    .width = TextureExtent,
                    .height = TextureExtent,
                    .data = terrainTexture.data(),
                    .dataStorageBytes = terrainTexture.size(),
                    .mipmapOffsetBytes = {0u},
                });
                allocation.textureInfos.push_back(eastl::move(textureInfo));
            }
            entity.entityIndex = encoder->allocEntity(allocation);
        }
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
    }

    void WebglGeometryTerrainRaycastRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    {
        (void)renderer;
        (void)options;
        (void)frameIndex;
    }

    void WebglGeometryTerrainRaycastRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;
        if (captureWritten || frameIndex != options.targetFrame) return;
        const uint64_t byteCount = uint64_t(width) * uint64_t(height) * 4u;
        if (byteCount > std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error(
                "Terrain raycast capture is too large.");
        }
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)
            ->readTexture(readbackTexture, rgba.data(), rgba.size())
            ->submit();
        if (!options.captureRgbaPath.empty())
        {
            prepareTerrainRaycastOutput(options.captureRgbaPath);
            std::ofstream output(
                options.captureRgbaPath.c_str(),
                std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char *>(rgba.data()),
                static_cast<std::streamsize>(rgba.size()));
        }
        if (!options.captureMetadataPath.empty())
        {
            prepareTerrainRaycastOutput(options.captureMetadataPath);
            std::ofstream output(
                options.captureMetadataPath.c_str(),
                std::ios::trunc);
            output
                << "{\n"
                << "  \"schemaVersion\":1,\n"
                << "  \"source\":\"gvm-three-r185\",\n"
                << "  \"caseId\":\"webgl_geometry_terrain_raycast\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"pipeline\":\"" << options.pipeline.c_str() << "\",\n"
                << "  \"backend\":\"" << threeSampleBackendName(options.backend) << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"randomSeed\":" << options.randomSeed << ",\n"
                << "  \"randomState\":3460588083,\n"
                << "  \"width\":" << width << ",\n"
                << "  \"height\":" << height << ",\n"
                << "  \"rowStrideBytes\":" << uint64_t(width) * 4u << ",\n"
                << "  \"byteCount\":" << rgba.size() << ",\n"
                << "  \"format\":\"rgba8unorm\",\n"
                << "  \"inputReplay\":";
            if (options.scenarioId == "raycast-input")
            {
                output
                    << "{\"sha256\":\"" << PointerReplaySha256 << "\","
                    << "\"caseId\":\"webgl_geometry_terrain_raycast\","
                    << "\"scenarioId\":\"raycast-input\","
                    << "\"captureFrame\":61,\"eventCount\":1,"
                    << "\"target\":\"#container > canvas\"}";
            }
            else
            {
                output << "null";
            }
            output << "\n}\n";
        }
        if (!options.sceneSnapshotPath.empty())
        {
            prepareTerrainRaycastOutput(options.sceneSnapshotPath);
            std::ofstream output(
                options.sceneSnapshotPath.c_str(),
                std::ios::trunc);
            output
                << "{\n"
                << "  \"caseId\":\"webgl_geometry_terrain_raycast\",\n"
                << "  \"scenarioId\":\"" << options.scenarioId.c_str() << "\",\n"
                << "  \"frame\":" << frameIndex << ",\n"
                << "  \"implementationLevel\":\"semantic-complete\",\n"
                << "  \"gpuWorkDslOnly\":true,\n"
                << "  \"renderSetPolicy\":\"required\",\n"
                << "  \"sceneRenderSetCount\":1,\n"
                << "  \"renderableObjectCount\":2,\n"
                << "  \"entityCount\":2,\n"
                << "  \"entityInstanceCounts\":[1,1],\n"
                << "  \"scenePassCount\":1,\n"
                << "  \"drawCommandCount\":1,\n"
                << "  \"computeDispatchThreads\":[1024,1024,1],\n"
                << "  \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                << "  \"sceneRoots\":[{\n"
                << "    \"id\":\"scene\",\n"
                << "    \"renderSetCount\":1,\n"
                << "    \"renderSetId\":\"terrain-raycast-scene-set\",\n"
                << "    \"renderSetType\":\"WebglGeometryTerrainRaycastSceneRenderSet\",\n"
                << "    \"renderableObjectCount\":2,\n"
                << "    \"entityCount\":2,\n"
                << "    \"entities\":["
                << "{\"entityId\":" << entities[0u].entityIndex
                << ",\"logicalRenderableId\":\"terrain\",\"instanceCount\":1},"
                << "{\"entityId\":" << entities[1u].entityIndex
                << ",\"logicalRenderableId\":\"normal-cone\",\"instanceCount\":1}],\n"
                << "    \"componentSchema\":["
                << "{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},"
                << "{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},"
                << "{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},"
                << "{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},"
                << "{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},"
                << "{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\n"
                << "    \"drawCommandCount\":1,\n"
                << "    \"directDrawFallback\":false,\n"
                << "    \"scenePasses\":[{"
                << "\"name\":\"main\","
                << "\"renderClass\":\"WebglGeometryTerrainRaycastMainPass\","
                << "\"renderSetId\":\"terrain-raycast-scene-set\","
                << "\"renderSetBindingCount\":1,"
                << "\"drawMode\":\"render-set-indexed-indirect\","
                << "\"invocationCount\":1,"
                << "\"drawCommandCount\":1,"
                << "\"usesStandaloneGeometry\":false,"
                << "\"usesExplicitDrawCount\":false}]\n"
                << "  }],\n"
                << "  \"usesRenderEntityID\":true,\n"
                << "  \"usesRenderEntityInstanceID\":true,\n"
                << "  \"directDrawFallback\":false\n"
                << "}\n";
        }
        captureWritten = true;
    }

    void WebglGeometryTerrainRaycastRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;
        (void)options;
        for (TerrainRaycastEntityState &entity : entities)
        {
            entity.vertices.clear();
            entity.indices.clear();
        }
        heights.clear();
        grain.clear();
        terrainTexture.clear();
    }
} // namespace GVM::ThreeSamples
