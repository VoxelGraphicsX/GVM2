#pragma once
#include "PCGForestTypesAndMath.hpp"

namespace PCGForestSample
{
    // =========================================================================
    // Stage 0: Deterministic Quadtree Subdivision Predicate Node
    // =========================================================================

    inline uint evaluateQuadtreeSplitCPU(IN QuadtreeNodeSpec node, IN PCGForestParams params)
    {
        return shouldSubdivideQuadtreeNode(node.minX, node.minZ, node.nodeSize, node.depth, params) ? 1u : 0u;
    }

    // =========================================================================
    // Stage 1: Per-Leaf Terrain Heightfield + CurveWave Road + Skirts Mesh
    // =========================================================================

    class GenerateQuadtreeLeafTerrainCPU
    {
    public:
        QuadtreeLeafInput leaf;

        ChunkTerrainData operator()(IN PCGForestParams params) const
        {
            ChunkTerrainData out;
            out.chunkSize = leaf.leafSize;
            out.minX = leaf.minX;
            out.minZ = leaf.minZ;
            out.depth = leaf.depth;

            const uint subdivs = params.terrainSubdivisions >= 8u
                ? (params.terrainSubdivisions <= 80u ? params.terrainSubdivisions : 80u)
                : 8u;
            const uint side = subdivs + 1u;
            out.gridSide = side;
            out.heights.resize(side * side);
            out.slopes.resize(side * side);
            out.roadFactors.resize(side * side);

            const float step = out.chunkSize / float(subdivs);
            eastl::vector<float4> normals(side * side);

            for (uint gz = 0u; gz < side; ++gz)
            {
                const float wz = out.minZ + float(gz) * step;
                for (uint gx = 0u; gx < side; ++gx)
                {
                    const float wx = out.minX + float(gx) * step;
                    const TerrainFieldSample centerSample = pcgSampleTerrainField(
                        wx, wz, params.fbmOctaves, params.seed,
                        params.pathAmplitude, params.pathFrequency, params.pathWidth, params.pathFade);
                    const float hC = centerSample.height;
                    const float eps = 0.65f;
                    const float hR = evaluateTerrainHeight(
                        wx + eps, wz, params.fbmOctaves, params.seed,
                        params.pathAmplitude, params.pathFrequency, params.pathWidth, params.pathFade);
                    const float hL = evaluateTerrainHeight(
                        wx - eps, wz, params.fbmOctaves, params.seed,
                        params.pathAmplitude, params.pathFrequency, params.pathWidth, params.pathFade);
                    const float hU = evaluateTerrainHeight(
                        wx, wz + eps, params.fbmOctaves, params.seed,
                        params.pathAmplitude, params.pathFrequency, params.pathWidth, params.pathFade);
                    const float hD = evaluateTerrainHeight(
                        wx, wz - eps, params.fbmOctaves, params.seed,
                        params.pathAmplitude, params.pathFrequency, params.pathWidth, params.pathFade);

                    const float dhdx = (hR - hL) / (2.0f * eps);
                    const float dhdz = (hU - hD) / (2.0f * eps);
                    float nx = -dhdx;
                    float ny = 1.0f;
                    float nz = -dhdz;
                    const float invLen = 1.0f / pcgMax(pcgSqrt(nx * nx + ny * ny + nz * nz), 1e-5f);
                    nx *= invLen;
                    ny *= invLen;
                    nz *= invLen;

                    const float slope = pcgClamp(1.0f - ny, 0.0f, 1.0f);
                    const float roadFactor = centerSample.roadFactor;

                    const uint idx = gz * side + gx;
                    out.heights[idx] = hC;
                    out.slopes[idx] = slope;
                    out.roadFactors[idx] = roadFactor;
                    normals[idx] = float4(nx, ny, nz, slope);
                }
            }

            // Main grid quads + 4 perimeter edge skirts (6 vertices per quad) for crack-free LOD seams.
            const uint totalQuads = subdivs * subdivs + 4u * subdivs;
            out.vertices.resize(totalQuads * 6u);
            const float lodDepthFloat = float(leaf.depth);
            const float skirtDepth = leaf.leafSize * 0.08f;
            uint writeIdx = 0u;

            for (uint gz = 0u; gz < subdivs; ++gz)
            {
                const float z0 = out.minZ + float(gz) * step;
                const float z1 = z0 + step;
                for (uint gx = 0u; gx < subdivs; ++gx)
                {
                    const float x0 = out.minX + float(gx) * step;
                    const float x1 = x0 + step;

                    const uint i00 = gz * side + gx;
                    const uint i10 = i00 + 1u;
                    const uint i01 = (gz + 1u) * side + gx;
                    const uint i11 = i01 + 1u;

                    TerrainVertex v00;
                    v00.posAndRoad = float4(x0, out.heights[i00], z0, out.roadFactors[i00]);
                    v00.normalAndSlope = normals[i00];
                    v00.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex v10;
                    v10.posAndRoad = float4(x1, out.heights[i10], z0, out.roadFactors[i10]);
                    v10.normalAndSlope = normals[i10];
                    v10.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex v01;
                    v01.posAndRoad = float4(x0, out.heights[i01], z1, out.roadFactors[i01]);
                    v01.normalAndSlope = normals[i01];
                    v01.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex v11;
                    v11.posAndRoad = float4(x1, out.heights[i11], z1, out.roadFactors[i11]);
                    v11.normalAndSlope = normals[i11];
                    v11.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    out.vertices[writeIdx + 0u] = v00;
                    out.vertices[writeIdx + 1u] = v01;
                    out.vertices[writeIdx + 2u] = v10;
                    out.vertices[writeIdx + 3u] = v10;
                    out.vertices[writeIdx + 4u] = v01;
                    out.vertices[writeIdx + 5u] = v11;
                    writeIdx += 6u;
                }
            }

            // Perimeter skirts along South (gz=0), North (gz=subdivs), West (gx=0), East (gx=subdivs).
            // Border normals, slopes, roadFactors, and LOD depth are preserved for seamless shading.
            for (uint i = 0u; i < subdivs; ++i)
            {
                const float x0 = out.minX + float(i) * step;
                const float x1 = x0 + step;
                const float zS = out.minZ;
                const float zN = out.minZ + out.chunkSize;

                // South edge (gz = 0)
                {
                    const uint ia = i;
                    const uint ib = i + 1u;
                    TerrainVertex aTop;
                    aTop.posAndRoad = float4(x0, out.heights[ia], zS, out.roadFactors[ia]);
                    aTop.normalAndSlope = normals[ia];
                    aTop.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex bTop;
                    bTop.posAndRoad = float4(x1, out.heights[ib], zS, out.roadFactors[ib]);
                    bTop.normalAndSlope = normals[ib];
                    bTop.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex aBot = aTop;
                    aBot.posAndRoad.y -= skirtDepth;
                    aBot.lodInfo.y = 1.0f;

                    TerrainVertex bBot = bTop;
                    bBot.posAndRoad.y -= skirtDepth;
                    bBot.lodInfo.y = 1.0f;

                    out.vertices[writeIdx + 0u] = aTop;
                    out.vertices[writeIdx + 1u] = bTop;
                    out.vertices[writeIdx + 2u] = aBot;
                    out.vertices[writeIdx + 3u] = bTop;
                    out.vertices[writeIdx + 4u] = bBot;
                    out.vertices[writeIdx + 5u] = aBot;
                    writeIdx += 6u;
                }

                // North edge (gz = subdivs)
                {
                    const uint ia = subdivs * side + i;
                    const uint ib = ia + 1u;
                    TerrainVertex aTop;
                    aTop.posAndRoad = float4(x0, out.heights[ia], zN, out.roadFactors[ia]);
                    aTop.normalAndSlope = normals[ia];
                    aTop.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex bTop;
                    bTop.posAndRoad = float4(x1, out.heights[ib], zN, out.roadFactors[ib]);
                    bTop.normalAndSlope = normals[ib];
                    bTop.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex aBot = aTop;
                    aBot.posAndRoad.y -= skirtDepth;
                    aBot.lodInfo.y = 1.0f;

                    TerrainVertex bBot = bTop;
                    bBot.posAndRoad.y -= skirtDepth;
                    bBot.lodInfo.y = 1.0f;

                    out.vertices[writeIdx + 0u] = aTop;
                    out.vertices[writeIdx + 1u] = aBot;
                    out.vertices[writeIdx + 2u] = bTop;
                    out.vertices[writeIdx + 3u] = bTop;
                    out.vertices[writeIdx + 4u] = aBot;
                    out.vertices[writeIdx + 5u] = bBot;
                    writeIdx += 6u;
                }

                const float z0 = out.minZ + float(i) * step;
                const float z1 = z0 + step;
                const float xW = out.minX;
                const float xE = out.minX + out.chunkSize;

                // West edge (gx = 0)
                {
                    const uint ia = i * side;
                    const uint ib = (i + 1u) * side;
                    TerrainVertex aTop;
                    aTop.posAndRoad = float4(xW, out.heights[ia], z0, out.roadFactors[ia]);
                    aTop.normalAndSlope = normals[ia];
                    aTop.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex bTop;
                    bTop.posAndRoad = float4(xW, out.heights[ib], z1, out.roadFactors[ib]);
                    bTop.normalAndSlope = normals[ib];
                    bTop.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex aBot = aTop;
                    aBot.posAndRoad.y -= skirtDepth;
                    aBot.lodInfo.y = 1.0f;

                    TerrainVertex bBot = bTop;
                    bBot.posAndRoad.y -= skirtDepth;
                    bBot.lodInfo.y = 1.0f;

                    out.vertices[writeIdx + 0u] = aTop;
                    out.vertices[writeIdx + 1u] = aBot;
                    out.vertices[writeIdx + 2u] = bTop;
                    out.vertices[writeIdx + 3u] = bTop;
                    out.vertices[writeIdx + 4u] = aBot;
                    out.vertices[writeIdx + 5u] = bBot;
                    writeIdx += 6u;
                }

                // East edge (gx = subdivs)
                {
                    const uint ia = i * side + subdivs;
                    const uint ib = (i + 1u) * side + subdivs;
                    TerrainVertex aTop;
                    aTop.posAndRoad = float4(xE, out.heights[ia], z0, out.roadFactors[ia]);
                    aTop.normalAndSlope = normals[ia];
                    aTop.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex bTop;
                    bTop.posAndRoad = float4(xE, out.heights[ib], z1, out.roadFactors[ib]);
                    bTop.normalAndSlope = normals[ib];
                    bTop.lodInfo = float4(lodDepthFloat, 0.0f, 0.0f, 0.0f);

                    TerrainVertex aBot = aTop;
                    aBot.posAndRoad.y -= skirtDepth;
                    aBot.lodInfo.y = 1.0f;

                    TerrainVertex bBot = bTop;
                    bBot.posAndRoad.y -= skirtDepth;
                    bBot.lodInfo.y = 1.0f;

                    out.vertices[writeIdx + 0u] = aTop;
                    out.vertices[writeIdx + 1u] = bTop;
                    out.vertices[writeIdx + 2u] = aBot;
                    out.vertices[writeIdx + 3u] = bTop;
                    out.vertices[writeIdx + 4u] = bBot;
                    out.vertices[writeIdx + 5u] = aBot;
                    writeIdx += 6u;
                }
            }

            return out;
        }
    };

    // =========================================================================
    // Stage 2: Priority Forest Tree Scatter + Spatial-Hash Obstacle Rejection
    // =========================================================================

    class ScatterForestObstaclesCPU
    {
    public:
        QuadtreeLeafInput leaf;

        ChunkObstacleResult operator()(IN PCGForestParams params, IN ChunkTerrainData terrain) const
        {
            ChunkObstacleResult result;
            const float cell = pcgMax(params.treeCellSize, 1.4f);
            const uint cellsPerAxis = uint(terrain.chunkSize / cell) + 1u;
            const uint chunkSeed = params.seed ^ (leaf.leafCode * 0x45d9f3bu + leaf.depth * 977u + 17u);

            // 8x8 chunk-local spatial hash grid (storing index+1, linked via nextInBucket)
            constexpr uint kBuckets = 64u;
            eastl::vector<int> bucketHeads(kBuckets, -1);
            eastl::vector<int> nextObstacle;
            nextObstacle.reserve(cellsPerAxis * cellsPerAxis);

            for (uint cz = 0u; cz < cellsPerAxis; ++cz)
            {
                for (uint cx = 0u; cx < cellsPerAxis; ++cx)
                {
                    const float jx = pcgHash01(int(cx), int(cz), chunkSeed + 1u);
                    const float jz = pcgHash01(int(cx), int(cz), chunkSeed + 2u);
                    const float wx = terrain.minX + (float(cx) + 0.15f + 0.70f * jx) * cell;
                    const float wz = terrain.minZ + (float(cz) + 0.15f + 0.70f * jz) * cell;
                    if (wx >= terrain.minX + terrain.chunkSize || wz >= terrain.minZ + terrain.chunkSize)
                    {
                        continue;
                    }

                    // Forest grove macro density modulation + CurveWave trail clearance.
                    const float grove = pcgFbmRidged2D(wx * 0.032f + 4.1f, wz * 0.032f - 2.7f, 3u, params.seed + 303u);
                    const float pick = pcgHash01(int(cx) + 31, int(cz) - 19, chunkSeed + 3u);
                    if (pick > grove * 1.15f)
                    {
                        continue;
                    }

                    const float road = terrain.sampleRoad(wx, wz);
                    if (road > 0.18f)
                    {
                        continue;
                    }

                    const float slope = terrain.sampleSlope(wx, wz);
                    const float height = terrain.sampleHeight(wx, wz);
                    if (slope > params.treeMaxSlope || height > 18.5f)
                    {
                        continue;
                    }

                    const int bx = int(pcgClamp((wx - terrain.minX) / terrain.chunkSize * 8.0f, 0.0f, 7.0f));
                    const int bz = int(pcgClamp((wz - terrain.minZ) / terrain.chunkSize * 8.0f, 0.0f, 7.0f));
                    const float minDist = params.treeObstacleRadius;
                    const float minDistSq = minDist * minDist;
                    bool rejected = false;

                    for (int dz = -1; dz <= 1 && !rejected; ++dz)
                    {
                        const int nbz = bz + dz;
                        if (nbz < 0 || nbz >= 8)
                        {
                            continue;
                        }
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const int nbx = bx + dx;
                            if (nbx < 0 || nbx >= 8)
                            {
                                continue;
                            }
                            int node = bucketHeads[uint(nbz * 8 + nbx)];
                            while (node >= 0)
                            {
                                const ObstacleRecord existing = result.obstacles[uint(node)];
                                const float ex = wx - existing.x;
                                const float ez = wz - existing.z;
                                if (ex * ex + ez * ez < minDistSq)
                                {
                                    rejected = true;
                                    break;
                                }
                                node = nextObstacle[uint(node)];
                            }
                            if (rejected)
                            {
                                break;
                            }
                        }
                    }

                    if (rejected)
                    {
                        continue;
                    }

                    const float scale = 0.85f + 0.68f * pcgHash01(int(cx) * 7, int(cz) * 13, chunkSeed + 5u);
                    const float yaw = 6.2831853f * pcgHash01(int(cx) + 11, int(cz) + 29, chunkSeed + 7u);
                    const float tint = pcgHash01(int(cx) + 53, int(cz) + 71, chunkSeed + 9u);

                    ObstacleRecord obs;
                    obs.x = wx;
                    obs.z = wz;
                    obs.trunkRadius = 0.65f * scale;
                    obs.canopyRadius = 2.7f * scale;

                    const int newIndex = int(result.obstacles.size());
                    const uint bucketIdx = uint(bz * 8 + bx);
                    nextObstacle.push_back(bucketHeads[bucketIdx]);
                    bucketHeads[bucketIdx] = newIndex;
                    result.obstacles.push_back(obs);

                    PCGInstanceData inst;
                    inst.posScale = float4(wx, height - 0.12f, wz, scale);
                    inst.rotWindTint = float4(yaw, 0.0f, tint, grove);
                    result.trees.push_back(inst);
                }
            }
            return result;
        }
    };

    // =========================================================================
    // Stage 3A: Understory Rock Scatter (Runs in Parallel with Grass Scatter)
    // =========================================================================

    class ScatterUnderstoryRocksCPU
    {
    public:
        QuadtreeLeafInput leaf;

        eastl::vector<PCGInstanceData> operator()(
            IN PCGForestParams params,
            IN ChunkTerrainData terrain,
            IN ChunkObstacleResult obstacleData) const
        {
            eastl::vector<PCGInstanceData> rocks;
            const float cell = pcgMax(params.rockCellSize, 1.5f);
            const uint cellsPerAxis = uint(terrain.chunkSize / cell) + 1u;
            const uint chunkSeed = params.seed ^ (leaf.leafCode * 0x27d4eb2du + leaf.depth * 613u + 911u);
            const float clearanceSq = params.rockParentClearance * params.rockParentClearance;
            const uint obstacleCount = uint(obstacleData.obstacles.size());

            for (uint cz = 0u; cz < cellsPerAxis; ++cz)
            {
                for (uint cx = 0u; cx < cellsPerAxis; ++cx)
                {
                    const float wx = terrain.minX + (float(cx) + 0.12f + 0.76f * pcgHash01(int(cx), int(cz), chunkSeed)) * cell;
                    const float wz = terrain.minZ + (float(cz) + 0.12f + 0.76f * pcgHash01(int(cx), int(cz), chunkSeed + 1u)) * cell;
                    if (wx >= terrain.minX + terrain.chunkSize || wz >= terrain.minZ + terrain.chunkSize)
                    {
                        continue;
                    }

                    const float road = terrain.sampleRoad(wx, wz);
                    if (road > 0.52f)
                    {
                        continue;
                    }

                    const float slope = terrain.sampleSlope(wx, wz);
                    // Rocks favor trail shoulders and steeper rocky outcrops.
                    const float shoulderAffinity = (road > 0.08f && road <= 0.52f) ? 0.35f : 0.0f;
                    const float rockProb = 0.15f + slope * 0.72f + shoulderAffinity;
                    if (pcgHash01(int(cx) + 19, int(cz) + 47, chunkSeed + 2u) > rockProb)
                    {
                        continue;
                    }

                    bool blocked = false;
                    for (uint i = 0u; i < obstacleCount; ++i)
                    {
                        const ObstacleRecord obs = obstacleData.obstacles[i];
                        const float dx = wx - obs.x;
                        const float dz = wz - obs.z;
                        if (dx * dx + dz * dz < clearanceSq)
                        {
                            blocked = true;
                            break;
                        }
                    }
                    if (blocked)
                    {
                        continue;
                    }

                    const float height = terrain.sampleHeight(wx, wz);
                    const float scale = 0.45f + 0.85f * pcgHash01(int(cx) + 73, int(cz) + 89, chunkSeed + 3u);
                    const float yaw = 6.2831853f * pcgHash01(int(cx) + 101, int(cz) + 13, chunkSeed + 4u);
                    const float tint = pcgHash01(int(cx) + 5, int(cz) + 127, chunkSeed + 5u);

                    PCGInstanceData rock;
                    rock.posScale = float4(wx, height - 0.08f, wz, scale);
                    rock.rotWindTint = float4(yaw, 0.0f, tint, slope);
                    rocks.push_back(rock);
                }
            }
            return rocks;
        }
    };

    // =========================================================================
    // Stage 3B: Clustered Grass Scatter (LOD-Scaled Density + Canopy Exclusion)
    // =========================================================================

    class ScatterClusteredGrassCPU
    {
    public:
        QuadtreeLeafInput leaf;

        eastl::vector<PCGInstanceData> operator()(
            IN PCGForestParams params,
            IN ChunkTerrainData terrain,
            IN ChunkObstacleResult obstacleData) const
        {
            eastl::vector<PCGInstanceData> blades;
            const float cell = pcgMax(params.grassCellSize, 0.55f);
            const uint cellsPerAxis = uint(terrain.chunkSize / cell) + 1u;
            const uint baseCluster = params.grassBladesPerCluster > 0u
                ? (params.grassBladesPerCluster <= 10u ? params.grassBladesPerCluster : 10u)
                : 4u;

            // Concentrate grass density smoothly on finer / nearer LOD leaves while preserving
            // proportional area coverage across the 240m x 240m adaptive quadtree.
            float lodDensityScale = 1.0f;
            uint perCluster = baseCluster;
            if (leaf.depth <= 1u)
            {
                lodDensityScale = 0.24f;
                perCluster = baseCluster > 2u ? (baseCluster / 2u) : baseCluster;
            }
            else if (leaf.depth == 2u)
            {
                lodDensityScale = 0.62f;
                perCluster = baseCluster > 2u ? (baseCluster - 1u) : baseCluster;
            }

            const float areaRatio = (terrain.chunkSize / 30.0f) * (terrain.chunkSize / 30.0f);
            const uint estimatedBlades = uint(pcgMax(16.0f, 360.0f * areaRatio * lodDensityScale));
            blades.reserve(estimatedBlades);

            const uint chunkSeed = params.seed ^ (leaf.leafCode * 0x165667b1u + leaf.depth * 1301u + 4099u);
            const uint obstacleCount = uint(obstacleData.obstacles.size());

            for (uint cz = 0u; cz < cellsPerAxis; ++cz)
            {
                for (uint cx = 0u; cx < cellsPerAxis; ++cx)
                {
                    const float wx = terrain.minX + (float(cx) + 0.15f + 0.70f * pcgHash01(int(cx), int(cz), chunkSeed)) * cell;
                    const float wz = terrain.minZ + (float(cz) + 0.15f + 0.70f * pcgHash01(int(cx), int(cz), chunkSeed + 1u)) * cell;
                    if (wx >= terrain.minX + terrain.chunkSize || wz >= terrain.minZ + terrain.chunkSize)
                    {
                        continue;
                    }

                    const float road = terrain.sampleRoad(wx, wz);
                    if (road > 0.25f)
                    {
                        continue;
                    }

                    const float slope = terrain.sampleSlope(wx, wz);
                    const float centerHeight = terrain.sampleHeight(wx, wz);
                    if (slope > 0.54f || centerHeight > 17.5f)
                    {
                        continue;
                    }

                    // Meadow patch FBM with reduced understory density beneath tree canopies.
                    const float patchNoise = pcgFbmRidged2D(
                        wx * params.grassPatchFrequency, wz * params.grassPatchFrequency, 3u, params.seed + 9001u);
                    float density = pcgSmoothstep(0.24f, 0.72f, patchNoise) * (1.0f - road * 3.2f) * lodDensityScale;

                    bool insideTrunk = false;
                    bool underCanopy = false;
                    for (uint i = 0u; i < obstacleCount; ++i)
                    {
                        const ObstacleRecord obs = obstacleData.obstacles[i];
                        const float dx = wx - obs.x;
                        const float dz = wz - obs.z;
                        const float d2 = dx * dx + dz * dz;
                        if (d2 < obs.trunkRadius * obs.trunkRadius)
                        {
                            insideTrunk = true;
                            break;
                        }
                        if (d2 < obs.canopyRadius * obs.canopyRadius)
                        {
                            underCanopy = true;
                        }
                    }
                    if (insideTrunk)
                    {
                        continue;
                    }
                    if (underCanopy)
                    {
                        density *= params.grassOccupiedDensity;
                    }

                    if (pcgHash01(int(cx) + 17, int(cz) + 37, chunkSeed + 2u) > density)
                    {
                        continue;
                    }

                    const float clusterRadius = cell * 0.55f;
                    for (uint b = 0u; b < perCluster; ++b)
                    {
                        const uint bladeSeed = chunkSeed + 100u + b * 31u;
                        const float angle = 6.2831853f * pcgHash01(int(cx), int(cz), bladeSeed);
                        const float dist = clusterRadius * pcgSqrt(pcgHash01(int(cx) + 7, int(cz) + 11, bladeSeed + 1u));
                        const float bx = wx + pcgCos(angle) * dist;
                        const float bz = wz + pcgSin(angle) * dist;
                        const float bh = terrain.sampleHeight(bx, bz);
                        const float bladeScale = 0.65f + 0.65f * patchNoise * pcgHash01(int(cx) + 3, int(cz) + 5, bladeSeed + 2u);
                        const float bladeYaw = 6.2831853f * pcgHash01(int(cx) + 23, int(cz) + 41, bladeSeed + 3u);
                        const float bladeTint = pcgClamp(patchNoise * 0.7f + 0.3f * pcgHash01(int(cx), int(cz), bladeSeed + 4u), 0.0f, 1.0f);

                        PCGInstanceData blade;
                        blade.posScale = float4(bx, bh - 0.03f, bz, bladeScale);
                        blade.rotWindTint = float4(bladeYaw, 0.0f, bladeTint, underCanopy ? 0.25f : 1.0f);
                        blades.push_back(blade);
                    }
                }
            }
            return blades;
        }
    };

    // =========================================================================
    // Stage 4: Chunk Packing & Parallel Quadtree Merge Helpers
    // =========================================================================

    inline ChunkGeometryPack packChunkGeometry(
        IN ChunkTerrainData terrain,
        IN ChunkObstacleResult obstacleData,
        IN eastl::vector<PCGInstanceData> rocks,
        IN eastl::vector<PCGInstanceData> grass,
        IN uint lodDepth)
    {
        ChunkGeometryPack pack;
        pack.terrainVertices = terrain.vertices;
        pack.trees = obstacleData.trees;
        pack.rocks = rocks;
        pack.grass = grass;
        pack.leafCount = 1u;
        if (lodDepth == 0u)
        {
            pack.lodLeafCount0 = 1u;
        }
        else if (lodDepth == 1u)
        {
            pack.lodLeafCount1 = 1u;
        }
        else if (lodDepth == 2u)
        {
            pack.lodLeafCount2 = 1u;
        }
        else
        {
            pack.lodLeafCount3 = 1u;
        }
        return pack;
    }

    inline ChunkGeometryPack mergeChunkGeometry(IN ChunkGeometryPack a, IN ChunkGeometryPack b)
    {
        ChunkGeometryPack merged;

        const uint aVerts = uint(a.terrainVertices.size());
        const uint bVerts = uint(b.terrainVertices.size());
        merged.terrainVertices.resize(aVerts + bVerts);
        for (uint i = 0u; i < aVerts; ++i)
        {
            merged.terrainVertices[i] = a.terrainVertices[i];
        }
        for (uint i = 0u; i < bVerts; ++i)
        {
            merged.terrainVertices[aVerts + i] = b.terrainVertices[i];
        }

        const uint aTrees = uint(a.trees.size());
        const uint bTrees = uint(b.trees.size());
        merged.trees.resize(aTrees + bTrees);
        for (uint i = 0u; i < aTrees; ++i)
        {
            merged.trees[i] = a.trees[i];
        }
        for (uint i = 0u; i < bTrees; ++i)
        {
            merged.trees[aTrees + i] = b.trees[i];
        }

        const uint aRocks = uint(a.rocks.size());
        const uint bRocks = uint(b.rocks.size());
        merged.rocks.resize(aRocks + bRocks);
        for (uint i = 0u; i < aRocks; ++i)
        {
            merged.rocks[i] = a.rocks[i];
        }
        for (uint i = 0u; i < bRocks; ++i)
        {
            merged.rocks[aRocks + i] = b.rocks[i];
        }

        const uint aGrass = uint(a.grass.size());
        const uint bGrass = uint(b.grass.size());
        merged.grass.resize(aGrass + bGrass);
        for (uint i = 0u; i < aGrass; ++i)
        {
            merged.grass[i] = a.grass[i];
        }
        for (uint i = 0u; i < bGrass; ++i)
        {
            merged.grass[aGrass + i] = b.grass[i];
        }

        merged.leafCount = a.leafCount + b.leafCount;
        merged.lodLeafCount0 = a.lodLeafCount0 + b.lodLeafCount0;
        merged.lodLeafCount1 = a.lodLeafCount1 + b.lodLeafCount1;
        merged.lodLeafCount2 = a.lodLeafCount2 + b.lodLeafCount2;
        merged.lodLeafCount3 = a.lodLeafCount3 + b.lodLeafCount3;

        return merged;
    }

    inline ForestFrameSummary summarizeQuadrants(
        IN ChunkGeometryPack q0,
        IN ChunkGeometryPack q1,
        IN ChunkGeometryPack q2,
        IN ChunkGeometryPack q3,
        IN uint nestedSubgraphCount)
    {
        ForestFrameSummary summary;
        summary.chunkCount = q0.leafCount + q1.leafCount + q2.leafCount + q3.leafCount;
        summary.lodLeafCounts[0] = q0.lodLeafCount0 + q1.lodLeafCount0 + q2.lodLeafCount0 + q3.lodLeafCount0;
        summary.lodLeafCounts[1] = q0.lodLeafCount1 + q1.lodLeafCount1 + q2.lodLeafCount1 + q3.lodLeafCount1;
        summary.lodLeafCounts[2] = q0.lodLeafCount2 + q1.lodLeafCount2 + q2.lodLeafCount2 + q3.lodLeafCount2;
        summary.lodLeafCounts[3] = q0.lodLeafCount3 + q1.lodLeafCount3 + q2.lodLeafCount3 + q3.lodLeafCount3;
        summary.nestedSubgraphCount = nestedSubgraphCount;
        const uint totalVerts =
            uint(q0.terrainVertices.size() + q1.terrainVertices.size() +
                 q2.terrainVertices.size() + q3.terrainVertices.size());
        summary.terrainTriangleCount = totalVerts / 3u;
        summary.treeCount = uint(q0.trees.size() + q1.trees.size() + q2.trees.size() + q3.trees.size());
        summary.rockCount = uint(q0.rocks.size() + q1.rocks.size() + q2.rocks.size() + q3.rocks.size());
        summary.grassBladeCount = uint(q0.grass.size() + q1.grass.size() + q2.grass.size() + q3.grass.size());
        return summary;
    }

    inline ForestFrameSummary finalizeSwapchainSummary(IN ForestFrameSummary summary, IN uint presentToken)
    {
        ForestFrameSummary out = summary;
        if (presentToken == 0u)
        {
            out.chunkCount = 0u;
        }
        return out;
    }

    inline ForestBenchmarkResult evaluateBenchmarkPixels(
        IN ForestFrameSummary summary,
        IN eastl::vector<uint8_t> pixels)
    {
        ForestBenchmarkResult result;
        result.summary = summary;
        uint nonSky = 0u;
        uint hash = 2166136261u;
        const uint byteCount = uint(pixels.size());
        for (uint i = 0u; i + 3u < byteCount; i += 4u)
        {
            const uint r = uint(pixels[i + 0u]);
            const uint g = uint(pixels[i + 1u]);
            const uint b = uint(pixels[i + 2u]);
            // Clear sky color is ~(148, 189, 232) -> (0.58, 0.74, 0.91).
            const int dr = int(r) - 148;
            const int dg = int(g) - 189;
            const int db = int(b) - 232;
            if (dr * dr + dg * dg + db * db > 144)
            {
                ++nonSky;
            }
            if ((i & 63u) == 0u)
            {
                hash = (hash ^ (r | (g << 8u) | (b << 16u))) * 16777619u;
            }
        }
        result.nonSkyPixelCount = nonSky;
        result.pixelChecksum = hash;
        return result;
    }
} // namespace PCGForestSample
