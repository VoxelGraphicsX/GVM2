#pragma once
#include "UGL.h"
#include <EASTL/vector.h>

namespace PCGForestSample
{
    using namespace UGL;

    // =========================================================================
    // 1. Self-Contained Deterministic Math (UGLC Source-Effect Validated)
    // =========================================================================

    inline float pcgAbs(float x)
    {
        return x < 0.0f ? -x : x;
    }

    inline float pcgMin(float a, float b)
    {
        return a < b ? a : b;
    }

    inline float pcgMax(float a, float b)
    {
        return a > b ? a : b;
    }

    inline float pcgClamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline float pcgFloor(float x)
    {
        const int i = int(x);
        return (x < float(i)) ? float(i - 1) : float(i);
    }

    inline float pcgFract(float x)
    {
        return x - pcgFloor(x);
    }

    inline float pcgSmoothstep(float edge0, float edge1, float x)
    {
        const float denom = edge1 - edge0;
        const float t = denom > 1e-6f ? pcgClamp((x - edge0) / denom, 0.0f, 1.0f) : 0.0f;
        return t * t * (3.0f - 2.0f * t);
    }

    inline float pcgLerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    inline float pcgSin(float radians)
    {
        constexpr float kPi = 3.14159265359f;
        constexpr float kTwoPi = 6.28318530718f;
        const int turns = int(radians / kTwoPi);
        float x = radians - float(turns) * kTwoPi;
        if (x > kPi)
        {
            x -= kTwoPi;
        }
        else if (x < -kPi)
        {
            x += kTwoPi;
        }
        const float x2 = x * x;
        return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f))));
    }

    inline float pcgCos(float radians)
    {
        return pcgSin(radians + 1.57079632679f);
    }

    inline float pcgSqrt(float v)
    {
        if (v <= 1e-12f)
        {
            return 0.0f;
        }
        float g = v > 1.0f ? v * 0.5f : 1.0f;
        for (int iter = 0; iter < 5; ++iter)
        {
            g = 0.5f * (g + v / g);
        }
        return g;
    }

    inline uint pcgMixBits(uint x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    inline uint pcgHashCoords(int ix, int iz, uint seed)
    {
        const uint ux = uint(ix) * 0x9e3779b1u;
        const uint uz = uint(iz) * 0x85ebca77u;
        return pcgMixBits(ux ^ uz ^ (seed * 0xc2b2ae3du));
    }

    inline float pcgHash01(int ix, int iz, uint seed)
    {
        return float(pcgHashCoords(ix, iz, seed) & 0x00ffffffu) * (1.0f / 16777216.0f);
    }

    inline float pcgValueNoise2D(float x, float z, uint seed)
    {
        const float fx = pcgFloor(x);
        const float fz = pcgFloor(z);
        const int ix = int(fx);
        const int iz = int(fz);
        const float tx = x - fx;
        const float tz = z - fz;
        const float ux = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
        const float uz = tz * tz * tz * (tz * (tz * 6.0f - 15.0f) + 10.0f);

        const float v00 = pcgHash01(ix, iz, seed);
        const float v10 = pcgHash01(ix + 1, iz, seed);
        const float v01 = pcgHash01(ix, iz + 1, seed);
        const float v11 = pcgHash01(ix + 1, iz + 1, seed);
        return pcgLerp(pcgLerp(v00, v10, ux), pcgLerp(v01, v11, ux), uz);
    }

    /// Multi-octave FBM with ridged mountain crest detail for procedural terrain synthesis.
    inline float pcgFbmRidged2D(float x, float z, uint octaves, uint seed)
    {
        float sum = 0.0f;
        float amp = 0.52f;
        float freq = 1.0f;
        float norm = 0.0f;
        const uint safeOctaves = octaves > 0u ? (octaves <= 8u ? octaves : 8u) : 5u;

        for (uint o = 0u; o < safeOctaves; ++o)
        {
            const float n = pcgValueNoise2D(x * freq, z * freq, seed + o * 101u);
            const float ridge = 1.0f - pcgAbs(n * 2.0f - 1.0f);
            const float blended = pcgLerp(n, ridge * ridge, 0.35f);
            sum += blended * amp;
            norm += amp;
            // Domain rotation avoids grid-axis artifacts across octaves.
            const float rx = x * 0.8f - z * 0.6f;
            const float rz = x * 0.6f + z * 0.8f;
            x = rx;
            z = rz;
            freq *= 2.03f;
            amp *= 0.49f;
        }
        return norm > 0.0f ? (sum / norm) : 0.0f;
    }

    /// Evaluates signed centerline distance along Z with a secondary meander harmonic.
    inline float pcgCurveWavePathDistance(float worldX, float worldZ, float amplitude, float frequency, float phase)
    {
        const float centerX =
            amplitude * pcgSin(worldZ * frequency + phase) +
            0.35f * amplitude * pcgSin(worldZ * (frequency * 2.15f) - 0.7f + phase * 0.5f);
        return worldX - centerX;
    }

    struct TerrainFieldSample
    {
        float height = 0.0f;
        float roadFactor = 0.0f;
    };

    /// Evaluates continuous world terrain height and road mask at (worldX, worldZ).
    inline TerrainFieldSample pcgSampleTerrainField(
        float worldX,
        float worldZ,
        uint fbmOctaves,
        uint seed,
        float pathAmplitude,
        float pathFrequency,
        float pathWidth,
        float pathFade)
    {
        const float macro = pcgFbmRidged2D(worldX * 0.014f, worldZ * 0.014f, fbmOctaves, seed);
        const float detail = pcgFbmRidged2D(worldX * 0.055f + 13.7f, worldZ * 0.055f - 9.2f, 4u, seed + 7919u);
        const float basin = 0.0018f * (worldX * worldX + worldZ * worldZ * 0.65f);
        const float rawHeight = (macro - 0.38f) * 26.0f + (detail - 0.5f) * 3.4f + basin * 0.45f;

        // Carve a smooth, drivable forest trail along the CurveWave centerline.
        const float pathDist = pcgAbs(pcgCurveWavePathDistance(worldX, worldZ, pathAmplitude, pathFrequency, 0.35f));
        const float roadBlend = 1.0f - pcgSmoothstep(pathWidth * 0.65f, pathWidth + pathFade, pathDist);
        const float trailBase = (pcgFbmRidged2D(0.0f, worldZ * 0.011f, 3u, seed) - 0.38f) * 14.0f - 0.25f;

        TerrainFieldSample out;
        out.height = pcgLerp(rawHeight, trailBase, roadBlend * 0.82f);
        out.roadFactor = 1.0f - pcgSmoothstep(
            pathWidth * 0.55f, pathWidth + pathFade * 0.75f, pathDist);
        return out;
    }

    inline float evaluateTerrainHeight(
        float worldX,
        float worldZ,
        uint fbmOctaves,
        uint seed,
        float pathAmplitude,
        float pathFrequency,
        float pathWidth,
        float pathFade)
    {
        return pcgSampleTerrainField(
            worldX, worldZ, fbmOctaves, seed,
            pathAmplitude, pathFrequency, pathWidth, pathFade).height;
    }

    // =========================================================================
    // 2. Shared PCG & Rendering Data Structures
    // =========================================================================

    /// Host-prepared camera, lighting, and PCG controls passed by value into the TaskGraph.
    struct PCGForestParams
    {
        uint2 extent = uint2(960u, 640u);
        uint terrainSubdivisions = 24u;
        uint fbmOctaves = 6u;
        uint seed = 1337u;
        uint grassBladesPerCluster = 6u;

        float worldHalfSize = 120.0f;
        uint minQuadtreeDepth = 1u;
        uint maxQuadtreeDepth = 3u;
        float roughnessSplitThreshold = 3.60f;
        float lodDistanceScale = 1.0f;
        uint visualizeLodLevels = 0u;

        float pathWidth = 3.8f;
        float pathFade = 3.2f;
        float pathAmplitude = 18.0f;
        float pathFrequency = 0.042f;

        float treeCellSize = 4.6f;
        float treeObstacleRadius = 2.95f;
        float treeMaxSlope = 0.58f;

        float rockCellSize = 4.9f;
        float rockParentClearance = 2.0f;

        float grassCellSize = 0.80f;
        float grassPatchFrequency = 0.085f;
        float grassOccupiedDensity = 0.32f;

        float timeSeconds = 0.0f;
        float windStrength = 1.0f;

        // Row-major ViewProjection matrix rows + camera eye + sun direction.
        float4 viewProjRow0 = float4(1.0f, 0.0f, 0.0f, 0.0f);
        float4 viewProjRow1 = float4(0.0f, 1.0f, 0.0f, 0.0f);
        float4 viewProjRow2 = float4(0.0f, 0.0f, 1.0f, 0.0f);
        float4 viewProjRow3 = float4(0.0f, 0.0f, 0.0f, 1.0f);
        float4 eyePosition = float4(28.0f, 42.0f, -82.0f, 1.0f);
        float4 sunDirection = float4(0.48f, 0.76f, -0.44f, 0.0f);
    };

    /// Quadtree node bounds & Morton path code passed down recursive/wave subdivision.
    struct QuadtreeNodeSpec
    {
        uint quadrantIndex = 0u;
        uint leafCode = 0u;
        uint depth = 0u;
        float minX = 0.0f;
        float minZ = 0.0f;
        float nodeSize = 120.0f;
    };

    /// Owned input snapshot for one UGL [[Export]] leaf chunk subgraph.
    struct QuadtreeLeafInput
    {
        PCGForestParams params;
        uint quadrantIndex = 0u;
        uint leafCode = 0u;
        uint depth = 1u;
        float minX = 0.0f;
        float minZ = 0.0f;
        float leafSize = 60.0f;
    };

    /// Deterministic adaptive quadtree subdivision heuristic:
    /// probes a 5x5 height/road grid inside the candidate node, evaluates bilinear deviation
    /// roughness + max slope + road proximity + 3D distance to camera.
    inline bool shouldSubdivideQuadtreeNode(
        float minX,
        float minZ,
        float nodeSize,
        uint depth,
        IN PCGForestParams params)
    {
        const uint safeMaxDepth = params.maxQuadtreeDepth <= 4u ? params.maxQuadtreeDepth : 4u;
        const uint safeMinDepth = params.minQuadtreeDepth <= safeMaxDepth ? params.minQuadtreeDepth : safeMaxDepth;
        if (depth >= safeMaxDepth)
        {
            return false;
        }
        if (depth < safeMinDepth)
        {
            return true;
        }

        float probeHeights[25];
        float maxRoad = 0.0f;
        for (uint gz = 0u; gz < 5u; ++gz)
        {
            const float wz = minZ + (float(gz) * 0.25f) * nodeSize;
            for (uint gx = 0u; gx < 5u; ++gx)
            {
                const float wx = minX + (float(gx) * 0.25f) * nodeSize;
                const TerrainFieldSample s = pcgSampleTerrainField(
                    wx, wz, params.fbmOctaves, params.seed,
                    params.pathAmplitude, params.pathFrequency, params.pathWidth, params.pathFade);
                probeHeights[gz * 5u + gx] = s.height;
                if (s.roadFactor > maxRoad)
                {
                    maxRoad = s.roadFactor;
                }
            }
        }

        const float h00 = probeHeights[0];
        const float h10 = probeHeights[4];
        const float h01 = probeHeights[20];
        const float h11 = probeHeights[24];

        float maxDeviation = 0.0f;
        for (uint gz = 0u; gz < 5u; ++gz)
        {
            const float v = float(gz) * 0.25f;
            for (uint gx = 0u; gx < 5u; ++gx)
            {
                const float u = float(gx) * 0.25f;
                const float hBilinear = pcgLerp(pcgLerp(h00, h10, u), pcgLerp(h01, h11, u), v);
                const float dev = pcgAbs(probeHeights[gz * 5u + gx] - hBilinear);
                if (dev > maxDeviation)
                {
                    maxDeviation = dev;
                }
            }
        }

        const float invStep = nodeSize > 1e-4f ? (4.0f / nodeSize) : 0.0f;
        float maxSlope = 0.0f;
        for (uint gz = 0u; gz < 5u; ++gz)
        {
            for (uint gx = 0u; gx < 5u; ++gx)
            {
                const float hC = probeHeights[gz * 5u + gx];
                if (gx + 1u < 5u)
                {
                    const float sx = pcgAbs(probeHeights[gz * 5u + (gx + 1u)] - hC) * invStep;
                    if (sx > maxSlope)
                    {
                        maxSlope = sx;
                    }
                }
                if (gz + 1u < 5u)
                {
                    const float sz = pcgAbs(probeHeights[(gz + 1u) * 5u + gx] - hC) * invStep;
                    if (sz > maxSlope)
                    {
                        maxSlope = sz;
                    }
                }
            }
        }

        const float chordRef = pcgMax(nodeSize * 0.04f, 0.5f);
        const float roughness = (maxDeviation / chordRef) + maxSlope * 1.5f;
        const float cx = minX + 0.5f * nodeSize;
        const float cz = minZ + 0.5f * nodeSize;
        const float cy = probeHeights[12];

        const float dx = cx - params.eyePosition.x;
        const float dy = cy - params.eyePosition.y;
        const float dz = cz - params.eyePosition.z;
        const float distanceToCamera = pcgSqrt(dx * dx + dy * dy + dz * dz);

        const float distScale = pcgClamp(params.lodDistanceScale, 0.25f, 3.0f);
        const float splitRadiusForDepth = (nodeSize * 1.15f + 16.0f) * distScale;
        const float depthRoughnessThresh = params.roughnessSplitThreshold * (depth <= 1u ? 1.0f : 1.45f);
        const bool hasRoadNearCamera = (maxRoad > 0.15f) && (distanceToCamera < splitRadiusForDepth * 1.15f);

        return (distanceToCamera < splitRadiusForDepth) ||
               (roughness > depthRoughnessThresh) ||
               hasRoadNearCamera;
    }

    /// Terrain vertex uploaded per quadrant: xyz=worldPos, w=roadFactor; normalAndSlope: xyz=normal, w=slope;
    /// lodInfo: x=float(lodDepth), y=skirtFlag (0.0 surface, 1.0 skirt).
    struct TerrainVertex
    {
        float4 posAndRoad;
        float4 normalAndSlope;
        float4 lodInfo;
    };

    /// Compact 32-byte instance record shared by Trees, Rocks, and Grass blades.
    /// posScale: xyz=worldPos, w=uniformScale
    /// rotWindTint: x=yawRadians, y=windSwayOffset, z=colorTint01, w=variant01
    struct PCGInstanceData
    {
        float4 posScale;
        float4 rotWindTint;
    };

    /// Priority obstacle emitted by Forest tree scatter and queried by Rocks & Grass layers.
    struct ObstacleRecord
    {
        float x = 0.0f;
        float z = 0.0f;
        float trunkRadius = 1.0f;
        float canopyRadius = 3.0f;
    };

    /// Sampled terrain field + generated triangle list for one quadtree leaf chunk.
    struct ChunkTerrainData
    {
        float minX = 0.0f;
        float minZ = 0.0f;
        float chunkSize = 60.0f;
        uint gridSide = 25u;
        uint depth = 1u;
        eastl::vector<float> heights;
        eastl::vector<float> slopes;
        eastl::vector<float> roadFactors;
        eastl::vector<TerrainVertex> vertices;

        float sampleHeight(float wx, float wz) const
        {
            if (gridSide < 2u || heights.empty())
            {
                return 0.0f;
            }
            const float u = pcgClamp((wx - minX) / chunkSize, 0.0f, 1.0f) * float(gridSide - 1u);
            const float v = pcgClamp((wz - minZ) / chunkSize, 0.0f, 1.0f) * float(gridSide - 1u);
            const uint x0 = uint(u) < (gridSide - 1u) ? uint(u) : (gridSide - 2u);
            const uint z0 = uint(v) < (gridSide - 1u) ? uint(v) : (gridSide - 2u);
            const float tx = u - float(x0);
            const float tz = v - float(z0);
            const float f00 = heights[z0 * gridSide + x0];
            const float f10 = heights[z0 * gridSide + (x0 + 1u)];
            const float f01 = heights[(z0 + 1u) * gridSide + x0];
            const float f11 = heights[(z0 + 1u) * gridSide + (x0 + 1u)];
            return pcgLerp(pcgLerp(f00, f10, tx), pcgLerp(f01, f11, tx), tz);
        }

        float sampleSlope(float wx, float wz) const
        {
            if (gridSide < 2u || slopes.empty())
            {
                return 0.0f;
            }
            const float u = pcgClamp((wx - minX) / chunkSize, 0.0f, 1.0f) * float(gridSide - 1u);
            const float v = pcgClamp((wz - minZ) / chunkSize, 0.0f, 1.0f) * float(gridSide - 1u);
            const uint x0 = uint(u) < (gridSide - 1u) ? uint(u) : (gridSide - 2u);
            const uint z0 = uint(v) < (gridSide - 1u) ? uint(v) : (gridSide - 2u);
            const float tx = u - float(x0);
            const float tz = v - float(z0);
            const float f00 = slopes[z0 * gridSide + x0];
            const float f10 = slopes[z0 * gridSide + (x0 + 1u)];
            const float f01 = slopes[(z0 + 1u) * gridSide + x0];
            const float f11 = slopes[(z0 + 1u) * gridSide + (x0 + 1u)];
            return pcgLerp(pcgLerp(f00, f10, tx), pcgLerp(f01, f11, tx), tz);
        }

        float sampleRoad(float wx, float wz) const
        {
            if (gridSide < 2u || roadFactors.empty())
            {
                return 0.0f;
            }
            const float u = pcgClamp((wx - minX) / chunkSize, 0.0f, 1.0f) * float(gridSide - 1u);
            const float v = pcgClamp((wz - minZ) / chunkSize, 0.0f, 1.0f) * float(gridSide - 1u);
            const uint x0 = uint(u) < (gridSide - 1u) ? uint(u) : (gridSide - 2u);
            const uint z0 = uint(v) < (gridSide - 1u) ? uint(v) : (gridSide - 2u);
            const float tx = u - float(x0);
            const float tz = v - float(z0);
            const float f00 = roadFactors[z0 * gridSide + x0];
            const float f10 = roadFactors[z0 * gridSide + (x0 + 1u)];
            const float f01 = roadFactors[(z0 + 1u) * gridSide + x0];
            const float f11 = roadFactors[(z0 + 1u) * gridSide + (x0 + 1u)];
            return pcgLerp(pcgLerp(f00, f10, tx), pcgLerp(f01, f11, tx), tz);
        }
    };

    /// Output of priority tree obstacle scatter on one leaf chunk.
    struct ChunkObstacleResult
    {
        eastl::vector<ObstacleRecord> obstacles;
        eastl::vector<PCGInstanceData> trees;
    };

    /// Aggregated CPU geometry + LOD leaf counts for one leaf chunk or merged quadtree subtree.
    struct ChunkGeometryPack
    {
        eastl::vector<TerrainVertex> terrainVertices;
        eastl::vector<PCGInstanceData> trees;
        eastl::vector<PCGInstanceData> rocks;
        eastl::vector<PCGInstanceData> grass;
        uint leafCount = 0u;
        uint lodLeafCount0 = 0u;
        uint lodLeafCount1 = 0u;
        uint lodLeafCount2 = 0u;
        uint lodLeafCount3 = 0u;
    };

    /// Deterministic geometry counters surfaced to the ImGui overlay and CLI benchmark verifier.
    struct ForestFrameSummary
    {
        uint chunkCount = 0u;
        uint lodLeafCounts[4] = {0u, 0u, 0u, 0u};
        uint nestedSubgraphCount = 0u;
        uint terrainTriangleCount = 0u;
        uint treeCount = 0u;
        uint rockCount = 0u;
        uint grassBladeCount = 0u;
    };

    /// Headless CLI benchmark verification output combining deterministic PCG counts and GPU pixel stats.
    struct ForestBenchmarkResult
    {
        ForestFrameSummary summary;
        uint nonSkyPixelCount = 0u;
        uint pixelChecksum = 0u;
    };
} // namespace PCGForestSample
