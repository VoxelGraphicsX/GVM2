#pragma once
#include "SparseVoxelTypesAndMath.hpp"

namespace SparseVoxelSample
{
    // =========================================================================
    // 1. Pure Micro-Voxel Landmark Evaluators (0.125m World-Space Resolution)
    // =========================================================================

    inline uint evalCrystalColorAt(float dx, float dy, float dz, uint seed)
    {
        return ((svHashCoords3D(int(dx * 2.0f), int(dy * 2.0f), int(dz * 2.0f), seed) & 1u) == 0u)
            ? kMatCrystalCyan
            : kMatCrystalAmethyst;
    }

    inline uint evalLandmarkVoxel(
        float wx,
        float wy,
        float wz,
        IN VoxelLandmark lm,
        uint currentMat,
        IN SparseVoxelParams params)
    {
        if (wx < lm.minBound.x || wx > lm.maxBound.x ||
            wy < lm.minBound.y || wy > lm.maxBound.y ||
            wz < lm.minBound.z || wz > lm.maxBound.z)
        {
            return currentMat;
        }

        const float dx = wx - lm.anchor.x;
        const float dy = wy - lm.anchor.y;
        const float dz = wz - lm.anchor.z;

        if (lm.kind == kLandmarkGrotto)
        {
            // Carve an open cliffside grotto alcove facing +X toward the river
            const float ex = (dx - 0.8f) / 4.4f;
            const float ey = dy / 2.2f;
            const float ez = dz / 4.2f;
            const float warp = (svValueNoise3D(wx * 0.65f, wy * 0.65f, wz * 0.65f, lm.variantSeed) - 0.5f) * 0.24f;
            const float ellip = ex * ex + ey * ey + ez * ez + warp;

            // Open mouth facing river (+X side)
            const bool inMouth = (dx > -0.8f) && (svAbs(dz) < 3.1f) && (dy > -1.35f) && (dy < 1.9f);
            if (ellip < 1.0f || inMouth)
            {
                if (currentMat == kMatLimestoneLight || currentMat == kMatLimestoneDark || currentMat == kMatMossyGrass)
                {
                    currentMat = kMatAir;
                }
            }

            // Faceted glowing crystal spires rising from the grotto floor
            for (uint c = 0u; c < 7u; ++c)
            {
                const float angle = float(c) * 0.92f + 0.3f;
                const float dist = 1.15f + 1.55f * svHash01(int(c), 1, lm.variantSeed + 7u);
                const float cx = lm.anchor.x - 0.6f + svCos(angle) * dist * 0.85f;
                const float cz = lm.anchor.z + svSin(angle) * dist;
                const float baseY = lm.anchor.y - 1.55f;
                const float spireH = 1.65f + 1.45f * svHash01(int(c), 2, lm.variantSeed + 13u);
                const float tiltX = (svHash01(int(c), 3, lm.variantSeed + 19u) - 0.5f) * 0.45f;
                const float tiltZ = (svHash01(int(c), 4, lm.variantSeed + 23u) - 0.5f) * 0.45f;

                const float hRel = wy - baseY;
                if (hRel >= 0.0f && hRel <= spireH)
                {
                    const float t = hRel / spireH;
                    const float r = svLerp(0.42f, 0.08f, t);
                    const float px = wx - (cx + tiltX * hRel);
                    const float pz = wz - (cz + tiltZ * hRel);
                    // Faceted diamond cross-section
                    if (svAbs(px) + svAbs(pz) * 0.85f < r * 1.15f && (px * px + pz * pz) < r * r)
                    {
                        return (c % 2u == 0u) ? kMatCrystalCyan : kMatCrystalAmethyst;
                    }
                }
            }
            return currentMat;
        }

        if (lm.kind == kLandmarkArch)
        {
            const float halfSpan = lm.radius;
            const float normX = dx / halfSpan;
            if (svAbs(normX) <= 1.12f)
            {
                const float archCurveY = lm.anchor.y + (lm.aux0 - lm.anchor.y) * (1.0f - normX * normX);
                const float warp = (svValueNoise3D(wx * 0.45f, wy * 0.45f, wz * 0.45f, lm.variantSeed) - 0.5f) * 0.75f;
                const float thicknessY = 1.15f + 0.55f * svAbs(normX);
                const float thicknessZ = 1.45f + 0.50f * svAbs(normX);
                const float dyA = (wy - archCurveY) / thicknessY;
                const float dzA = dz / thicknessZ;
                if (dyA * dyA + dzA * dzA + warp * 0.35f < 1.0f)
                {
                    if (wy > archCurveY + thicknessY * 0.52f)
                    {
                        return kMatMossyGrass;
                    }
                    const int band = svFloorToInt(wy * 2.0f + warp);
                    return ((band & 1) == 0) ? kMatLimestoneLight : kMatLimestoneDark;
                }
            }
            return currentMat;
        }

        if (lm.kind == kLandmarkBridge)
        {
            const float halfSpan = lm.radius;
            const float normX = dx / halfSpan;
            if (svAbs(normX) <= 1.04f && svAbs(dz) <= 1.45f)
            {
                const float deckY = lm.aux0 + 1.45f + 1.85f * svMax(0.0f, 1.0f - normX * normX);
                const float quantizedDeck = svFloor(deckY / params.voxelSize) * params.voxelSize;
                // Stone arch rib beneath deck
                const float underArchY = quantizedDeck - 0.625f;
                const bool inDeck = (wy <= quantizedDeck && wy >= quantizedDeck - 0.48f && svAbs(dz) <= 1.25f);
                const bool inUnderRib = (wy <= quantizedDeck - 0.35f && wy >= underArchY - 0.35f * (normX * normX) && svAbs(dz) <= 1.10f);
                // Side parapets + post pillars
                const bool onEdge = (svAbs(dz) >= 0.98f && svAbs(dz) <= 1.28f);
                const float postSpacing = svAbs(svFract((dx + halfSpan) / 2.25f) - 0.5f);
                const float railH = (postSpacing < 0.12f) ? 0.875f : 0.50f;
                const bool inParapet = onEdge && (wy > quantizedDeck && wy <= quantizedDeck + railH);
                // End piers into riverbed
                const bool inPier = (svAbs(svAbs(dx) - (halfSpan - 1.1f)) < 0.72f) &&
                    (wy <= quantizedDeck && wy >= lm.aux0 - 1.8f) && (svAbs(dz) <= 1.25f);

                if (inDeck || inUnderRib || inParapet || inPier)
                {
                    return kMatBridgeStone;
                }
            }
            return currentMat;
        }

        if (lm.kind == kLandmarkSteppingStones)
        {
            for (uint s = 0u; s < 6u; ++s)
            {
                const float t = (float(s) + 0.5f) / 6.0f;
                const float sx = lm.anchor.x + svLerp(-3.8f, 3.8f, t);
                const float sz = lm.anchor.z + 0.55f * svSin(float(s) * 1.3f);
                const float rx = wx - sx;
                const float rz = wz - sz;
                const float r2 = rx * rx + rz * rz;
                if (r2 < 0.58f * 0.58f && wy <= lm.anchor.y + 0.25f && wy >= lm.anchor.y - 1.4f)
                {
                    return kMatBridgeStone;
                }
            }
            return currentMat;
        }

        if (lm.kind == kLandmarkPagoda)
        {
            const float ax = svAbs(dx);
            const float az = svAbs(dz);
            const float maxXZ = svMax(ax, az);

            // Stepped stone foundation plinth
            if (dy >= -1.2f && dy <= 0.375f && maxXZ <= 3.0f)
            {
                return kMatBridgeStone;
            }
            if (dy > 0.375f && dy <= 0.75f && maxXZ <= 2.5f)
            {
                return kMatBridgeStone;
            }

            // Tier 1 pillars & shrine lantern (dy in 0.75 .. 3.25)
            if (dy > 0.75f && dy <= 3.25f)
            {
                const bool pillar = (svAbs(ax - 1.625f) < 0.26f && svAbs(az - 1.625f) < 0.26f);
                if (pillar)
                {
                    return kMatPagodaWood;
                }
                // Warm glowing shrine lantern core inside Tier 1
                if (ax < 0.38f && az < 0.38f && dy >= 1.25f && dy <= 2.15f)
                {
                    return (dy >= 1.45f && dy <= 1.95f && ax < 0.26f && az < 0.26f)
                        ? kMatLanternGlow
                        : kMatLanternFrame;
                }
            }

            // Tier 1 overhanging upturned pagoda roof (dy in 3.1 .. 4.35)
            if (dy >= 3.1f && dy <= 4.35f)
            {
                const float roofT = svClamp((dy - 3.1f) / 1.15f, 0.0f, 1.0f);
                const float edgeSpan = svLerp(3.35f, 1.55f, roofT);
                const float cornerLift = 0.055f * (ax * ax + az * az);
                const float roofBase = 3.15f + roofT * 0.95f + cornerLift;
                if (maxXZ <= edgeSpan && svAbs(wy - (lm.anchor.y + roofBase)) < 0.22f)
                {
                    return kMatPagodaRoof;
                }
            }

            // Tier 2 pillars (dy in 4.15 .. 6.0)
            if (dy > 4.15f && dy <= 6.0f)
            {
                const bool pillar2 = (svAbs(ax - 1.05f) < 0.22f && svAbs(az - 1.05f) < 0.22f);
                const bool innerWall = (maxXZ <= 0.85f && maxXZ >= 0.62f);
                if (pillar2 || innerWall)
                {
                    return kMatPagodaWood;
                }
            }

            // Tier 2 overhanging upturned pagoda roof (dy in 5.85 .. 7.25)
            if (dy >= 5.85f && dy <= 7.25f)
            {
                const float roofT = svClamp((dy - 5.85f) / 1.15f, 0.0f, 1.0f);
                const float edgeSpan = svLerp(2.55f, 0.75f, roofT);
                const float cornerLift = 0.065f * (ax * ax + az * az);
                const float roofBase = 5.90f + roofT * 1.05f + cornerLift;
                if (maxXZ <= edgeSpan && svAbs(wy - (lm.anchor.y + roofBase)) < 0.22f)
                {
                    return kMatPagodaRoof;
                }
            }

            // Spire finial (dy in 6.95 .. 8.75)
            if (dy >= 6.95f && dy <= 8.75f)
            {
                const float spireR = svLerp(0.32f, 0.08f, (dy - 6.95f) / 1.8f);
                if (ax * ax + az * az <= spireR * spireR)
                {
                    return kMatPagodaRoof;
                }
            }
            return currentMat;
        }

        if (lm.kind == kLandmarkRuin)
        {
            const float r2 = dx * dx + dz * dz;
            const float breakTilt = dx * 0.35f - dz * 0.25f;
            if (dy >= -0.4f && dy <= lm.height + breakTilt)
            {
                if (dy < 0.35f && svAbs(dx) < 0.62f && svAbs(dz) < 0.62f)
                {
                    return kMatRuinPillar;
                }
                if (r2 < 0.38f * 0.38f)
                {
                    return kMatRuinPillar;
                }
            }
            return currentMat;
        }

        if (lm.kind == kLandmarkLantern)
        {
            const float ax = svAbs(dx);
            const float az = svAbs(dz);
            if (dy >= -0.2f && dy <= 0.25f && ax < 0.34f && az < 0.34f)
            {
                return kMatLanternFrame;
            }
            if (dy > 0.25f && dy <= 0.95f && ax < 0.16f && az < 0.16f)
            {
                return kMatLanternFrame;
            }
            if (dy > 0.95f && dy <= 1.42f && ax < 0.28f && az < 0.28f)
            {
                if (ax < 0.17f && az < 0.17f && dy >= 1.05f && dy <= 1.32f)
                {
                    return kMatLanternGlow;
                }
                if (ax > 0.17f && az > 0.17f)
                {
                    return kMatLanternFrame;
                }
                return kMatLanternGlow;
            }
            if (dy > 1.42f && dy <= 1.82f)
            {
                const float capR = svLerp(0.46f, 0.10f, (dy - 1.42f) / 0.40f);
                if (ax < capR && az < capR)
                {
                    return kMatLanternFrame;
                }
            }
            return currentMat;
        }

        if (lm.kind == kLandmarkMushroom)
        {
            const float bendX = 0.18f * svSin(dy * 1.4f + float(lm.variantSeed & 7u));
            const float sx = dx - bendX;
            const float stemR = 0.22f * (1.0f - 0.25f * svClamp(dy / lm.height, 0.0f, 1.0f));
            if (dy >= -0.2f && dy <= lm.height * 0.85f && (sx * sx + dz * dz) <= stemR * stemR)
            {
                return kMatMushroomStem;
            }
            const float capCenterY = lm.height * 0.78f;
            const float cy = dy - capCenterY;
            const float cx = dx - bendX;
            const float rNorm = svSqrt(cx * cx + dz * dz) / lm.radius;
            if (rNorm <= 1.0f && cy >= -0.15f && cy <= (1.0f - rNorm * rNorm) * (lm.radius * 0.58f))
            {
                return kMatMushroomCap;
            }
            return currentMat;
        }

        if (lm.kind == kLandmarkTreeSakura)
        {
            const float scale = lm.aux0;
            const float trunkH = 3.4f * scale;
            const float leanX = 0.55f * svSin(dy * 0.55f + float(lm.variantSeed & 15u)) * scale;
            const float leanZ = 0.35f * svCos(dy * 0.45f + float(lm.variantSeed & 7u)) * scale;

            // Gnarled spreading roots near ground
            if (dy >= -0.4f && dy <= 0.75f * scale)
            {
                const float tRoot = svClamp(1.0f - dy / (0.75f * scale), 0.0f, 1.0f);
                for (uint rt = 0u; rt < 4u; ++rt)
                {
                    const float ra = float(rt) * 1.5708f + float(lm.variantSeed & 3u) * 0.4f;
                    const float rx = dx - svCos(ra) * (0.85f * scale * tRoot);
                    const float rz = dz - svSin(ra) * (0.85f * scale * tRoot);
                    if (rx * rx + rz * rz < (0.24f * scale) * (0.24f * scale))
                    {
                        return kMatTreeBark;
                    }
                }
            }

            // Curved trunk
            if (dy >= -0.3f && dy <= trunkH)
            {
                const float trunkR = svLerp(0.36f, 0.20f, dy / trunkH) * scale;
                const float tx = dx - leanX;
                const float tz = dz - leanZ;
                if (tx * tx + tz * tz <= trunkR * trunkR)
                {
                    return kMatTreeBark;
                }
            }

            // Billowing cherry-blossom canopy lobes
            const float topX = leanX;
            const float topZ = leanZ;
            for (uint lobe = 0u; lobe < 5u; ++lobe)
            {
                const float ang = float(lobe) * 1.2566f + float(lm.variantSeed & 7u) * 0.3f;
                const float dist = (lobe == 0u) ? 0.0f : (1.55f * scale);
                const float lx = topX + svCos(ang) * dist;
                const float lz = topZ + svSin(ang) * dist;
                const float ly = (lobe == 0u) ? (4.65f * scale) : (3.65f + 0.35f * float(lobe & 1u)) * scale;
                const float lr = (lobe == 0u) ? (2.15f * scale) : (1.65f * scale);

                const float ex = (dx - lx) / lr;
                const float ey = (dy - ly) / (lr * 0.62f);
                const float ez = (dz - lz) / lr;
                const float fuzz = (svValueNoise3D(wx * 1.35f, wy * 1.35f, wz * 1.35f, lm.variantSeed + lobe * 17u) - 0.5f) * 0.38f;
                if (ex * ex + ey * ey + ez * ez + fuzz < 1.0f)
                {
                    return kMatSakuraBlossom;
                }
            }
            return currentMat;
        }

        if (lm.kind == kLandmarkTreePine)
        {
            const float scale = lm.aux0;
            const float trunkH = 6.2f * scale;
            if (dy >= -0.3f && dy <= trunkH)
            {
                const float trunkR = svLerp(0.28f, 0.10f, dy / trunkH) * scale;
                if (dx * dx + dz * dz <= trunkR * trunkR)
                {
                    return kMatTreeBark;
                }
            }
            // 4 conical pine bough tiers
            for (uint tier = 0u; tier < 4u; ++tier)
            {
                const float tierBase = (1.7f + float(tier) * 1.25f) * scale;
                const float tierH = 1.95f * scale;
                const float tierMaxR = (2.35f - float(tier) * 0.42f) * scale;
                const float ty = dy - tierBase;
                if (ty >= 0.0f && ty <= tierH)
                {
                    const float coneR = tierMaxR * (1.0f - ty / tierH);
                    const float fuzz = (svValueNoise3D(wx * 1.5f, wy * 1.5f, wz * 1.5f, lm.variantSeed + tier * 29u) - 0.5f) * 0.32f;
                    if (svSqrt(dx * dx + dz * dz) <= coneR + fuzz)
                    {
                        return kMatPineNeedle;
                    }
                }
            }
            return currentMat;
        }

        return currentMat;
    }

    // =========================================================================
    // 2. Greedy Brick Voxelization + 4-Corner AO + Local Radiance Meshing
    // =========================================================================

    inline uint packVoxelQuadMeta0(
        uint dir,
        uint matId,
        uint ao00,
        uint ao10,
        uint ao01,
        uint ao11,
        uint flip,
        uint category,
        uint uSpan,
        uint vSpan)
    {
        return (dir & 7u) |
               ((matId & 31u) << 3u) |
               ((ao00 & 3u) << 8u) |
               ((ao10 & 3u) << 10u) |
               ((ao01 & 3u) << 12u) |
               ((ao11 & 3u) << 14u) |
               ((flip & 1u) << 16u) |
               ((category & 3u) << 17u) |
               (((uSpan - 1u) & 15u) << 19u) |
               (((vSpan - 1u) & 15u) << 23u);
    }

    inline uint packVoxelQuadMeta1(uint glowLevel, uint glowPalette, uint voxelHash)
    {
        return (glowLevel & 7u) |
               ((glowPalette & 3u) << 3u) |
               ((voxelHash & 255u) << 8u);
    }

    class EvaluateAndMeshBrickCPU
    {
    public:
        uint subBrickIndex = 0u;

        VoxelQuadrantPack operator()(IN VoxelClusterInput input) const
        {
            VoxelQuadrantPack out;
            const uint bx = subBrickIndex & 1u;
            const uint by = (subBrickIndex >> 1u) & 1u;
            const uint bz = (subBrickIndex >> 2u) & 1u;

            OctreeNodeSpec brickSpec;
            brickSpec.quadrantIndex = input.clusterNode.quadrantIndex;
            brickSpec.depth = 4u;
            brickSpec.gx = input.clusterNode.gx + bx * kBrickDim;
            brickSpec.gy = input.clusterNode.gy + by * kBrickDim;
            brickSpec.gz = input.clusterNode.gz + bz * kBrickDim;
            brickSpec.sizeVoxels = kBrickDim;

            const float vs = svMax(input.params.voxelSize, 0.05f);
            const float3 bMin = nodeBoxMin(brickSpec, input.params);
            const float3 bMax = nodeBoxMax(brickSpec, input.params);

            // Pad by 1 voxel on all sides for exact boundary culling & 4-corner AO
            const float3 pMin = float3(bMin.x - vs, bMin.y - vs, bMin.z - vs);
            const float3 pMax = float3(bMax.x + vs, bMax.y + vs, bMax.z + vs);

            const eastl::vector<VoxelLandmark> tileLandmarks = gatherLandmarksForXZBox(pMin, pMax, input.params);
            const uint intervalState = classifyBoundsInterval(pMin, pMax, input.params, tileLandmarks);
            if (intervalState != kIntervalMixedActive)
            {
                out.prunedBrickCount = 1u;
                return out;
            }

            // Filter landmarks strictly overlapping this brick's 18^3 padded AABB
            eastl::vector<VoxelLandmark> brickLandmarks;
            const uint totalLms = uint(tileLandmarks.size());
            for (uint i = 0u; i < totalLms; ++i)
            {
                const VoxelLandmark lm = tileLandmarks[i];
                if (aabbOverlaps(pMin, pMax, lm.minBound, lm.maxBound))
                {
                    brickLandmarks.push_back(lm);
                }
            }
            const uint localLmCount = uint(brickLandmarks.size());
            const eastl::vector<EmissivePointLight> brickLights = gatherLightsFor3DBox(pMin, pMax, input.params);
            const uint localLightCount = uint(brickLights.size());

            // Precompute 18x18 2D terrain/river columns once per brick
            eastl::vector<TerrainColumnInfo> columns(kPaddedDim * kPaddedDim);
            for (uint hz = 0u; hz < kPaddedDim; ++hz)
            {
                const float wz = bMin.z + (float(int(hz) - 1) + 0.5f) * vs;
                for (uint hx = 0u; hx < kPaddedDim; ++hx)
                {
                    const float wx = bMin.x + (float(int(hx) - 1) + 0.5f) * vs;
                    columns[hz * kPaddedDim + hx] = evalTerrainColumn(wx, wz, input.params);
                }
            }

            // Evaluate 18x18x18 padded pure micro-voxels
            eastl::vector<uint8_t> voxels(kPaddedDim * kPaddedDim * kPaddedDim, uint8_t(kMatAir));
            uint solidInside = 0u;
            uint waterInside = 0u;

            for (uint hz = 0u; hz < kPaddedDim; ++hz)
            {
                const float wz = bMin.z + (float(int(hz) - 1) + 0.5f) * vs;
                for (uint hx = 0u; hx < kPaddedDim; ++hx)
                {
                    const float wx = bMin.x + (float(int(hx) - 1) + 0.5f) * vs;
                    const TerrainColumnInfo col = columns[hz * kPaddedDim + hx];
                    const bool inRiverXZ = (col.riverDist < col.riverHalfW + 0.35f);
                    const bool insideXZ = (hx >= 1u && hx <= 16u && hz >= 1u && hz <= 16u);

                    for (uint hy = 0u; hy < kPaddedDim; ++hy)
                    {
                        const float wy = bMin.y + (float(int(hy) - 1) + 0.5f) * vs;
                        uint mat = kMatAir;

                        if (wy <= col.terrainY)
                        {
                            const float depthBelow = col.terrainY - wy;
                            if (depthBelow < 0.34f && col.slopeEst < 0.52f && (!inRiverXZ || wy > col.waterY + 0.15f))
                            {
                                mat = kMatMossyGrass;
                            }
                            else
                            {
                                const int strata = svFloorToInt(wy * 1.45f + svValueNoise2D(wx * 0.25f, wz * 0.25f, input.params.seed) * 1.8f);
                                mat = ((strata & 1) == 0) ? kMatLimestoneLight : kMatLimestoneDark;
                            }
                        }
                        else if (inRiverXZ && wy <= col.waterY)
                        {
                            mat = (col.foamMask > 0.38f && wy >= col.waterY - 0.35f) ? kMatFoam : kMatWater;
                        }

                        for (uint li = 0u; li < localLmCount; ++li)
                        {
                            mat = evalLandmarkVoxel(wx, wy, wz, brickLandmarks[li], mat, input.params);
                        }

                        voxels[(hy * kPaddedDim + hz) * kPaddedDim + hx] = uint8_t(mat);

                        if (insideXZ && hy >= 1u && hy <= 16u)
                        {
                            if (isSolidMaterial(mat))
                            {
                                ++solidInside;
                            }
                            else if (mat == kMatWater || mat == kMatFoam)
                            {
                                ++waterInside;
                            }
                        }
                    }
                }
            }

            if (solidInside == 0u && waterInside == 0u)
            {
                out.prunedBrickCount = 1u;
                return out;
            }

            out.activeBrickCount = 1u;
            out.solidVoxelCount = solidInside;

            // 6-Face Greedy Meshing + 4-Corner Voxel AO + Quantized Local Radiance
            eastl::vector<uint> mask(kBrickDim * kBrickDim, 0u);
            eastl::vector<uint> auxMask(kBrickDim * kBrickDim, 0u);

            for (uint dir = 0u; dir < 6u; ++dir)
            {
                const int nx = (dir == 0u) ? 1 : ((dir == 1u) ? -1 : 0);
                const int ny = (dir == 2u) ? 1 : ((dir == 3u) ? -1 : 0);
                const int nz = (dir == 4u) ? 1 : ((dir == 5u) ? -1 : 0);

                // U and V tangent vectors in integer voxel space
                int ux = 0;
                int uy = 0;
                int uz = 0;
                int vx = 0;
                int vy = 0;
                int vz = 0;
                if (dir < 2u)
                {
                    uz = 1;
                    vy = 1;
                }
                else if (dir < 4u)
                {
                    ux = 1;
                    vz = 1;
                }
                else
                {
                    ux = 1;
                    vy = 1;
                }

                for (uint slice = 0u; slice < kBrickDim; ++slice)
                {
                    for (uint v = 0u; v < kBrickDim; ++v)
                    {
                        for (uint u = 0u; u < kBrickDim; ++u)
                        {
                            const int ix = 1 + ((dir < 2u) ? int(slice) : int(u));
                            const int iy = 1 + ((dir == 2u || dir == 3u) ? int(slice) : int(v));
                            const int iz = 1 + ((dir < 2u) ? int(u) : ((dir < 4u) ? int(v) : int(slice)));

                            const uint matA = uint(voxels[(uint(iy) * kPaddedDim + uint(iz)) * kPaddedDim + uint(ix)]);
                            const uint cellIdx = v * kBrickDim + u;
                            if (matA == kMatAir)
                            {
                                mask[cellIdx] = 0u;
                                continue;
                            }

                            const int nbx = ix + nx;
                            const int nby = iy + ny;
                            const int nbz = iz + nz;
                            const uint matB = uint(voxels[(uint(nby) * kPaddedDim + uint(nbz)) * kPaddedDim + uint(nbx)]);

                            const bool solidA = isSolidMaterial(matA);
                            const bool exposed = solidA ? (!isSolidMaterial(matB)) : (matB == kMatAir);
                            if (!exposed)
                            {
                                mask[cellIdx] = 0u;
                                continue;
                            }

                            ++out.rawFaceCount;

                            uint ao00 = 3u;
                            uint ao10 = 3u;
                            uint ao01 = 3u;
                            uint ao11 = 3u;
                            uint flip = 0u;

                            if (solidA)
                            {
                                for (uint c = 0u; c < 4u; ++c)
                                {
                                    const int su = ((c & 1u) == 0u) ? -1 : 1;
                                    const int sv = ((c & 2u) == 0u) ? -1 : 1;

                                    const uint s1x = uint(nbx + su * ux);
                                    const uint s1y = uint(nby + su * uy);
                                    const uint s1z = uint(nbz + su * uz);
                                    const uint s1 = isSolidMaterial(uint(voxels[(s1y * kPaddedDim + s1z) * kPaddedDim + s1x])) ? 1u : 0u;

                                    const uint s2x = uint(nbx + sv * vx);
                                    const uint s2y = uint(nby + sv * vy);
                                    const uint s2z = uint(nbz + sv * vz);
                                    const uint s2 = isSolidMaterial(uint(voxels[(s2y * kPaddedDim + s2z) * kPaddedDim + s2x])) ? 1u : 0u;

                                    const uint cx = uint(nbx + su * ux + sv * vx);
                                    const uint cy = uint(nby + su * uy + sv * vy);
                                    const uint cz = uint(nbz + su * uz + sv * vz);
                                    const uint cr = isSolidMaterial(uint(voxels[(cy * kPaddedDim + cz) * kPaddedDim + cx])) ? 1u : 0u;

                                    const uint ao = (s1 != 0u && s2 != 0u) ? 0u : (3u - (s1 + s2 + cr));
                                    if (c == 0u) { ao00 = ao; }
                                    else if (c == 1u) { ao10 = ao; }
                                    else if (c == 2u) { ao01 = ao; }
                                    else { ao11 = ao; }
                                }
                                flip = (ao00 + ao11 < ao10 + ao01) ? 1u : 0u;
                            }

                            // Sample local emissive radiance from nearby lanterns, crystals & mushrooms
                            uint glowLevel = 0u;
                            uint glowPalette = 0u;
                            if (localLightCount > 0u)
                            {
                                const float faceX = bMin.x + (float(ix - 1) + 0.5f + float(nx) * 0.5f) * vs;
                                const float faceY = bMin.y + (float(iy - 1) + 0.5f + float(ny) * 0.5f) * vs;
                                const float faceZ = bMin.z + (float(iz - 1) + 0.5f + float(nz) * 0.5f) * vs;
                                float bestLight = 0.0f;
                                float totalLight = 0.0f;
                                for (uint li = 0u; li < localLightCount; ++li)
                                {
                                    const EmissivePointLight pl = brickLights[li];
                                    const float ldx = pl.pos.x - faceX;
                                    const float ldy = pl.pos.y - faceY;
                                    const float ldz = pl.pos.z - faceZ;
                                    const float d = svSqrt(ldx * ldx + ldy * ldy + ldz * ldz);
                                    if (d < pl.radius)
                                    {
                                        const float att = (1.0f - d / pl.radius);
                                        const float ndotl = (d > 1e-4f)
                                            ? svMax(0.22f, (ldx * float(nx) + ldy * float(ny) + ldz * float(nz)) / d * 0.7f + 0.35f)
                                            : 1.0f;
                                        const float contrib = att * att * pl.strength * ndotl;
                                        totalLight += contrib;
                                        if (contrib > bestLight)
                                        {
                                            bestLight = contrib;
                                            glowPalette = pl.palette;
                                        }
                                    }
                                }
                                glowLevel = uint(svClamp(totalLight * 6.0f, 0.0f, 7.0f));
                            }

                            const uint hashByte = svHashCoords3D(
                                int(brickSpec.gx) + ix - 1,
                                int(brickSpec.gy) + iy - 1,
                                int(brickSpec.gz) + iz - 1,
                                input.params.seed) & 255u;

                            mask[cellIdx] = (1u << 31u) |
                                (matA & 31u) |
                                ((ao00 & 3u) << 5u) |
                                ((ao10 & 3u) << 7u) |
                                ((ao01 & 3u) << 9u) |
                                ((ao11 & 3u) << 11u) |
                                ((flip & 1u) << 13u) |
                                ((glowLevel & 7u) << 14u) |
                                ((glowPalette & 3u) << 17u);
                            auxMask[cellIdx] = hashByte;
                        }
                    }

                    // 2D Greedy Rectangle Merging across (u, v)
                    for (uint v = 0u; v < kBrickDim; ++v)
                    {
                        for (uint u = 0u; u < kBrickDim; )
                        {
                            const uint key = mask[v * kBrickDim + u];
                            if (key == 0u)
                            {
                                ++u;
                                continue;
                            }

                            uint w = 1u;
                            while (u + w < kBrickDim && mask[v * kBrickDim + (u + w)] == key)
                            {
                                ++w;
                            }

                            uint h = 1u;
                            bool canGrow = true;
                            while (v + h < kBrickDim && canGrow)
                            {
                                for (uint k = 0u; k < w; ++k)
                                {
                                    if (mask[(v + h) * kBrickDim + (u + k)] != key)
                                    {
                                        canGrow = false;
                                        break;
                                    }
                                }
                                if (canGrow)
                                {
                                    ++h;
                                }
                            }

                            for (uint dv = 0u; dv < h; ++dv)
                            {
                                for (uint du = 0u; du < w; ++du)
                                {
                                    mask[(v + dv) * kBrickDim + (u + du)] = 0u;
                                }
                            }

                            const uint matId = key & 31u;
                            const uint ao00 = (key >> 5u) & 3u;
                            const uint ao10 = (key >> 7u) & 3u;
                            const uint ao01 = (key >> 9u) & 3u;
                            const uint ao11 = (key >> 11u) & 3u;
                            const uint flip = (key >> 13u) & 1u;
                            const uint glowLevel = (key >> 14u) & 7u;
                            const uint glowPalette = (key >> 17u) & 3u;
                            const uint cat = materialQuadCategory(matId);
                            const uint hashByte = auxMask[v * kBrickDim + u];

                            // Quad corner in world space
                            const uint oxVoxel = (dir < 2u) ? (slice + (dir == 0u ? 1u : 0u)) : u;
                            const uint oyVoxel = (dir == 2u || dir == 3u) ? (slice + (dir == 2u ? 1u : 0u)) : v;
                            const uint ozVoxel = (dir < 2u) ? u : ((dir < 4u) ? v : (slice + (dir == 4u ? 1u : 0u)));

                            VoxelQuad quad;
                            quad.originAndAnim = float4(
                                bMin.x + float(oxVoxel) * vs,
                                bMin.y + float(oyVoxel) * vs,
                                bMin.z + float(ozVoxel) * vs,
                                0.0f);
                            quad.meta = uint4(
                                packVoxelQuadMeta0(dir, matId, ao00, ao10, ao01, ao11, flip, cat, w, h),
                                packVoxelQuadMeta1(glowLevel, glowPalette, hashByte),
                                0u,
                                0u);

                            if (cat == kQuadCatOpaque)
                            {
                                out.opaqueQuads.push_back(quad);
                            }
                            else if (cat == kQuadCatFoliageEmissive)
                            {
                                out.foliageQuads.push_back(quad);
                            }
                            else
                            {
                                out.waterQuads.push_back(quad);
                            }
                            ++out.greedyQuadCount;
                            u += w;
                        }
                    }
                }
            }

            return out;
        }
    };

    // =========================================================================
    // 3. Deterministic Binary Reduction & Frame Summary Helpers
    // =========================================================================

    inline VoxelQuadrantPack mergeVoxelBrickMeshes(IN VoxelQuadrantPack a, IN VoxelQuadrantPack b)
    {
        VoxelQuadrantPack merged;

        const uint aOp = uint(a.opaqueQuads.size());
        const uint bOp = uint(b.opaqueQuads.size());
        merged.opaqueQuads.resize(aOp + bOp);
        for (uint i = 0u; i < aOp; ++i) { merged.opaqueQuads[i] = a.opaqueQuads[i]; }
        for (uint i = 0u; i < bOp; ++i) { merged.opaqueQuads[aOp + i] = b.opaqueQuads[i]; }

        const uint aFol = uint(a.foliageQuads.size());
        const uint bFol = uint(b.foliageQuads.size());
        merged.foliageQuads.resize(aFol + bFol);
        for (uint i = 0u; i < aFol; ++i) { merged.foliageQuads[i] = a.foliageQuads[i]; }
        for (uint i = 0u; i < bFol; ++i) { merged.foliageQuads[aFol + i] = b.foliageQuads[i]; }

        const uint aWat = uint(a.waterQuads.size());
        const uint bWat = uint(b.waterQuads.size());
        merged.waterQuads.resize(aWat + bWat);
        for (uint i = 0u; i < aWat; ++i) { merged.waterQuads[i] = a.waterQuads[i]; }
        for (uint i = 0u; i < bWat; ++i) { merged.waterQuads[aWat + i] = b.waterQuads[i]; }

        merged.activeBrickCount = a.activeBrickCount + b.activeBrickCount;
        merged.prunedBrickCount = a.prunedBrickCount + b.prunedBrickCount;
        merged.solidVoxelCount = a.solidVoxelCount + b.solidVoxelCount;
        merged.rawFaceCount = a.rawFaceCount + b.rawFaceCount;
        merged.greedyQuadCount = a.greedyQuadCount + b.greedyQuadCount;
        merged.nestedSubgraphCount = a.nestedSubgraphCount + b.nestedSubgraphCount;
        return merged;
    }

    inline VoxelQuadrantPack tagClusterSubgraphCount(IN VoxelQuadrantPack clusterMesh)
    {
        VoxelQuadrantPack out = clusterMesh;
        out.nestedSubgraphCount += 1u;
        return out;
    }

    inline SparseVoxelFrameSummary summarizeVoxelQuadrants(
        IN VoxelQuadrantPack q0,
        IN VoxelQuadrantPack q1,
        IN VoxelQuadrantPack q2,
        IN VoxelQuadrantPack q3,
        IN uint octreeInteriorCount)
    {
        SparseVoxelFrameSummary summary;
        summary.virtualVoxelCount = uint64_t(kGridDimX) * uint64_t(kGridDimY) * uint64_t(kGridDimZ);
        summary.totalBrickCount = (kGridDimX / kBrickDim) * (kGridDimY / kBrickDim) * (kGridDimZ / kBrickDim);
        summary.activeBrickCount = q0.activeBrickCount + q1.activeBrickCount + q2.activeBrickCount + q3.activeBrickCount;
        summary.prunedBrickCount = q0.prunedBrickCount + q1.prunedBrickCount + q2.prunedBrickCount + q3.prunedBrickCount;
        summary.solidVoxelCount = q0.solidVoxelCount + q1.solidVoxelCount + q2.solidVoxelCount + q3.solidVoxelCount;
        summary.rawFaceCount = q0.rawFaceCount + q1.rawFaceCount + q2.rawFaceCount + q3.rawFaceCount;
        summary.greedyQuadCount = q0.greedyQuadCount + q1.greedyQuadCount + q2.greedyQuadCount + q3.greedyQuadCount;
        summary.triangleCount = summary.greedyQuadCount * 2u;
        summary.nestedSubgraphCount =
            q0.nestedSubgraphCount + q1.nestedSubgraphCount + q2.nestedSubgraphCount + q3.nestedSubgraphCount;
        summary.octreeInteriorCount = octreeInteriorCount;
        return summary;
    }

    inline SparseVoxelFrameSummary finalizeSwapchainSummary(IN SparseVoxelFrameSummary summary, IN uint presentToken)
    {
        SparseVoxelFrameSummary out = summary;
        if (presentToken == 0u)
        {
            out.activeBrickCount = 0u;
        }
        return out;
    }

    inline SparseVoxelBenchmarkResult evaluateBenchmarkPixels(
        IN SparseVoxelFrameSummary summary,
        IN eastl::vector<uint8_t> pixels)
    {
        SparseVoxelBenchmarkResult result;
        result.summary = summary;
        uint nonSky = 0u;
        uint hash = 2166136261u;
        const uint byteCount = uint(pixels.size());
        for (uint i = 0u; i + 3u < byteCount; i += 4u)
        {
            const uint r = uint(pixels[i + 0u]);
            const uint g = uint(pixels[i + 1u]);
            const uint b = uint(pixels[i + 2u]);
            // Clear sky color ~(140, 186, 232) -> (0.55, 0.73, 0.91)
            const int dr = int(r) - 140;
            const int dg = int(g) - 186;
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
} // namespace SparseVoxelSample
