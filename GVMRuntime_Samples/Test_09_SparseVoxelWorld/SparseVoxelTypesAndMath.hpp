#pragma once
#include "UGL.h"
#include <EASTL/vector.h>

namespace SparseVoxelSample
{
    using namespace UGL;

    // =========================================================================
    // 1. Self-Contained Deterministic Math (UGLC Static Purity Compliant)
    // =========================================================================

    inline float svAbs(float x)
    {
        return x < 0.0f ? -x : x;
    }

    inline float svMin(float a, float b)
    {
        return a < b ? a : b;
    }

    inline float svMax(float a, float b)
    {
        return a > b ? a : b;
    }

    inline uint svMinU(uint a, uint b)
    {
        return a < b ? a : b;
    }

    inline uint svMaxU(uint a, uint b)
    {
        return a > b ? a : b;
    }

    inline int svMinI(int a, int b)
    {
        return a < b ? a : b;
    }

    inline int svMaxI(int a, int b)
    {
        return a > b ? a : b;
    }

    inline float svClamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline float svFloor(float x)
    {
        const int i = int(x);
        return (x < float(i)) ? float(i - 1) : float(i);
    }

    inline int svFloorToInt(float x)
    {
        const int i = int(x);
        return (x < float(i)) ? (i - 1) : i;
    }

    inline float svFract(float x)
    {
        return x - svFloor(x);
    }

    inline float svLerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    inline float svSmoothstep(float edge0, float edge1, float x)
    {
        const float denom = edge1 - edge0;
        const float t = denom > 1e-6f ? svClamp((x - edge0) / denom, 0.0f, 1.0f) : 0.0f;
        return t * t * (3.0f - 2.0f * t);
    }

    inline float svSin(float radians)
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

    inline float svCos(float radians)
    {
        return svSin(radians + 1.57079632679f);
    }

    inline float svSqrt(float v)
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

    inline uint svMixBits(uint x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    inline uint svHashCoords2D(int ix, int iz, uint seed)
    {
        const uint ux = uint(ix) * 0x9e3779b1u;
        const uint uz = uint(iz) * 0x85ebca77u;
        return svMixBits(ux ^ uz ^ (seed * 0xc2b2ae3du));
    }

    inline uint svHashCoords3D(int ix, int iy, int iz, uint seed)
    {
        const uint ux = uint(ix) * 0x9e3779b1u;
        const uint uy = uint(iy) * 0x27d4eb2du;
        const uint uz = uint(iz) * 0x85ebca77u;
        return svMixBits(ux ^ uy ^ uz ^ (seed * 0x165667b1u));
    }

    inline float svHash01(int ix, int iz, uint seed)
    {
        return float(svHashCoords2D(ix, iz, seed) & 0x00ffffffu) * (1.0f / 16777216.0f);
    }

    inline float svHash3D01(int ix, int iy, int iz, uint seed)
    {
        return float(svHashCoords3D(ix, iy, iz, seed) & 0x00ffffffu) * (1.0f / 16777216.0f);
    }

    inline float svValueNoise2D(float x, float z, uint seed)
    {
        const float fx = svFloor(x);
        const float fz = svFloor(z);
        const int ix = int(fx);
        const int iz = int(fz);
        const float tx = x - fx;
        const float tz = z - fz;
        const float ux = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
        const float uz = tz * tz * tz * (tz * (tz * 6.0f - 15.0f) + 10.0f);

        const float v00 = svHash01(ix, iz, seed);
        const float v10 = svHash01(ix + 1, iz, seed);
        const float v01 = svHash01(ix, iz + 1, seed);
        const float v11 = svHash01(ix + 1, iz + 1, seed);
        return svLerp(svLerp(v00, v10, ux), svLerp(v01, v11, ux), uz);
    }

    inline float svValueNoise3D(float x, float y, float z, uint seed)
    {
        const float fx = svFloor(x);
        const float fy = svFloor(y);
        const float fz = svFloor(z);
        const int ix = int(fx);
        const int iy = int(fy);
        const int iz = int(fz);
        const float tx = x - fx;
        const float ty = y - fy;
        const float tz = z - fz;
        const float ux = tx * tx * (3.0f - 2.0f * tx);
        const float uy = ty * ty * (3.0f - 2.0f * ty);
        const float uz = tz * tz * (3.0f - 2.0f * tz);

        const float c000 = svHash3D01(ix, iy, iz, seed);
        const float c100 = svHash3D01(ix + 1, iy, iz, seed);
        const float c010 = svHash3D01(ix, iy + 1, iz, seed);
        const float c110 = svHash3D01(ix + 1, iy + 1, iz, seed);
        const float c001 = svHash3D01(ix, iy, iz + 1, seed);
        const float c101 = svHash3D01(ix + 1, iy, iz + 1, seed);
        const float c011 = svHash3D01(ix, iy + 1, iz + 1, seed);
        const float c111 = svHash3D01(ix + 1, iy + 1, iz + 1, seed);

        const float x00 = svLerp(c000, c100, ux);
        const float x10 = svLerp(c010, c110, ux);
        const float x01 = svLerp(c001, c101, ux);
        const float x11 = svLerp(c011, c111, ux);
        return svLerp(svLerp(x00, x10, uy), svLerp(x01, x11, uy), uz);
    }

    inline float svFbm2D(float x, float z, uint octaves, uint seed)
    {
        float sum = 0.0f;
        float amp = 0.5f;
        float freq = 1.0f;
        float norm = 0.0f;
        const uint safeOct = octaves > 0u ? (octaves <= 6u ? octaves : 6u) : 4u;
        for (uint o = 0u; o < safeOct; ++o)
        {
            const float n = svValueNoise2D(x * freq, z * freq, seed + o * 131u);
            sum += n * amp;
            norm += amp;
            const float rx = x * 0.8f - z * 0.6f;
            const float rz = x * 0.6f + z * 0.8f;
            x = rx;
            z = rz;
            freq *= 2.04f;
            amp *= 0.5f;
        }
        return norm > 0.0f ? (sum / norm) : 0.0f;
    }

    // =========================================================================
    // 2. Micro-Voxel Material Palette & World Constants
    // =========================================================================

    inline constexpr uint kMatAir = 0u;
    inline constexpr uint kMatLimestoneLight = 1u;
    inline constexpr uint kMatLimestoneDark = 2u;
    inline constexpr uint kMatMossyGrass = 3u;
    inline constexpr uint kMatWater = 4u;
    inline constexpr uint kMatFoam = 5u;
    inline constexpr uint kMatBridgeStone = 6u;
    inline constexpr uint kMatPagodaWood = 7u;
    inline constexpr uint kMatPagodaRoof = 8u;
    inline constexpr uint kMatTreeBark = 9u;
    inline constexpr uint kMatSakuraBlossom = 10u;
    inline constexpr uint kMatPineNeedle = 11u;
    inline constexpr uint kMatCrystalCyan = 12u;
    inline constexpr uint kMatCrystalAmethyst = 13u;
    inline constexpr uint kMatLanternFrame = 14u;
    inline constexpr uint kMatLanternGlow = 15u;
    inline constexpr uint kMatMushroomStem = 16u;
    inline constexpr uint kMatMushroomCap = 17u;
    inline constexpr uint kMatRuinPillar = 18u;

    inline constexpr uint kQuadCatOpaque = 0u;
    inline constexpr uint kQuadCatFoliageEmissive = 1u;
    inline constexpr uint kQuadCatWater = 2u;

    inline constexpr uint kBrickDim = 16u;
    inline constexpr uint kPaddedDim = 18u;
    inline constexpr uint kClusterBricksPerAxis = 2u;
    inline constexpr uint kClusterDim = 32u;
    inline constexpr uint kGridDimX = 512u;
    inline constexpr uint kGridDimY = 256u;
    inline constexpr uint kGridDimZ = 512u;
    inline constexpr float kMacroTileSize = 64.0f;

    inline bool isSolidMaterial(uint mat)
    {
        return mat != kMatAir && mat != kMatWater && mat != kMatFoam;
    }

    inline uint materialQuadCategory(uint mat)
    {
        if (mat == kMatWater || mat == kMatFoam)
        {
            return kQuadCatWater;
        }
        if (mat == kMatSakuraBlossom || mat == kMatPineNeedle ||
            mat == kMatCrystalCyan || mat == kMatCrystalAmethyst ||
            mat == kMatLanternGlow || mat == kMatMushroomCap)
        {
            return kQuadCatFoliageEmissive;
        }
        return kQuadCatOpaque;
    }

    // =========================================================================
    // 3. Shared GPU/CPU Structures (16-Byte Aligned, UGLC Compliant)
    // =========================================================================

    /// Compact 32-byte greedy-merged voxel quad unpacked procedurally in the vertex shader.
    /// originAndAnim: xyz = world-space quad base corner, w = compute-animated sway/ripple/distance parameter.
    /// meta:
    ///   x = dir(0..5) | (matId<<3) | (ao00<<8) | (ao10<<10) | (ao01<<12) | (ao11<<14) | (flip<<16) | (cat<<17) | ((uSpan-1)<<19) | ((vSpan-1)<<23)
    ///   y = glowLevel(0..7) | (glowPalette<<3) | (voxelHash<<8) | (aux<<16)
    ///   z = animatedPhase / tint packed by ComputePass
    ///   w = animatedDisplacement / pulse packed by ComputePass
    struct VoxelQuad
    {
        float4 originAndAnim;
        uint4 meta;
    };

    /// Host-prepared simulation, streaming, camera, and voxel world settings.
    struct SparseVoxelParams
    {
        uint2 extent = uint2(960u, 640u);
        uint seed = 2026u;
        float voxelSize = 0.125f;

        float2 streamingCenterXZ = float2(0.0f, 0.0f);
        float worldMinY = -8.0f;
        float timeSeconds = 0.0f;

        float canyonHeightScale = 1.0f;
        float waterRippleStrength = 1.0f;
        float emissiveGlowScale = 1.0f;
        uint visualizeOctree = 0u;

        float4 viewProjRow0 = float4(1.0f, 0.0f, 0.0f, 0.0f);
        float4 viewProjRow1 = float4(0.0f, 1.0f, 0.0f, 0.0f);
        float4 viewProjRow2 = float4(0.0f, 0.0f, 1.0f, 0.0f);
        float4 viewProjRow3 = float4(0.0f, 0.0f, 0.0f, 1.0f);
        float4 eyePosition = float4(18.0f, 16.0f, -34.0f, 1.0f);
        float4 sunDirection = float4(0.45f, 0.78f, -0.43f, 0.0f);
    };

    /// Sparse Octree node descriptor spanning [256^3 -> 128^3 -> 64^3 -> 32^3 cluster].
    struct OctreeNodeSpec
    {
        uint quadrantIndex = 0u;
        uint depth = 0u;
        uint gx = 0u;
        uint gy = 0u;
        uint gz = 0u;
        uint sizeVoxels = 256u;
    };

    /// Owned input snapshot for one exported UGL [[Export]] 32^3 voxel brick cluster subgraph.
    struct VoxelClusterInput
    {
        SparseVoxelParams params;
        OctreeNodeSpec clusterNode;
    };

    /// Output of a brick, cluster, or quadrant merge on CPU.
    struct VoxelQuadrantPack
    {
        eastl::vector<VoxelQuad> opaqueQuads;
        eastl::vector<VoxelQuad> foliageQuads;
        eastl::vector<VoxelQuad> waterQuads;
        uint activeBrickCount = 0u;
        uint prunedBrickCount = 0u;
        uint solidVoxelCount = 0u;
        uint rawFaceCount = 0u;
        uint greedyQuadCount = 0u;
        uint nestedSubgraphCount = 0u;
    };

    /// Deterministic voxel world counters surfaced to ImGui & CLI benchmark verification.
    struct SparseVoxelFrameSummary
    {
        uint64_t virtualVoxelCount = 0ull;
        uint totalBrickCount = 0u;
        uint activeBrickCount = 0u;
        uint prunedBrickCount = 0u;
        uint solidVoxelCount = 0u;
        uint rawFaceCount = 0u;
        uint greedyQuadCount = 0u;
        uint triangleCount = 0u;
        uint nestedSubgraphCount = 0u;
        uint octreeInteriorCount = 0u;
    };

    /// Headless CLI benchmark verification output combining PCG counts and GPU pixel checksums.
    struct SparseVoxelBenchmarkResult
    {
        SparseVoxelFrameSummary summary;
        uint nonSkyPixelCount = 0u;
        uint pixelChecksum = 0u;
    };

    // =========================================================================
    // 4. Deterministic Canyon River, Waterfall, Karst Terrain & Macro Landmarks
    // =========================================================================

    inline float3 computeWorldGridOrigin(IN SparseVoxelParams params)
    {
        const float vs = svMax(params.voxelSize, 0.05f);
        const float clusterWorldSpan = vs * float(kClusterDim);
        const float snappedX = svFloor(params.streamingCenterXZ.x / clusterWorldSpan) * clusterWorldSpan;
        const float snappedZ = svFloor(params.streamingCenterXZ.y / clusterWorldSpan) * clusterWorldSpan;
        return float3(
            snappedX - float(kGridDimX / 2u) * vs,
            params.worldMinY,
            snappedZ - float(kGridDimZ / 2u) * vs);
    }

    inline float evalRiverCenterX(float worldZ)
    {
        return 7.5f * svSin(worldZ * 0.068f + 0.35f) + 2.8f * svCos(worldZ * 0.145f - 0.65f);
    }

    inline float evalRiverHalfWidth(float worldZ)
    {
        return 3.8f + 0.75f * svSin(worldZ * 0.11f + 1.1f);
    }

    /// Periodic waterfall cascade along Z so open-world streaming remains seamless across 64m tiles.
    inline float evalWaterSurfaceY(float worldZ)
    {
        const float phase = svFract((worldZ + 32.0f) / kMacroTileSize);
        if (phase < 0.41f)
        {
            return -0.5f;
        }
        if (phase < 0.49f)
        {
            const float t = svSmoothstep(0.41f, 0.49f, phase);
            return svLerp(-0.5f, 2.5f, t);
        }
        if (phase < 0.84f)
        {
            return 2.5f;
        }
        const float tBack = svSmoothstep(0.84f, 1.0f, phase);
        return svLerp(2.5f, -0.5f, tBack);
    }

    inline float evalWaterfallFoamMask(float worldZ)
    {
        const float phase = svFract((worldZ + 32.0f) / kMacroTileSize);
        const float cascade = 1.0f - svSmoothstep(0.0f, 0.055f, svAbs(phase - 0.445f));
        const float splash = 1.0f - svSmoothstep(0.0f, 0.045f, svAbs(phase - 0.395f));
        return svMax(cascade, splash * 0.85f);
    }

    struct TerrainColumnInfo
    {
        float terrainY = 0.0f;
        float waterY = -0.5f;
        float riverDist = 100.0f;
        float riverHalfW = 4.0f;
        float foamMask = 0.0f;
        float slopeEst = 0.0f;
    };

    inline TerrainColumnInfo evalTerrainColumn(float wx, float wz, IN SparseVoxelParams params)
    {
        TerrainColumnInfo info;
        const float rx = evalRiverCenterX(wz);
        const float halfW = evalRiverHalfWidth(wz);
        const float dRiver = svAbs(wx - rx);
        const float waterY = evalWaterSurfaceY(wz);

        const float macro = svFbm2D(wx * 0.032f + 3.1f, wz * 0.032f - 5.7f, 4u, params.seed);
        const float detail = svFbm2D(wx * 0.11f - 9.4f, wz * 0.11f + 4.2f, 3u, params.seed + 701u);

        const float canyonRise = svSmoothstep(halfW * 0.72f, halfW + 5.8f, dRiver);
        const float bedDip = (1.0f - svSmoothstep(halfW * 0.42f, halfW * 1.05f, dRiver)) * 1.45f;
        const float rawCliff = waterY + 0.95f + canyonRise * (3.2f + (macro * 6.8f + detail * 2.1f) * params.canyonHeightScale);
        // Stratified karst terracing on canyon walls
        const float terraceStep = 1.375f;
        const float terraced = svFloor(rawCliff / terraceStep) * terraceStep +
            svSmoothstep(0.28f, 0.92f, svFract(rawCliff / terraceStep)) * terraceStep;
        const float bankHeight = svLerp(rawCliff, terraced, 0.46f * canyonRise);
        const float finalHeight = svLerp(waterY - bedDip, bankHeight, svSmoothstep(halfW * 0.55f, halfW * 1.15f, dRiver));

        info.terrainY = finalHeight;
        info.waterY = waterY;
        info.riverDist = dRiver;
        info.riverHalfW = halfW;
        info.foamMask = (dRiver < halfW + 0.35f) ? evalWaterfallFoamMask(wz) : 0.0f;
        info.slopeEst = svClamp(canyonRise * (1.0f - canyonRise) * 3.5f, 0.0f, 1.0f);
        return info;
    }

    // =========================================================================
    // 5. Macro-Tile Landmarks & Point Light Emitters (Exact 3D AABBs)
    // =========================================================================

    inline constexpr uint kLandmarkArch = 1u;
    inline constexpr uint kLandmarkBridge = 2u;
    inline constexpr uint kLandmarkPagoda = 3u;
    inline constexpr uint kLandmarkGrotto = 4u;
    inline constexpr uint kLandmarkTreeSakura = 5u;
    inline constexpr uint kLandmarkTreePine = 6u;
    inline constexpr uint kLandmarkLantern = 7u;
    inline constexpr uint kLandmarkMushroom = 8u;
    inline constexpr uint kLandmarkRuin = 9u;
    inline constexpr uint kLandmarkSteppingStones = 10u;

    struct VoxelLandmark
    {
        uint kind = 0u;
        uint variantSeed = 0u;
        float3 minBound;
        float3 maxBound;
        float3 anchor;
        float radius = 1.0f;
        float height = 1.0f;
        float aux0 = 0.0f;
        float aux1 = 0.0f;
    };

    struct EmissivePointLight
    {
        float3 pos;
        float radius = 4.5f;
        uint palette = 0u; // 0=amber lantern, 1=cyan crystal, 2=amethyst crystal, 3=magenta mushroom
        float strength = 1.0f;
    };

    struct MacroTileFeatures
    {
        eastl::vector<VoxelLandmark> landmarks;
        eastl::vector<EmissivePointLight> lights;
    };

    inline MacroTileFeatures buildMacroTileFeatures(int tileX, int tileZ, IN SparseVoxelParams params)
    {
        MacroTileFeatures out;
        const float cx = float(tileX) * kMacroTileSize;
        const float cz = float(tileZ) * kMacroTileSize;
        const uint tileSeed = svHashCoords2D(tileX, tileZ, params.seed + 4242u);

        // 1. Natural Karst Stone Arch spanning the upper canyon river
        {
            const float archZ = cz + 3.0f;
            const float archX = evalRiverCenterX(archZ);
            const float baseWaterY = evalWaterSurfaceY(archZ);
            const float halfSpan = 8.8f;
            const float apexY = baseWaterY + 8.6f;

            VoxelLandmark lm;
            lm.kind = kLandmarkArch;
            lm.variantSeed = tileSeed + 11u;
            lm.anchor = float3(archX, baseWaterY - 0.5f, archZ);
            lm.radius = halfSpan;
            lm.height = apexY - (baseWaterY - 0.5f);
            lm.aux0 = apexY;
            lm.minBound = float3(archX - halfSpan - 2.2f, baseWaterY - 1.5f, archZ - 2.6f);
            lm.maxBound = float3(archX + halfSpan + 2.2f, apexY + 2.2f, archZ + 2.6f);
            out.landmarks.push_back(lm);
        }

        // 2. Arched Stone Voxel Bridge crossing the lower river
        {
            const float bridgeZ = cz - 16.5f;
            const float bridgeX = evalRiverCenterX(bridgeZ);
            const float waterY = evalWaterSurfaceY(bridgeZ);
            const float halfSpan = 7.4f;
            const float crownY = waterY + 3.6f;

            VoxelLandmark lm;
            lm.kind = kLandmarkBridge;
            lm.variantSeed = tileSeed + 23u;
            lm.anchor = float3(bridgeX, waterY, bridgeZ);
            lm.radius = halfSpan;
            lm.height = crownY;
            lm.aux0 = waterY;
            lm.minBound = float3(bridgeX - halfSpan - 1.2f, waterY - 2.0f, bridgeZ - 2.0f);
            lm.maxBound = float3(bridgeX + halfSpan + 1.2f, crownY + 1.5f, bridgeZ + 2.0f);
            out.landmarks.push_back(lm);

            // Bridge end stone lanterns
            for (int side = -1; side <= 1; side += 2)
            {
                const float lx = bridgeX + float(side) * (halfSpan - 0.8f);
                const float ly = evalTerrainColumn(lx, bridgeZ - 1.5f, params).terrainY;
                VoxelLandmark lan;
                lan.kind = kLandmarkLantern;
                lan.variantSeed = tileSeed + uint(side + 50);
                lan.anchor = float3(lx, ly, bridgeZ - 1.5f);
                lan.radius = 0.65f;
                lan.height = 1.875f;
                lan.minBound = float3(lx - 0.7f, ly - 0.3f, bridgeZ - 2.2f);
                lan.maxBound = float3(lx + 0.7f, ly + 2.0f, bridgeZ - 0.8f);
                out.landmarks.push_back(lan);

                EmissivePointLight pl;
                pl.pos = float3(lx, ly + 1.25f, bridgeZ - 1.5f);
                pl.radius = 5.2f;
                pl.palette = 0u;
                pl.strength = 1.25f;
                out.lights.push_back(pl);
            }
        }

        // 3. Stepping Stones across lower turquoise river
        {
            const float stepZ = cz - 10.2f;
            const float stepX = evalRiverCenterX(stepZ);
            const float waterY = evalWaterSurfaceY(stepZ);
            VoxelLandmark lm;
            lm.kind = kLandmarkSteppingStones;
            lm.variantSeed = tileSeed + 37u;
            lm.anchor = float3(stepX, waterY, stepZ);
            lm.radius = 5.0f;
            lm.minBound = float3(stepX - 5.2f, waterY - 1.6f, stepZ - 1.8f);
            lm.maxBound = float3(stepX + 5.2f, waterY + 0.6f, stepZ + 1.8f);
            out.landmarks.push_back(lm);
        }

        // 4. Multi-Tier Ancient Pagoda on right-bank scenic overlook
        {
            const float pgZ = cz + 5.0f;
            const float pgX = evalRiverCenterX(pgZ) + 12.0f;
            const float baseH = evalTerrainColumn(pgX, pgZ, params).terrainY;
            const float totalH = 9.2f;

            VoxelLandmark lm;
            lm.kind = kLandmarkPagoda;
            lm.variantSeed = tileSeed + 41u;
            lm.anchor = float3(pgX, baseH, pgZ);
            lm.radius = 3.6f;
            lm.height = totalH;
            lm.minBound = float3(pgX - 3.8f, baseH - 1.5f, pgZ - 3.8f);
            lm.maxBound = float3(pgX + 3.8f, baseH + totalH + 0.5f, pgZ + 3.8f);
            out.landmarks.push_back(lm);

            // Pagoda warm interior shrine glow
            EmissivePointLight pl;
            pl.pos = float3(pgX, baseH + 2.0f, pgZ);
            pl.radius = 6.0f;
            pl.palette = 0u;
            pl.strength = 1.4f;
            out.lights.push_back(pl);

            // Pagoda approach lantern
            const float lx = pgX - 4.2f;
            const float lz = pgZ - 2.0f;
            const float ly = evalTerrainColumn(lx, lz, params).terrainY;
            VoxelLandmark lan;
            lan.kind = kLandmarkLantern;
            lan.variantSeed = tileSeed + 43u;
            lan.anchor = float3(lx, ly, lz);
            lan.radius = 0.65f;
            lan.height = 1.875f;
            lan.minBound = float3(lx - 0.7f, ly - 0.3f, lz - 0.7f);
            lan.maxBound = float3(lx + 0.7f, ly + 2.0f, lz + 0.7f);
            out.landmarks.push_back(lan);

            EmissivePointLight pl2;
            pl2.pos = float3(lx, ly + 1.25f, lz);
            pl2.radius = 5.0f;
            pl2.palette = 0u;
            pl2.strength = 1.2f;
            out.lights.push_back(pl2);
        }

        // 5. Cliffside Glowing Crystal Grotto (left canyon wall facing river)
        {
            const float grZ = cz + 6.5f;
            const float grX = evalRiverCenterX(grZ) - 9.2f;
            const float floorY = evalWaterSurfaceY(grZ) + 1.6f;

            VoxelLandmark lm;
            lm.kind = kLandmarkGrotto;
            lm.variantSeed = tileSeed + 59u;
            lm.anchor = float3(grX, floorY + 1.6f, grZ);
            lm.radius = 4.6f;
            lm.height = 4.2f;
            lm.minBound = float3(grX - 5.0f, floorY - 1.0f, grZ - 5.0f);
            lm.maxBound = float3(grX + 5.0f, floorY + 4.8f, grZ + 5.0f);
            out.landmarks.push_back(lm);

            EmissivePointLight c1;
            c1.pos = float3(grX - 0.8f, floorY + 1.3f, grZ - 0.6f);
            c1.radius = 6.2f;
            c1.palette = 1u;
            c1.strength = 1.5f;
            out.lights.push_back(c1);

            EmissivePointLight c2;
            c2.pos = float3(grX + 1.1f, floorY + 1.1f, grZ + 1.2f);
            c2.radius = 5.6f;
            c2.palette = 2u;
            c2.strength = 1.35f;
            out.lights.push_back(c2);
        }

        // 6. Weathered Stone Ruin Pillars
        for (uint r = 0u; r < 3u; ++r)
        {
            const float rz = cz - 6.0f + float(r) * 5.2f;
            const float rx = evalRiverCenterX(rz) + 7.2f + float(r % 2u) * 2.4f;
            const float ry = evalTerrainColumn(rx, rz, params).terrainY;
            const float rh = 2.4f + 1.6f * svHash01(int(r), tileZ, tileSeed + 71u);
            VoxelLandmark lm;
            lm.kind = kLandmarkRuin;
            lm.variantSeed = tileSeed + 80u + r;
            lm.anchor = float3(rx, ry, rz);
            lm.radius = 0.75f;
            lm.height = rh;
            lm.minBound = float3(rx - 0.9f, ry - 0.5f, rz - 0.9f);
            lm.maxBound = float3(rx + 0.9f, ry + rh + 0.3f, rz + 0.9f);
            out.landmarks.push_back(lm);
        }

        // 7. Giant Fantasy Mushrooms near Grotto & Riverbank
        for (uint m = 0u; m < 5u; ++m)
        {
            const float mz = cz - 1.5f + float(m) * 3.8f;
            const float mx = evalRiverCenterX(mz) - 5.4f - 1.8f * svHash01(int(m), 3, tileSeed + 91u);
            const float my = evalTerrainColumn(mx, mz, params).terrainY;
            const float mh = 1.6f + 1.2f * svHash01(int(m), 7, tileSeed + 97u);
            const float mr = 1.1f + 0.6f * svHash01(int(m), 11, tileSeed + 101u);

            VoxelLandmark lm;
            lm.kind = kLandmarkMushroom;
            lm.variantSeed = tileSeed + 110u + m;
            lm.anchor = float3(mx, my, mz);
            lm.radius = mr;
            lm.height = mh;
            lm.minBound = float3(mx - mr - 0.25f, my - 0.3f, mz - mr - 0.25f);
            lm.maxBound = float3(mx + mr + 0.25f, my + mh + 0.5f, mz + mr + 0.25f);
            out.landmarks.push_back(lm);

            EmissivePointLight ml;
            ml.pos = float3(mx, my + mh * 0.92f, mz);
            ml.radius = 4.2f;
            ml.palette = 3u;
            ml.strength = 1.05f;
            out.lights.push_back(ml);
        }

        // 8. Ancient Sakura & Emerald Pine Voxel Trees (10 per tile)
        for (uint t = 0u; t < 10u; ++t)
        {
            const float sideSign = (t % 2u == 0u) ? 1.0f : -1.0f;
            const float jz = -24.0f + float(t) * 5.2f + (svHash01(int(t), 1, tileSeed + 150u) - 0.5f) * 2.5f;
            const float wz = cz + jz;
            const float riverX = evalRiverCenterX(wz);
            const float offset = 8.0f + 12.0f * svHash01(int(t), 2, tileSeed + 151u);
            const float wx = riverX + sideSign * offset;

            // Keep clear of Pagoda and Grotto center
            if (svAbs(wx - (evalRiverCenterX(cz + 5.0f) + 12.0f)) < 4.5f && svAbs(wz - (cz + 5.0f)) < 4.5f)
            {
                continue;
            }
            if (svAbs(wx - (evalRiverCenterX(cz + 6.5f) - 9.2f)) < 4.8f && svAbs(wz - (cz + 6.5f)) < 4.8f)
            {
                continue;
            }

            const float wy = evalTerrainColumn(wx, wz, params).terrainY;
            const bool isSakura = (t % 3u != 2u);
            const float scale = 0.85f + 0.55f * svHash01(int(t), 5, tileSeed + 155u);
            const float treeH = (isSakura ? 6.2f : 7.6f) * scale;
            const float canopyR = (isSakura ? 3.5f : 2.6f) * scale;

            VoxelLandmark lm;
            lm.kind = isSakura ? kLandmarkTreeSakura : kLandmarkTreePine;
            lm.variantSeed = tileSeed + 200u + t;
            lm.anchor = float3(wx, wy, wz);
            lm.radius = canopyR;
            lm.height = treeH;
            lm.aux0 = scale;
            lm.minBound = float3(wx - canopyR - 0.4f, wy - 0.5f, wz - canopyR - 0.4f);
            lm.maxBound = float3(wx + canopyR + 0.4f, wy + treeH + 0.6f, wz + canopyR + 0.4f);
            out.landmarks.push_back(lm);
        }

        return out;
    }

    inline bool aabbOverlaps(float3 aMin, float3 aMax, float3 bMin, float3 bMax)
    {
        return (aMin.x <= bMax.x && aMax.x >= bMin.x) &&
               (aMin.y <= bMax.y && aMax.y >= bMin.y) &&
               (aMin.z <= bMax.z && aMax.z >= bMin.z);
    }

    // =========================================================================
    // 6. Conservative Interval Pruning for Sparse Octree Nodes & 16^3 Bricks
    // =========================================================================

    inline constexpr uint kIntervalEmptyAir = 0u;
    inline constexpr uint kIntervalDeepSolid = 1u;
    inline constexpr uint kIntervalMixedActive = 2u;

    inline uint classifyBoundsInterval(
        float3 boxMin,
        float3 boxMax,
        IN SparseVoxelParams params,
        IN eastl::vector<VoxelLandmark> tileLandmarks)
    {
        // Sample 3x3 terrain+water heights across the XZ footprint + conservative FBM margin
        const float spanX = boxMax.x - boxMin.x;
        const float spanZ = boxMax.z - boxMin.z;
        const float conservativeMargin = svMin(svMax(spanX, spanZ) * 0.20f + 1.15f, 5.0f);

        float minSurfY = 1e6f;
        float maxSurfY = -1e6f;
        for (uint iz = 0u; iz < 3u; ++iz)
        {
            const float wz = svLerp(boxMin.z, boxMax.z, float(iz) * 0.5f);
            for (uint ix = 0u; ix < 3u; ++ix)
            {
                const float wx = svLerp(boxMin.x, boxMax.x, float(ix) * 0.5f);
                const TerrainColumnInfo col = evalTerrainColumn(wx, wz, params);
                const float colTop = svMax(col.terrainY, (col.riverDist < col.riverHalfW + 0.4f) ? col.waterY : col.terrainY);
                const float colBot = col.terrainY;
                if (colBot < minSurfY)
                {
                    minSurfY = colBot;
                }
                if (colTop > maxSurfY)
                {
                    maxSurfY = colTop;
                }
            }
        }

        float featureMinY = minSurfY - conservativeMargin;
        float featureMaxY = maxSurfY + conservativeMargin;

        const uint lmCount = uint(tileLandmarks.size());
        for (uint i = 0u; i < lmCount; ++i)
        {
            const VoxelLandmark lm = tileLandmarks[i];
            if (boxMin.x <= lm.maxBound.x && boxMax.x >= lm.minBound.x &&
                boxMin.z <= lm.maxBound.z && boxMax.z >= lm.minBound.z)
            {
                if (lm.minBound.y < featureMinY)
                {
                    featureMinY = lm.minBound.y;
                }
                if (lm.maxBound.y > featureMaxY)
                {
                    featureMaxY = lm.maxBound.y;
                }
            }
        }

        if (boxMin.y > featureMaxY)
        {
            return kIntervalEmptyAir;
        }
        if (boxMax.y < featureMinY)
        {
            return kIntervalDeepSolid;
        }
        return kIntervalMixedActive;
    }

    inline eastl::vector<VoxelLandmark> gatherLandmarksForXZBox(
        float3 boxMin,
        float3 boxMax,
        IN SparseVoxelParams params)
    {
        eastl::vector<VoxelLandmark> combined;
        const int tMinX = svFloorToInt((boxMin.x + 32.0f) / kMacroTileSize);
        const int tMaxX = svFloorToInt((boxMax.x + 32.0f) / kMacroTileSize);
        const int tMinZ = svFloorToInt((boxMin.z + 32.0f) / kMacroTileSize);
        const int tMaxZ = svFloorToInt((boxMax.z + 32.0f) / kMacroTileSize);

        for (int tz = tMinZ; tz <= tMaxZ; ++tz)
        {
            for (int tx = tMinX; tx <= tMaxX; ++tx)
            {
                const MacroTileFeatures tf = buildMacroTileFeatures(tx, tz, params);
                const uint n = uint(tf.landmarks.size());
                const uint prev = uint(combined.size());
                combined.resize(prev + n);
                for (uint i = 0u; i < n; ++i)
                {
                    combined[prev + i] = tf.landmarks[i];
                }
            }
        }
        return combined;
    }

    inline eastl::vector<EmissivePointLight> gatherLightsFor3DBox(
        float3 boxMin,
        float3 boxMax,
        IN SparseVoxelParams params)
    {
        eastl::vector<EmissivePointLight> out;
        const float pad = 6.5f;
        const int tMinX = svFloorToInt((boxMin.x - pad + 32.0f) / kMacroTileSize);
        const int tMaxX = svFloorToInt((boxMax.x + pad + 32.0f) / kMacroTileSize);
        const int tMinZ = svFloorToInt((boxMin.z - pad + 32.0f) / kMacroTileSize);
        const int tMaxZ = svFloorToInt((boxMax.z + pad + 32.0f) / kMacroTileSize);

        for (int tz = tMinZ; tz <= tMaxZ; ++tz)
        {
            for (int tx = tMinX; tx <= tMaxX; ++tx)
            {
                const MacroTileFeatures tf = buildMacroTileFeatures(tx, tz, params);
                const uint n = uint(tf.lights.size());
                for (uint i = 0u; i < n; ++i)
                {
                    const EmissivePointLight pl = tf.lights[i];
                    if (pl.pos.x + pl.radius >= boxMin.x && pl.pos.x - pl.radius <= boxMax.x &&
                        pl.pos.y + pl.radius >= boxMin.y && pl.pos.y - pl.radius <= boxMax.y &&
                        pl.pos.z + pl.radius >= boxMin.z && pl.pos.z - pl.radius <= boxMax.z)
                    {
                        out.push_back(pl);
                    }
                }
            }
        }
        return out;
    }

    inline float3 nodeBoxMin(IN OctreeNodeSpec node, IN SparseVoxelParams params)
    {
        const float3 origin = computeWorldGridOrigin(params);
        const float vs = svMax(params.voxelSize, 0.05f);
        return float3(
            origin.x + float(node.gx) * vs,
            origin.y + float(node.gy) * vs,
            origin.z + float(node.gz) * vs);
    }

    inline float3 nodeBoxMax(IN OctreeNodeSpec node, IN SparseVoxelParams params)
    {
        const float3 origin = computeWorldGridOrigin(params);
        const float vs = svMax(params.voxelSize, 0.05f);
        return float3(
            origin.x + float(node.gx + node.sizeVoxels) * vs,
            origin.y + float(node.gy + node.sizeVoxels) * vs,
            origin.z + float(node.gz + node.sizeVoxels) * vs);
    }

    inline uint countBricksInOctreeNode(uint sizeVoxels)
    {
        const uint b = sizeVoxels / kBrickDim;
        return b * b * b;
    }

    inline OctreeNodeSpec makeQuadrantRootSpec(uint quadrantIndex)
    {
        OctreeNodeSpec spec;
        spec.quadrantIndex = quadrantIndex;
        spec.depth = 0u;
        spec.gx = (quadrantIndex & 1u) != 0u ? 256u : 0u;
        spec.gy = 0u;
        spec.gz = (quadrantIndex & 2u) != 0u ? 256u : 0u;
        spec.sizeVoxels = 256u;
        return spec;
    }

    inline OctreeNodeSpec makeOctreeChildSpec(IN OctreeNodeSpec parent, uint octant)
    {
        const uint halfSize = parent.sizeVoxels / 2u;
        OctreeNodeSpec child;
        child.quadrantIndex = parent.quadrantIndex;
        child.depth = parent.depth + 1u;
        child.gx = parent.gx + ((octant & 1u) != 0u ? halfSize : 0u);
        child.gy = parent.gy + ((octant & 2u) != 0u ? halfSize : 0u);
        child.gz = parent.gz + ((octant & 4u) != 0u ? halfSize : 0u);
        child.sizeVoxels = halfSize;
        return child;
    }
} // namespace SparseVoxelSample
