#pragma once
#include "ProceduralCityTypesAndMath.hpp"

namespace ProceduralCitySample
{
    // =========================================================================
    // 0. Deterministic Urban Canyon Sky-View Occlusion & Static Mesh Builder
    // =========================================================================

    /// Evaluates procedural street-canyon sky-view occlusion and architectural tint variation
    /// from neighboring district height envelopes and multi-octave urban roughness noise.
    inline float sampleUrbanSkyViewTint(float wx, float wy, float wz, float baseTint, CityParams params)
    {
        float occlusion = 0.0f;
        const float rCenter = citySqrt(wx * wx + wz * wz);
        const float distCbd = cityClamp(1.0f - rCenter / cityMax(params.cbdRadius * 1.15f, 1.0f), 0.0f, 1.0f);
        for (uint dir = 0u; dir < 8u; ++dir)
        {
            const float ang = float(dir) * 0.78539816f + 0.1963495f;
            const float dx = cityCos(ang);
            const float dz = citySin(ang);
            float maxHorizon = 0.0f;
            for (uint ring = 1u; ring <= 3u; ++ring)
            {
                const float sampleDist = 14.0f * float(ring);
                const float sx = wx + dx * sampleDist;
                const float sz = wz + dz * sampleDist;
                const float canalGap = cityAbs(sx - cityCanalCenterX(sz, params.canalAmplitude, params.canalFrequency, params.seed));
                const float sr = citySqrt(sx * sx + sz * sz);
                float envH = 9.0f;
                if (canalGap < 16.0f)
                {
                    envH = 1.0f;
                }
                else if (sr < params.cbdRadius)
                {
                    envH = 42.0f + 36.0f * cityValueNoise2D(sx * 0.045f, sz * 0.045f, params.seed + 13u);
                }
                else if (sr < params.commercialRadius)
                {
                    envH = 18.0f + 14.0f * cityValueNoise2D(sx * 0.055f, sz * 0.055f, params.seed + 37u);
                }
                else
                {
                    envH = 8.0f + 5.0f * cityValueNoise2D(sx * 0.07f, sz * 0.07f, params.seed + 71u);
                }
                const float deltaH = cityMax(envH - wy, 0.0f);
                const float slope = deltaH / (sampleDist + deltaH);
                if (slope > maxHorizon)
                {
                    maxHorizon = slope;
                }
            }
            occlusion += maxHorizon;
        }
        const float skyView = cityClamp(1.0f - (occlusion * 0.125f) * (0.45f + 0.35f * distCbd), 0.25f, 1.0f);
        return cityClamp(baseTint * (0.68f + 0.32f * skyView), 0.05f, 1.0f);
    }

    struct CityMeshBuilder
    {
        eastl::vector<CityArchVertex> vertices;

        void addTri(
            float3 p0, float3 p1, float3 p2,
            float3 n, float matId, float tint,
            float2 uv0, float2 uv1, float2 uv2,
            float seedMeta, float floorMeta)
        {
            CityArchVertex v0;
            v0.posAndMat = float4(p0.x, p0.y, p0.z, matId);
            v0.normalAndTint = float4(n.x, n.y, n.z, tint);
            v0.uvAndMeta = float4(uv0.x, uv0.y, seedMeta, floorMeta);

            CityArchVertex v1;
            v1.posAndMat = float4(p1.x, p1.y, p1.z, matId);
            v1.normalAndTint = float4(n.x, n.y, n.z, tint);
            v1.uvAndMeta = float4(uv1.x, uv1.y, seedMeta, floorMeta);

            CityArchVertex v2;
            v2.posAndMat = float4(p2.x, p2.y, p2.z, matId);
            v2.normalAndTint = float4(n.x, n.y, n.z, tint);
            v2.uvAndMeta = float4(uv2.x, uv2.y, seedMeta, floorMeta);

            vertices.push_back(v0);
            vertices.push_back(v1);
            vertices.push_back(v2);
        }

        void addQuad(
            float3 p0, float3 p1, float3 p2, float3 p3,
            float3 n, float matId, float tint,
            float2 uv0, float2 uv1, float2 uv2, float2 uv3,
            float seedMeta, float floorMeta)
        {
            addTri(p0, p1, p2, n, matId, tint, uv0, uv1, uv2, seedMeta, floorMeta);
            addTri(p0, p2, p3, n, matId, tint, uv0, uv2, uv3, seedMeta, floorMeta);
        }

        void addFlatQuadXZ(
            float minX, float maxX, float minZ, float maxZ, float y,
            float matId, float tint, float seedMeta)
        {
            const float w = maxX - minX;
            const float d = maxZ - minZ;
            addQuad(
                float3(minX, y, minZ),
                float3(maxX, y, minZ),
                float3(maxX, y, maxZ),
                float3(minX, y, maxZ),
                float3(0.0f, 1.0f, 0.0f),
                matId, tint,
                float2(0.0f, 0.0f), float2(w, 0.0f), float2(w, d), float2(0.0f, d),
                seedMeta, 1.0f);
        }

        void addBox(
            float minX, float minY, float minZ,
            float maxX, float maxY, float maxZ,
            float sideMatId, float topMatId, float tint,
            float seedMeta, float floorHeight)
        {
            const float w = maxX - minX;
            const float h = maxY - minY;
            const float d = maxZ - minZ;

            // Top (+Y)
            addQuad(
                float3(minX, maxY, minZ),
                float3(maxX, maxY, minZ),
                float3(maxX, maxY, maxZ),
                float3(minX, maxY, maxZ),
                float3(0.0f, 1.0f, 0.0f), topMatId, tint,
                float2(0.0f, 0.0f), float2(w, 0.0f), float2(w, d), float2(0.0f, d),
                seedMeta, floorHeight);

            // North (+Z)
            addQuad(
                float3(minX, minY, maxZ),
                float3(maxX, minY, maxZ),
                float3(maxX, maxY, maxZ),
                float3(minX, maxY, maxZ),
                float3(0.0f, 0.0f, 1.0f), sideMatId, tint,
                float2(0.0f, 0.0f), float2(w, 0.0f), float2(w, h), float2(0.0f, h),
                seedMeta, floorHeight);

            // South (-Z)
            addQuad(
                float3(maxX, minY, minZ),
                float3(minX, minY, minZ),
                float3(minX, maxY, minZ),
                float3(maxX, maxY, minZ),
                float3(0.0f, 0.0f, -1.0f), sideMatId, tint,
                float2(0.0f, 0.0f), float2(w, 0.0f), float2(w, h), float2(0.0f, h),
                seedMeta, floorHeight);

            // East (+X)
            addQuad(
                float3(maxX, minY, maxZ),
                float3(maxX, minY, minZ),
                float3(maxX, maxY, minZ),
                float3(maxX, maxY, maxZ),
                float3(1.0f, 0.0f, 0.0f), sideMatId, tint,
                float2(0.0f, 0.0f), float2(d, 0.0f), float2(d, h), float2(0.0f, h),
                seedMeta, floorHeight);

            // West (-X)
            addQuad(
                float3(minX, minY, minZ),
                float3(minX, minY, maxZ),
                float3(minX, maxY, maxZ),
                float3(minX, maxY, minZ),
                float3(-1.0f, 0.0f, 0.0f), sideMatId, tint,
                float2(0.0f, 0.0f), float2(d, 0.0f), float2(d, h), float2(0.0f, h),
                seedMeta, floorHeight);
        }

        void addTaperedPyramid(
            float cx, float baseY, float cz,
            float halfW, float halfD, float topHalfW, float topHalfD, float height,
            float matId, float tint, float seedMeta)
        {
            const float x0 = cx - halfW;
            const float x1 = cx + halfW;
            const float z0 = cz - halfD;
            const float z1 = cz + halfD;
            const float tx0 = cx - topHalfW;
            const float tx1 = cx + topHalfW;
            const float tz0 = cz - topHalfD;
            const float tz1 = cz + topHalfD;
            const float topY = baseY + height;

            addQuad(
                float3(x0, baseY, z1), float3(x1, baseY, z1),
                float3(tx1, topY, tz1), float3(tx0, topY, tz1),
                float3(0.0f, 0.45f, 0.89f), matId, tint,
                float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, height), float2(0.0f, height),
                seedMeta, 1.0f);
            addQuad(
                float3(x1, baseY, z0), float3(x0, baseY, z0),
                float3(tx0, topY, tz0), float3(tx1, topY, tz0),
                float3(0.0f, 0.45f, -0.89f), matId, tint,
                float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, height), float2(0.0f, height),
                seedMeta, 1.0f);
            addQuad(
                float3(x1, baseY, z1), float3(x1, baseY, z0),
                float3(tx1, topY, tz0), float3(tx1, topY, tz1),
                float3(0.89f, 0.45f, 0.0f), matId, tint,
                float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, height), float2(0.0f, height),
                seedMeta, 1.0f);
            addQuad(
                float3(x0, baseY, z0), float3(x0, baseY, z1),
                float3(tx0, topY, tz1), float3(tx0, topY, tz0),
                float3(-0.89f, 0.45f, 0.0f), matId, tint,
                float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, height), float2(0.0f, height),
                seedMeta, 1.0f);
        }

        void applyCanyonOcclusion(CityParams params)
        {
            const uint count = uint(vertices.size());
            for (uint i = 0u; i < count; ++i)
            {
                const float matId = vertices[i].posAndMat.w;
                // Keep glass tint, lane markings, and canal water reflections untouched; modulate masonry/stone/pavement.
                if (matId < 1.5f || (matId > 2.5f && matId < 3.5f) || (matId > 4.5f && matId < 5.5f) || (matId > 6.5f && matId < 11.5f))
                {
                    vertices[i].normalAndTint.w = sampleUrbanSkyViewTint(
                        vertices[i].posAndMat.x,
                        vertices[i].posAndMat.y,
                        vertices[i].posAndMat.z,
                        vertices[i].normalAndTint.w,
                        params);
                }
            }
        }
    };

    inline float roadHalfWidthAtGridLine(uint gridIdx)
    {
        if (gridIdx == 2u || gridIdx == 4u || gridIdx == 6u)
        {
            return 9.0f;
        }
        return 7.0f;
    }

    // =========================================================================
    // 1. Phase 1: Data-Dependent Urban Quadrant Road & Parcel Lot Subdivision
    // =========================================================================

    inline QuadrantSubdivisionPlan subdivideUrbanQuadrantLots(IN CityParams params, IN uint quadrantIndex)
    {
        QuadrantSubdivisionPlan plan;
        plan.quadrantIndex = quadrantIndex;

        const uint qx = quadrantIndex & 1u;
        const uint qz = (quadrantIndex >> 1u) & 1u;
        const uint gxStart = qx * 4u;
        const uint gzStart = qz * 4u;
        const float cellSize = 60.0f;

        uint nextBlockId = quadrantIndex * 1000u + 1u;
        uint nextCorrId = quadrantIndex * 1000u + 1u;

        // 1A. Subdivide 4x4 macro cells in this quadrant into CBD, Commercial, Residential, Park, or Canal banks.
        for (uint lz = 0u; lz < 4u; ++lz)
        {
            const uint gz = gzStart + lz;
            const float cellMinZ = -240.0f + float(gz) * cellSize;
            const float lotMinZ = cellMinZ + roadHalfWidthAtGridLine(gz);
            const float lotMaxZ = cellMinZ + cellSize - roadHalfWidthAtGridLine(gz + 1u);

            for (uint lx = 0u; lx < 4u; ++lx)
            {
                const uint gx = gxStart + lx;
                const float cellMinX = -240.0f + float(gx) * cellSize;
                const float lotMinX = cellMinX + roadHalfWidthAtGridLine(gx);
                const float lotMaxX = cellMinX + cellSize - roadHalfWidthAtGridLine(gx + 1u);

                // Canal strip occupies gx == 4 (x in [0, 60]).
                if (gx == 4u)
                {
                    CityCorridorSpec canalSpec;
                    canalSpec.params = params;
                    canalSpec.quadrantIndex = quadrantIndex;
                    canalSpec.corridorId = nextCorrId++;
                    canalSpec.corridorKind = 2u; // Winding canal + stone embankments + promenades
                    canalSpec.minX = lotMinX;
                    canalSpec.maxX = lotMaxX;
                    canalSpec.minZ = lotMinZ;
                    canalSpec.maxZ = lotMaxZ;
                    canalSpec.laneCount = 0u;
                    plan.corridors.push_back(canalSpec);

                    // Waterfront dry banks on west/east edges of the canal cell become civic waterfront parks.
                    const float zMid = 0.5f * (lotMinZ + lotMaxZ);
                    const float cxMid = cityCanalCenterX(zMid, params.canalAmplitude, params.canalFrequency, params.seed);
                    const float leftBankMax = cxMid - 12.0f;
                    const float rightBankMin = cxMid + 12.0f;
                    if (leftBankMax - lotMinX > 5.0f)
                    {
                        CityBlockSpec westPark;
                        westPark.params = params;
                        westPark.quadrantIndex = quadrantIndex;
                        westPark.blockId = nextBlockId++;
                        westPark.districtKind = 3u;
                        westPark.minX = lotMinX;
                        westPark.maxX = leftBankMax;
                        westPark.minZ = lotMinZ;
                        westPark.maxZ = lotMaxZ;
                        westPark.waterfrontRatio = 1.0f;
                        westPark.densityFactor = 0.9f;
                        plan.midriseAndResBlocks.push_back(westPark);
                    }
                    if (lotMaxX - rightBankMin > 5.0f)
                    {
                        CityBlockSpec eastPark;
                        eastPark.params = params;
                        eastPark.quadrantIndex = quadrantIndex;
                        eastPark.blockId = nextBlockId++;
                        eastPark.districtKind = 3u;
                        eastPark.minX = rightBankMin;
                        eastPark.maxX = lotMaxX;
                        eastPark.minZ = lotMinZ;
                        eastPark.maxZ = lotMaxZ;
                        eastPark.waterfrontRatio = 1.0f;
                        eastPark.densityFactor = 0.9f;
                        plan.midriseAndResBlocks.push_back(eastPark);
                    }
                    continue;
                }

                const float cx = 0.5f * (lotMinX + lotMaxX);
                const float cz = 0.5f * (lotMinZ + lotMaxZ);
                const float radialDist = citySqrt(cx * cx + cz * cz);
                const float organicBias = (cityHash01(int(gx), int(gz), params.seed + 51u) - 0.5f) * 20.0f;
                const float effDist = radialDist + organicBias;

                CityBlockSpec block;
                block.params = params;
                block.quadrantIndex = quadrantIndex;
                block.blockId = nextBlockId++;
                block.minX = lotMinX;
                block.minZ = lotMinZ;
                block.maxX = lotMaxX;
                block.maxZ = lotMaxZ;
                block.waterfrontRatio = (gx == 3u || gx == 5u) ? 0.5f : 0.0f;

                // Central Park & Civic Waterfront district (3 contiguous blocks east of the canal).
                if (gx == 5u && (gz == 2u || gz == 3u || gz == 4u))
                {
                    block.districtKind = 3u;
                    block.densityFactor = (gz == 3u) ? 1.0f : 0.75f; // gz==3 gets central monument fountain
                    plan.midriseAndResBlocks.push_back(block);
                }
                else if (effDist < params.cbdRadius)
                {
                    block.districtKind = 0u; // Downtown CBD Skyscraper
                    block.densityFactor = cityClamp(1.18f - radialDist / cityMax(params.cbdRadius, 1.0f) * 0.45f, 0.55f, 1.18f);
                    plan.skyscraperBlocks.push_back(block);
                }
                else if (effDist < params.commercialRadius)
                {
                    block.districtKind = 1u; // Commercial Mid-Rise
                    block.densityFactor = cityClamp(1.05f - (radialDist - params.cbdRadius) / 90.0f * 0.35f, 0.6f, 1.05f);
                    plan.midriseAndResBlocks.push_back(block);
                }
                else
                {
                    block.districtKind = 2u; // Courtyard Residential
                    block.densityFactor = 0.75f + 0.3f * cityHash01(int(gx), int(gz), params.seed + 99u);
                    plan.midriseAndResBlocks.push_back(block);
                }
            }
        }

        // 1B. Emit Roadway & Bridge Corridor Specs cleanly partitioned per quadrant.
        // East-West streets (along X): owned for gz lines in [gzStart, gzStart+3] plus gz==8 for qz==1.
        const uint ewLineCount = (qz == 1u) ? 5u : 4u;
        for (uint lz = 0u; lz < ewLineCount; ++lz)
        {
            const uint gzLine = gzStart + lz;
            const float zCenter = -240.0f + float(gzLine) * cellSize;
            const float halfW = roadHalfWidthAtGridLine(gzLine);
            const uint lanes = halfW > 8.0f ? 4u : 2u;

            for (uint lx = 0u; lx < 4u; ++lx)
            {
                const uint gx = gxStart + lx;
                const float x0 = -240.0f + float(gx) * cellSize;
                const float x1 = x0 + cellSize;

                CityCorridorSpec corr;
                corr.params = params;
                corr.quadrantIndex = quadrantIndex;
                corr.corridorId = nextCorrId++;
                corr.minX = x0;
                corr.maxX = x1;
                corr.minZ = zCenter - halfW;
                corr.maxZ = zCenter + halfW;
                corr.laneCount = lanes;

                if (gx == 4u)
                {
                    // Boulevard crosses the winding canal via a multi-span arched bridge.
                    corr.corridorKind = 3u;
                    corr.bridgeCenterX = cityCanalCenterX(zCenter, params.canalAmplitude, params.canalFrequency, params.seed);
                    corr.bridgeHalfSpan = 16.5f;
                }
                else
                {
                    corr.corridorKind = 0u;
                }
                plan.corridors.push_back(corr);
            }
        }

        // North-South streets (along Z): owned for gx lines in [gxStart, gxStart+3] plus gx==8 for qx==1.
        const uint nsLineCount = (qx == 1u) ? 5u : 4u;
        for (uint lx = 0u; lx < nsLineCount; ++lx)
        {
            const uint gxLine = gxStart + lx;
            const float xCenter = -240.0f + float(gxLine) * cellSize;
            const float halfW = roadHalfWidthAtGridLine(gxLine);
            const uint lanes = halfW > 8.0f ? 4u : 2u;

            for (uint lz = 0u; lz < 4u; ++lz)
            {
                const uint gz = gzStart + lz;
                const float z0 = -240.0f + float(gz) * cellSize;
                const float z1 = z0 + cellSize;

                CityCorridorSpec corr;
                corr.params = params;
                corr.quadrantIndex = quadrantIndex;
                corr.corridorId = nextCorrId++;
                corr.corridorKind = 1u;
                corr.minX = xCenter - halfW;
                corr.maxX = xCenter + halfW;
                corr.minZ = z0;
                corr.maxZ = z1;
                corr.laneCount = lanes;
                plan.corridors.push_back(corr);
            }
        }

        return plan;
    }

    // =========================================================================
    // 2. Skyscraper District Subgraph CPU Tasks
    // =========================================================================

    struct SkyscraperMassingResult
    {
        eastl::vector<CityArchVertex> archVertices;
        eastl::vector<float4> towerRoofs; // xyz = roof center, w = spireTopY
        uint towerCount = 0u;
    };

    inline SkyscraperMassingResult generateSkyscraperMassingCPU(IN CityBlockSpec spec)
    {
        SkyscraperMassingResult res;
        CityMeshBuilder mb;
        const uint bSeed = spec.params.seed ^ (spec.blockId * 0x9e3779b1u + 17u);

        const float bw = spec.maxX - spec.minX;
        const float bd = spec.maxZ - spec.minZ;
        // Granite plaza slab under the entire CBD block.
        mb.addBox(spec.minX, 0.0f, spec.minZ, spec.maxX, 0.25f, spec.maxZ, 3.0f, 3.0f, 0.55f, float(spec.blockId), 1.0f);

        const float splitPick = cityHash01(int(spec.blockId), 3, bSeed);
        const uint numTowers = (bw > 34.0f && bd > 34.0f && splitPick > 0.42f) ? 2u : 1u;

        for (uint t = 0u; t < numTowers; ++t)
        {
            const uint tSeed = bSeed + t * 4099u;
            const float hRand = cityHash01(int(t), 11, tSeed);
            const float tint = cityHash01(int(t), 23, tSeed);

            float pMinX = spec.minX + 2.5f;
            float pMaxX = spec.maxX - 2.5f;
            float pMinZ = spec.minZ + 2.5f;
            float pMaxZ = spec.maxZ - 2.5f;
            if (numTowers == 2u)
            {
                const float midX = 0.5f * (spec.minX + spec.maxX);
                if (t == 0u)
                {
                    pMaxX = midX - 1.5f;
                }
                else
                {
                    pMinX = midX + 1.5f;
                }
            }

            const float podH = 5.5f + 3.5f * cityHash01(int(t), 31, tSeed);
            // Podium (material 7 = dark stone/metal)
            mb.addBox(pMinX, 0.25f, pMinZ, pMaxX, podH, pMaxZ, 7.0f, 3.0f, 0.35f + 0.35f * tint, float(tSeed & 1023u), 3.4f);

            const float inset1 = 2.2f + 1.4f * cityHash01(int(t), 41, tSeed);
            const float sMinX = pMinX + inset1;
            const float sMaxX = pMaxX - inset1;
            const float sMinZ = pMinZ + inset1;
            const float sMaxZ = pMaxZ - inset1;

            const float baseHeight = (38.0f + 44.0f * hRand) * spec.densityFactor;
            const float tier1Top = podH + baseHeight;

            // Main Glass Curtain-Wall Shaft (material 6)
            mb.addBox(sMinX, podH, sMinZ, sMaxX, tier1Top, sMaxZ, 6.0f, 7.0f, tint, float(tSeed & 4095u), 3.4f);

            // Corner structural aluminum/steel pilasters (material 7) for crisp silhouette
            const float fin = 0.75f;
            mb.addBox(sMinX - 0.15f, podH, sMinZ - 0.15f, sMinX + fin, tier1Top + 0.6f, sMinZ + fin, 7.0f, 7.0f, 0.85f, 1.0f, 1.0f);
            mb.addBox(sMaxX - fin, podH, sMinZ - 0.15f, sMaxX + 0.15f, tier1Top + 0.6f, sMinZ + fin, 7.0f, 7.0f, 0.85f, 1.0f, 1.0f);
            mb.addBox(sMinX - 0.15f, podH, sMaxZ - fin, sMinX + fin, tier1Top + 0.6f, sMaxZ + 0.15f, 7.0f, 7.0f, 0.85f, 1.0f, 1.0f);
            mb.addBox(sMaxX - fin, podH, sMaxZ - fin, sMaxX + 0.15f, tier1Top + 0.6f, sMaxZ + 0.15f, 7.0f, 7.0f, 0.85f, 1.0f, 1.0f);

            // Mid-facade architectural spine ribs
            const float midTowerX = 0.5f * (sMinX + sMaxX);
            const float midTowerZ = 0.5f * (sMinZ + sMaxZ);
            mb.addBox(midTowerX - 0.45f, podH, sMinZ - 0.28f, midTowerX + 0.45f, tier1Top + 0.4f, sMinZ + 0.25f, 7.0f, 7.0f, 0.75f, 2.0f, 1.0f);
            mb.addBox(midTowerX - 0.45f, podH, sMaxZ - 0.25f, midTowerX + 0.45f, tier1Top + 0.4f, sMaxZ + 0.28f, 7.0f, 7.0f, 0.75f, 2.0f, 1.0f);
            mb.addBox(sMinX - 0.28f, podH, midTowerZ - 0.45f, sMinX + 0.25f, tier1Top + 0.4f, midTowerZ + 0.45f, 7.0f, 7.0f, 0.75f, 2.0f, 1.0f);
            mb.addBox(sMaxX - 0.25f, podH, midTowerZ - 0.45f, sMaxX + 0.28f, tier1Top + 0.4f, midTowerZ + 0.45f, 7.0f, 7.0f, 0.75f, 2.0f, 1.0f);

            // Horizontal structural spandrel belt ledges up the curtain-wall tower
            for (float beltY = podH + 10.2f; beltY < tier1Top - 3.0f; beltY += 10.2f)
            {
                mb.addBox(sMinX - 0.22f, beltY - 0.35f, sMinZ - 0.22f, sMaxX + 0.22f, beltY + 0.35f, sMaxZ + 0.22f, 7.0f, 7.0f, 0.78f, 3.0f, 1.0f);
            }

            // Upper Setback Tier
            const float inset2 = cityMin(0.22f * (sMaxX - sMinX), 0.22f * (sMaxZ - sMinZ));
            const float uMinX = sMinX + inset2;
            const float uMaxX = sMaxX - inset2;
            const float uMinZ = sMinZ + inset2;
            const float uMaxZ = sMaxZ - inset2;
            const float tier2Top = tier1Top + 12.0f + 16.0f * cityHash01(int(t), 53, tSeed);
            if (uMaxX - uMinX > 4.0f && uMaxZ - uMinZ > 4.0f)
            {
                mb.addBox(uMinX, tier1Top, uMinZ, uMaxX, tier2Top, uMaxZ, 6.0f, 7.0f, tint, float((tSeed + 99u) & 4095u), 3.4f);
            }

            // Crown / Mechanical Penthouse + Helipad Deck
            const float cx = 0.5f * (uMinX + uMaxX);
            const float cz = 0.5f * (uMinZ + uMaxZ);
            const float crownHW = cityMax((uMaxX - uMinX) * 0.30f, 2.2f);
            const float crownHD = cityMax((uMaxZ - uMinZ) * 0.30f, 2.2f);
            const float crownTop = tier2Top + 4.2f;
            mb.addBox(cx - crownHW, tier2Top, cz - crownHD, cx + crownHW, crownTop, cz + crownHD, 7.0f, 11.0f, 0.72f, float(tSeed & 255u), 1.0f);

            // Tapered Communication Spire or Rooftop Helipad on tall CBD towers
            float spireTop = crownTop;
            if (hRand > 0.38f)
            {
                const float spireH = 11.0f + 12.0f * hRand;
                mb.addTaperedPyramid(cx, crownTop, cz, 0.95f, 0.95f, 0.10f, 0.10f, spireH, 7.0f, 0.95f, float(tSeed & 255u));
                spireTop = crownTop + spireH;
            }
            else
            {
                mb.addBox(cx - crownHW * 0.75f, crownTop, cz - crownHD * 0.75f, cx + crownHW * 0.75f, crownTop + 0.35f, cz + crownHD * 0.75f, 11.0f, 11.0f, 0.90f, 7.0f, 1.0f);
            }

            res.towerRoofs.push_back(float4(cx, tier2Top, cz, spireTop));
            ++res.towerCount;
        }

        mb.applyCanyonOcclusion(spec.params);
        res.archVertices = mb.vertices;
        return res;
    }

    inline eastl::vector<CityPropData> generateSkyscraperFacadeAndRoofPropsCPU(
        IN CityBlockSpec spec,
        IN SkyscraperMassingResult massing)
    {
        eastl::vector<CityPropData> props;
        const uint numRoofs = uint(massing.towerRoofs.size());
        for (uint i = 0u; i < numRoofs; ++i)
        {
            const float4 roof = massing.towerRoofs[i];
            // Rooftop HVAC units
            CityPropData hvac;
            hvac.posAndYaw = float4(roof.x - 2.6f, roof.y + 0.9f, roof.z + 1.8f, 0.0f);
            hvac.sizeAndKind = float4(1.2f, 0.9f, 1.5f, 2.0f); // kind 2 = rooftop HVAC
            hvac.colorAndPulse = float4(0.42f, 0.45f, 0.49f, 0.0f);
            props.push_back(hvac);

            // Aviation warning beacon at spire tip
            CityPropData beacon;
            beacon.posAndYaw = float4(roof.x, roof.w + 0.45f, roof.z, 0.0f);
            beacon.sizeAndKind = float4(0.45f, 0.45f, 0.45f, 3.0f); // kind 3 = beacon
            beacon.colorAndPulse = float4(1.0f, 0.20f, 0.12f, 1.0f);
            props.push_back(beacon);
        }

        // Ground-level entrance neon marquee on the block
        const float cx = 0.5f * (spec.minX + spec.maxX);
        CityPropData lobbySign;
        lobbySign.posAndYaw = float4(cx, 4.4f, spec.minZ + 2.2f, 0.0f);
        lobbySign.sizeAndKind = float4(4.5f, 0.55f, 0.25f, 0.0f);
        lobbySign.colorAndPulse = float4(0.25f, 0.85f, 1.0f, 1.0f);
        props.push_back(lobbySign);

        return props;
    }

    inline eastl::vector<CityFoliageLampData> generateBlockSidewalkTreesAndLampsCPU(IN CityBlockSpec spec)
    {
        eastl::vector<CityFoliageLampData> items;
        const uint seed = spec.params.seed ^ (spec.blockId * 2654435761u);
        const float y = 0.25f;

        // Place boulevard trees along the block's outer promenade border
        const float step = 11.5f;
        for (float x = spec.minX + 3.0f; x <= spec.maxX - 3.0f; x += step)
        {
            CityFoliageLampData tS;
            tS.posAndScale = float4(x, y, spec.minZ + 1.1f, 0.85f + 0.35f * cityHash01(int(x), 1, seed));
            tS.metaAndSway = float4(cityHash01(int(x), 2, seed) * 6.28f, 0.0f, cityHash01(int(x), 3, seed), 0.0f);
            items.push_back(tS);

            CityFoliageLampData tN;
            tN.posAndScale = float4(x, y, spec.maxZ - 1.1f, 0.85f + 0.35f * cityHash01(int(x), 4, seed));
            tN.metaAndSway = float4(cityHash01(int(x), 5, seed) * 6.28f, 0.0f, cityHash01(int(x), 6, seed), 0.0f);
            items.push_back(tN);
        }
        for (float z = spec.minZ + 6.0f; z <= spec.maxZ - 6.0f; z += step)
        {
            CityFoliageLampData tW;
            tW.posAndScale = float4(spec.minX + 1.1f, y, z, 0.85f + 0.35f * cityHash01(7, int(z), seed));
            tW.metaAndSway = float4(cityHash01(8, int(z), seed) * 6.28f, 0.0f, cityHash01(9, int(z), seed), 0.0f);
            items.push_back(tW);

            CityFoliageLampData tE;
            tE.posAndScale = float4(spec.maxX - 1.1f, y, z, 0.85f + 0.35f * cityHash01(10, int(z), seed));
            tE.metaAndSway = float4(cityHash01(11, int(z), seed) * 6.28f, 0.0f, cityHash01(12, int(z), seed), 0.0f);
            items.push_back(tE);
        }
        return items;
    }

    inline CityGeometryPack packSkyscraperBlockGeometry(
        IN CityBlockSpec spec,
        IN SkyscraperMassingResult massing,
        IN eastl::vector<CityPropData> props,
        IN eastl::vector<CityFoliageLampData> streetFurniture)
    {
        (void)spec;
        CityGeometryPack pack;
        pack.archVertices = massing.archVertices;
        pack.propsAndSignage = props;
        pack.foliageAndLamps = streetFurniture;
        pack.cbdTowerCount = massing.towerCount;
        const uint furnCount = uint(streetFurniture.size());
        for (uint i = 0u; i < furnCount; ++i)
        {
            if (streetFurniture[i].metaAndSway.y < 0.5f)
            {
                ++pack.treeCount;
            }
            else
            {
                ++pack.lampCount;
            }
        }
        pack.nestedSubgraphCount = 1u;
        return pack;
    }

    // =========================================================================
    // 3. Commercial Mid-Rise, Courtyard Residential & Park Subgraph CPU Tasks
    // =========================================================================

    inline eastl::vector<UrbanSubLot> splitUrbanBlockIntoLotsCPU(IN CityBlockSpec spec)
    {
        eastl::vector<UrbanSubLot> lots;
        const uint bSeed = spec.params.seed ^ (spec.blockId * 0x45d9f3bu + 911u);
        const float bw = spec.maxX - spec.minX;
        const float bd = spec.maxZ - spec.minZ;

        if (spec.districtKind == 3u)
        {
            // Central Park or Waterfront Civic Park
            UrbanSubLot parkLot;
            parkLot.minX = spec.minX;
            parkLot.maxX = spec.maxX;
            parkLot.minZ = spec.minZ;
            parkLot.maxZ = spec.maxZ;
            parkLot.height = 0.25f;
            parkLot.seed01 = cityHash01(int(spec.blockId), 7, bSeed);
            parkLot.lotKind = (spec.densityFactor > 0.95f && bw > 28.0f) ? 3u : 2u; // 3 = central fountain plaza
            lots.push_back(parkLot);
            return lots;
        }

        if (spec.districtKind == 1u)
        {
            // Commercial Mid-Rise: 2x2 lot split with a pedestrian service cross-alley
            const float midX = 0.5f * (spec.minX + spec.maxX);
            const float midZ = 0.5f * (spec.minZ + spec.maxZ);
            const float gap = 2.0f;
            const float margin = 2.2f;
            for (uint rz = 0u; rz < 2u; ++rz)
            {
                for (uint rx = 0u; rx < 2u; ++rx)
                {
                    const float x0 = (rx == 0u) ? (spec.minX + margin) : (midX + gap);
                    const float x1 = (rx == 0u) ? (midX - gap) : (spec.maxX - margin);
                    const float z0 = (rz == 0u) ? (spec.minZ + margin) : (midZ + gap);
                    const float z1 = (rz == 0u) ? (midZ - gap) : (spec.maxZ - margin);
                    if (x1 - x0 < 5.0f || z1 - z0 < 5.0f)
                    {
                        continue;
                    }
                    const float s01 = cityHash01(int(rx) + int(spec.blockId) * 3, int(rz) + 19, bSeed);
                    UrbanSubLot lot;
                    lot.minX = x0;
                    lot.maxX = x1;
                    lot.minZ = z0;
                    lot.maxZ = z1;
                    lot.height = (12.0f + 19.0f * s01) * spec.densityFactor;
                    lot.seed01 = s01;
                    lot.lotKind = 0u;
                    lots.push_back(lot);
                }
            }
            return lots;
        }

        // Courtyard Residential (districtKind == 2u): perimeter townhouses around a green inner courtyard.
        const float depthX = cityClamp(bw * 0.24f, 6.5f, 10.5f);
        const float depthZ = cityClamp(bd * 0.24f, 6.5f, 10.5f);
        const float margin = 1.8f;

        // South & North townhouse rows (2 houses each)
        const float midX = 0.5f * (spec.minX + spec.maxX);
        for (uint sideZ = 0u; sideZ < 2u; ++sideZ)
        {
            const float z0 = (sideZ == 0u) ? (spec.minZ + margin) : (spec.maxZ - margin - depthZ);
            const float z1 = (sideZ == 0u) ? (spec.minZ + margin + depthZ) : (spec.maxZ - margin);
            for (uint col = 0u; col < 2u; ++col)
            {
                const float x0 = (col == 0u) ? (spec.minX + margin) : (midX + 0.8f);
                const float x1 = (col == 0u) ? (midX - 0.8f) : (spec.maxX - margin);
                const float s01 = cityHash01(int(col) + int(sideZ) * 5, int(spec.blockId), bSeed + 31u);
                UrbanSubLot lot;
                lot.minX = x0;
                lot.maxX = x1;
                lot.minZ = z0;
                lot.maxZ = z1;
                lot.height = 6.8f + 4.4f * s01;
                lot.seed01 = s01;
                lot.lotKind = 1u;
                lots.push_back(lot);
            }
        }
        // West & East infill townhouses flanking the inner courtyard
        const float innerZ0 = spec.minZ + margin + depthZ + 1.4f;
        const float innerZ1 = spec.maxZ - margin - depthZ - 1.4f;
        if (innerZ1 - innerZ0 > 6.0f)
        {
            for (uint sideX = 0u; sideX < 2u; ++sideX)
            {
                const float x0 = (sideX == 0u) ? (spec.minX + margin) : (spec.maxX - margin - depthX);
                const float x1 = (sideX == 0u) ? (spec.minX + margin + depthX) : (spec.maxX - margin);
                const float s01 = cityHash01(int(sideX) + 17, int(spec.blockId), bSeed + 73u);
                UrbanSubLot lot;
                lot.minX = x0;
                lot.maxX = x1;
                lot.minZ = innerZ0;
                lot.maxZ = innerZ1;
                lot.height = 6.5f + 4.0f * s01;
                lot.seed01 = s01;
                lot.lotKind = 1u;
                lots.push_back(lot);
            }
        }
        return lots;
    }

    struct MidriseResMassingResult
    {
        eastl::vector<CityArchVertex> archVertices;
        uint commercialCount = 0u;
        uint residentialCount = 0u;
        uint parkCount = 0u;
    };

    inline MidriseResMassingResult generateMidriseAndTownhouseMassingCPU(
        IN CityBlockSpec spec,
        IN eastl::vector<UrbanSubLot> lots)
    {
        MidriseResMassingResult res;
        CityMeshBuilder mb;

        if (spec.districtKind == 3u)
        {
            // Park block: lush green terraced lawn + stone walkways + optional central fountain monument
            mb.addBox(spec.minX, 0.0f, spec.minZ, spec.maxX, 0.22f, spec.maxZ, 3.0f, 0.0f, 0.65f, float(spec.blockId), 1.0f);
            const float cx = 0.5f * (spec.minX + spec.maxX);
            const float cz = 0.5f * (spec.minZ + spec.maxZ);
            // Cross park footpaths
            mb.addFlatQuadXZ(spec.minX, spec.maxX, cz - 1.6f, cz + 1.6f, 0.24f, 3.0f, 0.72f, 1.0f);
            mb.addFlatQuadXZ(cx - 1.6f, cx + 1.6f, spec.minZ, spec.maxZ, 0.24f, 3.0f, 0.72f, 1.0f);

            const uint lotCount = uint(lots.size());
            if (lotCount > 0u && lots[0].lotKind == 3u)
            {
                // Grand Civic Fountain Plaza & Monument Obelisk
                mb.addBox(cx - 10.5f, 0.22f, cz - 10.5f, cx + 10.5f, 0.36f, cz + 10.5f, 11.0f, 11.0f, 0.85f, 1.0f, 1.0f);
                mb.addBox(cx - 6.8f, 0.36f, cz - 6.8f, cx + 6.8f, 0.85f, cz + 6.8f, 11.0f, 4.0f, 0.90f, 2.0f, 1.0f);
                mb.addBox(cx - 2.2f, 0.85f, cz - 2.2f, cx + 2.2f, 3.2f, cz + 2.2f, 11.0f, 11.0f, 0.95f, 3.0f, 1.0f);
                mb.addTaperedPyramid(cx, 3.2f, cz, 1.4f, 1.4f, 0.25f, 0.25f, 9.5f, 11.0f, 1.0f, 4.0f);
            }
            // Corner park planters / stone plinths
            mb.addBox(cx - 5.2f, 0.22f, cz - 5.2f, cx - 3.6f, 0.72f, cz - 3.6f, 5.0f, 0.0f, 0.75f, 1.0f, 1.0f);
            mb.addBox(cx + 3.6f, 0.22f, cz + 3.6f, cx + 5.2f, 0.72f, cz + 5.2f, 5.0f, 0.0f, 0.75f, 1.0f, 1.0f);
            res.parkCount = 1u;
            mb.applyCanyonOcclusion(spec.params);
            res.archVertices = mb.vertices;
            return res;
        }

        // Base pavement or green courtyard lawn for Commercial / Residential blocks
        if (spec.districtKind == 1u)
        {
            mb.addBox(spec.minX, 0.0f, spec.minZ, spec.maxX, 0.22f, spec.maxZ, 3.0f, 3.0f, 0.58f, float(spec.blockId), 1.0f);
        }
        else
        {
            // Residential block has concrete border + lush green inner courtyard garden
            mb.addBox(spec.minX, 0.0f, spec.minZ, spec.maxX, 0.22f, spec.maxZ, 3.0f, 3.0f, 0.62f, float(spec.blockId), 1.0f);
            const float pad = 9.5f;
            if (spec.maxX - spec.minX > pad * 2.0f && spec.maxZ - spec.minZ > pad * 2.0f)
            {
                mb.addFlatQuadXZ(spec.minX + pad, spec.maxX - pad, spec.minZ + pad, spec.maxZ - pad, 0.24f, 0.0f, 0.82f, float(spec.blockId));
            }
        }

        const uint lotCount = uint(lots.size());
        for (uint i = 0u; i < lotCount; ++i)
        {
            const UrbanSubLot lot = lots[i];
            const float lotSeed = float((spec.blockId * 13u + i * 7u) & 2047u);
            if (lot.lotKind == 0u)
            {
                // Commercial Mid-Rise: ground retail base + main upper tower + stepped cornice & parapet
                const float retailH = 3.8f;
                mb.addBox(lot.minX, 0.22f, lot.minZ, lot.maxX, retailH, lot.maxZ, 7.0f, 7.0f, 0.42f + 0.35f * lot.seed01, lotSeed, 3.2f);
                mb.addBox(lot.minX + 0.4f, retailH, lot.minZ + 0.4f, lot.maxX - 0.4f, lot.height, lot.maxZ - 0.4f, 8.0f, 3.0f, lot.seed01, lotSeed + 5.0f, 3.2f);
                mb.addBox(lot.minX + 0.2f, lot.height, lot.minZ + 0.2f, lot.maxX - 0.2f, lot.height + 0.85f, lot.maxZ - 0.2f, 7.0f, 3.0f, 0.68f, lotSeed, 1.0f);
                if (lot.height > 20.0f)
                {
                    const float midBeltY = 0.5f * (retailH + lot.height);
                    mb.addBox(lot.minX + 0.25f, midBeltY - 0.25f, lot.minZ + 0.25f, lot.maxX - 0.25f, midBeltY + 0.25f, lot.maxZ - 0.25f, 7.0f, 7.0f, 0.65f, lotSeed, 1.0f);
                }
                ++res.commercialCount;
            }
            else
            {
                // Courtyard Residential Townhouse: warm brick walls, pitched roof, and brick chimney.
                mb.addBox(lot.minX, 0.22f, lot.minZ, lot.maxX, lot.height, lot.maxZ, 9.0f, 9.0f, lot.seed01, lotSeed, 2.9f);
                const float rcx = 0.5f * (lot.minX + lot.maxX);
                const float rcz = 0.5f * (lot.minZ + lot.maxZ);
                const float rhw = 0.5f * (lot.maxX - lot.minX) + 0.35f;
                const float rhd = 0.5f * (lot.maxZ - lot.minZ) + 0.35f;
                const float ridgeW = rhw > rhd ? (rhw - rhd * 0.45f) : 0.25f;
                const float ridgeD = rhd > rhw ? (rhd - rhw * 0.45f) : 0.25f;
                const float roofH = 2.8f + 1.2f * lot.seed01;
                mb.addTaperedPyramid(rcx, lot.height, rcz, rhw, rhd, ridgeW, ridgeD, roofH, 10.0f, lot.seed01, lotSeed);
                mb.addBox(rcx + rhw * 0.28f - 0.45f, lot.height + 0.5f, rcz - 0.45f, rcx + rhw * 0.28f + 0.45f, lot.height + roofH + 0.85f, rcz + 0.45f, 9.0f, 7.0f, 0.45f, lotSeed, 1.0f);
                ++res.residentialCount;
            }
        }

        mb.applyCanyonOcclusion(spec.params);
        res.archVertices = mb.vertices;
        return res;
    }

    inline eastl::vector<CityPropData> generateAwningsSignageAndRoofPropsCPU(
        IN CityBlockSpec spec,
        IN eastl::vector<UrbanSubLot> lots)
    {
        eastl::vector<CityPropData> props;
        const uint lotCount = uint(lots.size());
        for (uint i = 0u; i < lotCount; ++i)
        {
            const UrbanSubLot lot = lots[i];
            if (lot.lotKind == 0u)
            {
                const float cx = 0.5f * (lot.minX + lot.maxX);
                const float cz = 0.5f * (lot.minZ + lot.maxZ);
                const float hw = 0.5f * (lot.maxX - lot.minX);
                const float hd = 0.5f * (lot.maxZ - lot.minZ);

                // Ground-floor commercial awning
                CityPropData awning;
                awning.posAndYaw = float4(cx, 3.35f, lot.minZ - 0.55f, 0.0f);
                awning.sizeAndKind = float4(hw * 0.72f, 0.22f, 0.75f, 1.0f);
                awning.colorAndPulse = float4(
                    0.55f + 0.35f * lot.seed01,
                    0.18f + 0.30f * (1.0f - lot.seed01),
                    0.22f,
                    0.0f);
                props.push_back(awning);

                // Vibrant Neon Signage Panel mounted on commercial facade
                const uint neonPick = (spec.blockId + i) % 4u;
                float3 neonRgb = float3(0.15f, 0.92f, 1.0f); // cyan
                if (neonPick == 1u) { neonRgb = float3(1.0f, 0.22f, 0.72f); } // magenta
                else if (neonPick == 2u) { neonRgb = float3(1.0f, 0.74f, 0.16f); } // amber
                else if (neonPick == 3u) { neonRgb = float3(0.32f, 1.0f, 0.52f); } // mint neon

                CityPropData sign;
                sign.posAndYaw = float4(cx, 5.4f + 2.0f * lot.seed01, lot.minZ - 0.15f, float(i));
                sign.sizeAndKind = float4(cityMin(hw * 0.58f, 3.6f), 0.72f, 0.18f, 0.0f);
                sign.colorAndPulse = float4(neonRgb.x, neonRgb.y, neonRgb.z, 1.0f);
                props.push_back(sign);

                // Side neon billboard
                CityPropData sideSign;
                sideSign.posAndYaw = float4(lot.maxX + 0.15f, 6.2f, cz, float(i + 3u));
                sideSign.sizeAndKind = float4(0.18f, 0.95f, cityMin(hd * 0.45f, 2.8f), 0.0f);
                sideSign.colorAndPulse = float4(neonRgb.z, neonRgb.x, neonRgb.y, 1.0f);
                props.push_back(sideSign);

                // Rooftop HVAC / Water tank
                CityPropData hvac;
                hvac.posAndYaw = float4(cx - 1.2f, lot.height + 1.55f, cz + 0.8f, 0.0f);
                hvac.sizeAndKind = float4(1.1f, 0.75f, 1.2f, 2.0f);
                hvac.colorAndPulse = float4(0.45f, 0.48f, 0.52f, 0.0f);
                props.push_back(hvac);
            }
        }
        return props;
    }

    inline eastl::vector<CityFoliageLampData> generateCourtyardParkTreesAndLampsCPU(
        IN CityBlockSpec spec,
        IN eastl::vector<UrbanSubLot> lots)
    {
        (void)lots;
        eastl::vector<CityFoliageLampData> items;
        const uint seed = spec.params.seed ^ (spec.blockId * 0x85ebca6bu + 313u);
        const float bw = spec.maxX - spec.minX;
        const float bd = spec.maxZ - spec.minZ;

        if (spec.districtKind == 3u)
        {
            // Park and waterfront promenade groves.
            const float cell = 6.8f;
            const uint nx = uint(bw / cell);
            const uint nz = uint(bd / cell);
            const float cx = 0.5f * (spec.minX + spec.maxX);
            const float cz = 0.5f * (spec.minZ + spec.maxZ);

            for (uint iz = 0u; iz < nz; ++iz)
            {
                for (uint ix = 0u; ix < nx; ++ix)
                {
                    const float tx = spec.minX + (float(ix) + 0.25f + 0.5f * cityHash01(int(ix), int(iz), seed + 1u)) * cell;
                    const float tz = spec.minZ + (float(iz) + 0.25f + 0.5f * cityHash01(int(ix), int(iz), seed + 2u)) * cell;
                    // Keep park footpaths and central fountain clear
                    if (cityAbs(tx - cx) < 2.6f || cityAbs(tz - cz) < 2.6f)
                    {
                        continue;
                    }
                    if (spec.densityFactor > 0.95f && cityAbs(tx - cx) < 11.5f && cityAbs(tz - cz) < 11.5f)
                    {
                        continue;
                    }
                    if (cityHash01(int(ix) + 9, int(iz) + 17, seed + 3u) > 0.72f)
                    {
                        continue;
                    }
                    CityFoliageLampData tree;
                    tree.posAndScale = float4(tx, 0.22f, tz, 0.85f + 0.65f * cityHash01(int(ix), int(iz), seed + 4u));
                    tree.metaAndSway = float4(cityHash01(int(ix), int(iz), seed + 5u) * 6.28f, 0.0f, cityHash01(int(ix), int(iz), seed + 6u), 0.0f);
                    items.push_back(tree);
                }
            }
            // Park pathway lamps
            for (uint k = 0u; k < 4u; ++k)
            {
                const float ox = (k < 2u) ? -4.5f : 4.5f;
                const float oz = (k % 2u == 0u) ? -4.5f : 4.5f;
                CityFoliageLampData lamp;
                lamp.posAndScale = float4(cx + ox, 0.22f, cz + oz, 1.0f);
                lamp.metaAndSway = float4(0.0f, 1.0f, 1.0f, 0.0f);
                items.push_back(lamp);
            }
            return items;
        }

        if (spec.districtKind == 2u)
        {
            // Residential Courtyard Garden Trees inside the center square
            const float cx = 0.5f * (spec.minX + spec.maxX);
            const float cz = 0.5f * (spec.minZ + spec.maxZ);
            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (dx == 0 && dz == 0)
                    {
                        CityFoliageLampData lamp;
                        lamp.posAndScale = float4(cx, 0.24f, cz, 0.85f);
                        lamp.metaAndSway = float4(0.0f, 1.0f, 1.0f, 0.0f);
                        items.push_back(lamp);
                        continue;
                    }
                    const float jx = (cityHash01(dx + 5, dz + 5, seed) - 0.5f) * 2.2f;
                    const float jz = (cityHash01(dx + 9, dz + 11, seed) - 0.5f) * 2.2f;
                    CityFoliageLampData tree;
                    tree.posAndScale = float4(cx + float(dx) * 4.5f + jx, 0.24f, cz + float(dz) * 4.5f + jz, 0.78f + 0.32f * cityHash01(dx, dz, seed + 7u));
                    tree.metaAndSway = float4(cityHash01(dx, dz, seed + 8u) * 6.28f, 0.0f, cityHash01(dx, dz, seed + 9u), 0.0f);
                    items.push_back(tree);
                }
            }
        }
        else
        {
            // Commercial district sidewalk trees along south/north block edge
            for (float x = spec.minX + 4.5f; x <= spec.maxX - 4.5f; x += 14.0f)
            {
                CityFoliageLampData tree;
                tree.posAndScale = float4(x, 0.22f, spec.minZ + 0.9f, 0.80f + 0.25f * cityHash01(int(x), 1, seed));
                tree.metaAndSway = float4(cityHash01(int(x), 2, seed) * 6.28f, 0.0f, cityHash01(int(x), 3, seed), 0.0f);
                items.push_back(tree);
            }
        }
        return items;
    }

    inline CityGeometryPack packMidriseResBlockGeometry(
        IN CityBlockSpec spec,
        IN eastl::vector<UrbanSubLot> lots,
        IN MidriseResMassingResult buildings,
        IN eastl::vector<CityPropData> props,
        IN eastl::vector<CityFoliageLampData> foliageAndLamps)
    {
        (void)spec;
        (void)lots;
        CityGeometryPack pack;
        pack.archVertices = buildings.archVertices;
        pack.propsAndSignage = props;
        pack.foliageAndLamps = foliageAndLamps;
        pack.commercialBuildingCount = buildings.commercialCount;
        pack.residentialBuildingCount = buildings.residentialCount;
        pack.parkCount = buildings.parkCount;
        const uint count = uint(foliageAndLamps.size());
        for (uint i = 0u; i < count; ++i)
        {
            if (foliageAndLamps[i].metaAndSway.y < 0.5f)
            {
                ++pack.treeCount;
            }
            else
            {
                ++pack.lampCount;
            }
        }
        pack.nestedSubgraphCount = 1u;
        return pack;
    }

    // =========================================================================
    // 4. Roadways, Canal & Bridges Subgraph CPU Tasks
    // =========================================================================

    struct CorridorMeshResult
    {
        eastl::vector<CityArchVertex> archVertices;
        uint bridgeCount = 0u;
    };

    inline CorridorMeshResult generateRoadCanalBridgeMeshCPU(IN CityCorridorSpec spec)
    {
        CorridorMeshResult res;
        CityMeshBuilder mb;

        if (spec.corridorKind == 2u)
        {
            // Winding Metropolitan River / Canal + Stone Embankments + Waterfront Promenades
            const uint segs = 14u;
            const float zStep = (spec.maxZ - spec.minZ) / float(segs);
            const float canalHalfW = 9.8f;
            const float embW = 2.0f;
            const float promW = 3.0f;

            for (uint s = 0u; s < segs; ++s)
            {
                const float z0 = spec.minZ + float(s) * zStep;
                const float z1 = z0 + zStep;
                const float cx0 = cityCanalCenterX(z0, spec.params.canalAmplitude, spec.params.canalFrequency, spec.params.seed);
                const float cx1 = cityCanalCenterX(z1, spec.params.canalAmplitude, spec.params.canalFrequency, spec.params.seed);

                // Water ribbon (material 4)
                mb.addQuad(
                    float3(cx0 - canalHalfW, -1.65f, z0),
                    float3(cx0 + canalHalfW, -1.65f, z0),
                    float3(cx1 + canalHalfW, -1.65f, z1),
                    float3(cx1 - canalHalfW, -1.65f, z1),
                    float3(0.0f, 1.0f, 0.0f), 4.0f, 0.5f,
                    float2(0.0f, z0), float2(canalHalfW * 2.0f, z0),
                    float2(canalHalfW * 2.0f, z1), float2(0.0f, z1),
                    float(spec.corridorId), 1.0f);

                // West embankment wall + promenade (material 5 + 3)
                mb.addQuad(
                    float3(cx0 - canalHalfW - embW, 0.24f, z0),
                    float3(cx0 - canalHalfW, -2.1f, z0),
                    float3(cx1 - canalHalfW, -2.1f, z1),
                    float3(cx1 - canalHalfW - embW, 0.24f, z1),
                    float3(0.85f, 0.52f, 0.0f), 5.0f, 0.72f,
                    float2(0.0f, z0), float2(2.0f, z0), float2(2.0f, z1), float2(0.0f, z1),
                    1.0f, 1.0f);
                mb.addQuad(
                    float3(cx0 - canalHalfW - embW - promW, 0.24f, z0),
                    float3(cx0 - canalHalfW - embW, 0.24f, z0),
                    float3(cx1 - canalHalfW - embW, 0.24f, z1),
                    float3(cx1 - canalHalfW - embW - promW, 0.24f, z1),
                    float3(0.0f, 1.0f, 0.0f), 3.0f, 0.78f,
                    float2(0.0f, z0), float2(promW, z0), float2(promW, z1), float2(0.0f, z1),
                    1.0f, 1.0f);

                // East embankment wall + promenade (material 5 + 3)
                mb.addQuad(
                    float3(cx0 + canalHalfW, -2.1f, z0),
                    float3(cx0 + canalHalfW + embW, 0.24f, z0),
                    float3(cx1 + canalHalfW + embW, 0.24f, z1),
                    float3(cx1 + canalHalfW, -2.1f, z1),
                    float3(-0.85f, 0.52f, 0.0f), 5.0f, 0.72f,
                    float2(0.0f, z0), float2(2.0f, z0), float2(2.0f, z1), float2(0.0f, z1),
                    1.0f, 1.0f);
                mb.addQuad(
                    float3(cx0 + canalHalfW + embW, 0.24f, z0),
                    float3(cx0 + canalHalfW + embW + promW, 0.24f, z0),
                    float3(cx1 + canalHalfW + embW + promW, 0.24f, z1),
                    float3(cx1 + canalHalfW + embW, 0.24f, z1),
                    float3(0.0f, 1.0f, 0.0f), 3.0f, 0.78f,
                    float2(0.0f, z0), float2(promW, z0), float2(promW, z1), float2(0.0f, z1),
                    1.0f, 1.0f);

                // Stone embankment bollards along both waterfront promenades
                if ((s & 1u) == 0u)
                {
                    mb.addBox(cx0 - canalHalfW - embW - 0.35f, 0.24f, z0 - 0.25f, cx0 - canalHalfW - embW + 0.15f, 0.92f, z0 + 0.25f, 5.0f, 5.0f, 0.80f, 1.0f, 1.0f);
                    mb.addBox(cx0 + canalHalfW + embW - 0.15f, 0.24f, z0 - 0.25f, cx0 + canalHalfW + embW + 0.35f, 0.92f, z0 + 0.25f, 5.0f, 5.0f, 0.80f, 1.0f, 1.0f);
                }
            }
            mb.applyCanyonOcclusion(spec.params);
            res.archVertices = mb.vertices;
            return res;
        }

        if (spec.corridorKind == 3u)
        {
            // Multi-Span Arched Bridge over Canal + Approach Boulevards
            const float bMinX = spec.bridgeCenterX - spec.bridgeHalfSpan;
            const float bMaxX = spec.bridgeCenterX + spec.bridgeHalfSpan;
            const float roadInset = (spec.laneCount >= 4u) ? 2.2f : 2.0f;
            const float rMinZ = spec.minZ + roadInset;
            const float rMaxZ = spec.maxZ - roadInset;
            const float cz = 0.5f * (spec.minZ + spec.maxZ);

            // West approach & East approach flat roadways
            if (bMinX > spec.minX)
            {
                mb.addFlatQuadXZ(spec.minX, bMinX, spec.minZ, spec.maxZ, 0.14f, 3.0f, 0.62f, 1.0f);
                mb.addFlatQuadXZ(spec.minX, bMinX, rMinZ, rMaxZ, 0.04f, 1.0f, 0.45f, 1.0f);
                mb.addFlatQuadXZ(spec.minX + 1.0f, bMinX - 1.0f, cz - 0.18f, cz + 0.18f, 0.06f, 2.0f, 0.90f, 1.0f);
            }
            if (spec.maxX > bMaxX)
            {
                mb.addFlatQuadXZ(bMaxX, spec.maxX, spec.minZ, spec.maxZ, 0.14f, 3.0f, 0.62f, 1.0f);
                mb.addFlatQuadXZ(bMaxX, spec.maxX, rMinZ, rMaxZ, 0.04f, 1.0f, 0.45f, 1.0f);
                mb.addFlatQuadXZ(bMaxX + 1.0f, spec.maxX - 1.0f, cz - 0.18f, cz + 0.18f, 0.06f, 2.0f, 0.90f, 1.0f);
            }

            // Arched Bridge Deck (14 segments across the canal + stone balustrade pillars)
            const uint segs = 14u;
            const float stepX = (bMaxX - bMinX) / float(segs);
            for (uint s = 0u; s < segs; ++s)
            {
                const float x0 = bMinX + float(s) * stepX;
                const float x1 = x0 + stepX;
                const float y0 = cityBridgeDeckHeight(x0, spec.bridgeCenterX, spec.bridgeHalfSpan);
                const float y1 = cityBridgeDeckHeight(x1, spec.bridgeCenterX, spec.bridgeHalfSpan);

                // Bridge asphalt deck
                mb.addQuad(
                    float3(x0, y0, rMinZ), float3(x1, y1, rMinZ),
                    float3(x1, y1, rMaxZ), float3(x0, y0, rMaxZ),
                    float3(0.0f, 1.0f, 0.0f), 1.0f, 0.50f,
                    float2(x0, 0.0f), float2(x1, 0.0f), float2(x1, rMaxZ - rMinZ), float2(x0, rMaxZ - rMinZ),
                    1.0f, 1.0f);
                // Bridge lane centerline
                mb.addQuad(
                    float3(x0, y0 + 0.03f, cz - 0.16f), float3(x1, y1 + 0.03f, cz - 0.16f),
                    float3(x1, y1 + 0.03f, cz + 0.16f), float3(x0, y0 + 0.03f, cz + 0.16f),
                    float3(0.0f, 1.0f, 0.0f), 2.0f, 0.95f,
                    float2(x0, 0.0f), float2(x1, 0.0f), float2(x1, 0.32f), float2(x0, 0.32f),
                    1.0f, 1.0f);
                // Bridge raised concrete deck sidewalks & outer stone parapets (south & north)
                mb.addQuad(
                    float3(x0, y0 + 0.16f, spec.minZ), float3(x1, y1 + 0.16f, spec.minZ),
                    float3(x1, y1 + 0.16f, rMinZ), float3(x0, y0 + 0.16f, rMinZ),
                    float3(0.0f, 1.0f, 0.0f), 3.0f, 0.72f,
                    float2(x0, 0.0f), float2(x1, 0.0f), float2(x1, 2.0f), float2(x0, 2.0f),
                    1.0f, 1.0f);
                mb.addQuad(
                    float3(x0, y0 + 0.16f, rMaxZ), float3(x1, y1 + 0.16f, rMaxZ),
                    float3(x1, y1 + 0.16f, spec.maxZ), float3(x0, y0 + 0.16f, spec.maxZ),
                    float3(0.0f, 1.0f, 0.0f), 3.0f, 0.72f,
                    float2(x0, 0.0f), float2(x1, 0.0f), float2(x1, 2.0f), float2(x0, 2.0f),
                    1.0f, 1.0f);
                // Parapet walls (material 5)
                mb.addQuad(
                    float3(x0, y0 - 0.45f, spec.minZ), float3(x1, y1 - 0.45f, spec.minZ),
                    float3(x1, y1 + 0.85f, spec.minZ), float3(x0, y0 + 0.85f, spec.minZ),
                    float3(0.0f, 0.0f, -1.0f), 5.0f, 0.85f,
                    float2(x0, 0.0f), float2(x1, 0.0f), float2(x1, 1.3f), float2(x0, 1.3f),
                    1.0f, 1.0f);
                mb.addQuad(
                    float3(x1, y1 - 0.45f, spec.maxZ), float3(x0, y0 - 0.45f, spec.maxZ),
                    float3(x0, y0 + 0.85f, spec.maxZ), float3(x1, y1 + 0.85f, spec.maxZ),
                    float3(0.0f, 0.0f, 1.0f), 5.0f, 0.85f,
                    float2(x0, 0.0f), float2(x1, 0.0f), float2(x1, 1.3f), float2(x0, 1.3f),
                    1.0f, 1.0f);
                if ((s & 1u) == 0u)
                {
                    mb.addBox(x0 - 0.25f, y0 + 0.16f, spec.minZ - 0.15f, x0 + 0.25f, y0 + 1.25f, spec.minZ + 0.35f, 5.0f, 5.0f, 0.88f, 1.0f, 1.0f);
                    mb.addBox(x0 - 0.25f, y0 + 0.16f, spec.maxZ - 0.35f, x0 + 0.25f, y0 + 1.25f, spec.maxZ + 0.15f, 5.0f, 5.0f, 0.88f, 1.0f, 1.0f);
                }
            }

            // Stone Bridge Piers in the canal
            mb.addBox(spec.bridgeCenterX - 8.5f, -2.5f, spec.minZ + 0.6f, spec.bridgeCenterX - 6.5f, 2.3f, spec.maxZ - 0.6f, 5.0f, 5.0f, 0.72f, 1.0f, 1.0f);
            mb.addBox(spec.bridgeCenterX + 6.5f, -2.5f, spec.minZ + 0.6f, spec.bridgeCenterX + 8.5f, 2.3f, spec.maxZ - 0.6f, 5.0f, 5.0f, 0.72f, 1.0f, 1.0f);

            res.bridgeCount = 1u;
            mb.applyCanyonOcclusion(spec.params);
            res.archVertices = mb.vertices;
            return res;
        }

        // Standard Roadway Corridor (0 = East-West, 1 = North-South)
        const float roadInset = (spec.laneCount >= 4u) ? 2.2f : 2.0f;
        if (spec.corridorKind == 0u)
        {
            const float rMinZ = spec.minZ + roadInset;
            const float rMaxZ = spec.maxZ - roadInset;
            const float cz = 0.5f * (spec.minZ + spec.maxZ);
            // Sidewalk base strips
            mb.addFlatQuadXZ(spec.minX, spec.maxX, spec.minZ, rMinZ, 0.14f, 3.0f, 0.62f, 1.0f);
            mb.addFlatQuadXZ(spec.minX, spec.maxX, rMaxZ, spec.maxZ, 0.14f, 3.0f, 0.62f, 1.0f);
            // Asphalt roadway
            mb.addFlatQuadXZ(spec.minX, spec.maxX, rMinZ, rMaxZ, 0.02f, 1.0f, 0.45f, 1.0f);
            // Median / center double yellow line
            mb.addFlatQuadXZ(spec.minX + 8.0f, spec.maxX - 8.0f, cz - 0.22f, cz + 0.22f, 0.04f, 2.0f, 0.90f, 1.0f);
            // Dashed lane lines for 4-lane arterial boulevards
            if (spec.laneCount >= 4u)
            {
                const float laneOff = 3.2f;
                for (float x = spec.minX + 10.0f; x < spec.maxX - 11.0f; x += 6.0f)
                {
                    mb.addFlatQuadXZ(x, x + 2.6f, cz - laneOff - 0.10f, cz - laneOff + 0.10f, 0.04f, 2.0f, 1.0f, 1.0f);
                    mb.addFlatQuadXZ(x, x + 2.6f, cz + laneOff - 0.10f, cz + laneOff + 0.10f, 0.04f, 2.0f, 1.0f, 1.0f);
                }
            }
            // Zebra crosswalk stripes at both intersections
            for (float s = rMinZ + 0.8f; s < rMaxZ - 0.8f; s += 1.6f)
            {
                mb.addFlatQuadXZ(spec.minX + 7.5f, spec.minX + 10.2f, s, s + 0.7f, 0.045f, 2.0f, 1.0f, 1.0f);
                mb.addFlatQuadXZ(spec.maxX - 10.2f, spec.maxX - 7.5f, s, s + 0.7f, 0.045f, 2.0f, 1.0f, 1.0f);
            }
        }
        else
        {
            const float rMinX = spec.minX + roadInset;
            const float rMaxX = spec.maxX - roadInset;
            const float cx = 0.5f * (spec.minX + spec.maxX);
            // Sidewalk base strips
            mb.addFlatQuadXZ(spec.minX, rMinX, spec.minZ, spec.maxZ, 0.14f, 3.0f, 0.62f, 1.0f);
            mb.addFlatQuadXZ(rMaxX, spec.maxX, spec.minZ, spec.maxZ, 0.14f, 3.0f, 0.62f, 1.0f);
            // Asphalt roadway
            mb.addFlatQuadXZ(rMinX, rMaxX, spec.minZ, spec.maxZ, 0.02f, 1.0f, 0.45f, 1.0f);
            // Centerline
            mb.addFlatQuadXZ(cx - 0.22f, cx + 0.22f, spec.minZ + 8.0f, spec.maxZ - 8.0f, 0.04f, 2.0f, 0.90f, 1.0f);
            if (spec.laneCount >= 4u)
            {
                const float laneOff = 3.2f;
                for (float z = spec.minZ + 10.0f; z < spec.maxZ - 11.0f; z += 6.0f)
                {
                    mb.addFlatQuadXZ(cx - laneOff - 0.10f, cx - laneOff + 0.10f, z, z + 2.6f, 0.04f, 2.0f, 1.0f, 1.0f);
                    mb.addFlatQuadXZ(cx + laneOff - 0.10f, cx + laneOff + 0.10f, z, z + 2.6f, 0.04f, 2.0f, 1.0f, 1.0f);
                }
            }
            // Zebra crosswalk stripes
            for (float s = rMinX + 0.8f; s < rMaxX - 0.8f; s += 1.6f)
            {
                mb.addFlatQuadXZ(s, s + 0.7f, spec.minZ + 7.5f, spec.minZ + 10.2f, 0.045f, 2.0f, 1.0f, 1.0f);
                mb.addFlatQuadXZ(s, s + 0.7f, spec.maxZ - 10.2f, spec.maxZ - 7.5f, 0.045f, 2.0f, 1.0f, 1.0f);
            }
        }

        mb.applyCanyonOcclusion(spec.params);
        res.archVertices = mb.vertices;
        return res;
    }

    inline eastl::vector<CityFoliageLampData> generateCorridorStreetlampsCPU(IN CityCorridorSpec spec)
    {
        eastl::vector<CityFoliageLampData> lamps;
        if (spec.corridorKind == 2u)
        {
            // Waterfront promenade lamps along the canal banks
            for (float z = spec.minZ + 6.0f; z <= spec.maxZ - 6.0f; z += 14.0f)
            {
                const float cx = cityCanalCenterX(z, spec.params.canalAmplitude, spec.params.canalFrequency, spec.params.seed);
                CityFoliageLampData lW;
                lW.posAndScale = float4(cx - 12.6f, 0.24f, z, 1.0f);
                lW.metaAndSway = float4(0.0f, 1.0f, 1.0f, 0.0f);
                lamps.push_back(lW);

                CityFoliageLampData lE;
                lE.posAndScale = float4(cx + 12.6f, 0.24f, z, 1.0f);
                lE.metaAndSway = float4(3.1415926f, 1.0f, 1.0f, 0.0f);
                lamps.push_back(lE);
            }
            return lamps;
        }

        if (spec.corridorKind == 0u || spec.corridorKind == 3u)
        {
            for (float x = spec.minX + 10.0f; x <= spec.maxX - 10.0f; x += 13.0f)
            {
                const float y = (spec.corridorKind == 3u)
                    ? (cityBridgeDeckHeight(x, spec.bridgeCenterX, spec.bridgeHalfSpan) + 0.16f)
                    : 0.14f;
                CityFoliageLampData lS;
                lS.posAndScale = float4(x, y, spec.minZ + 1.0f, 1.0f);
                lS.metaAndSway = float4(1.5707963f, 1.0f, 1.0f, 0.0f);
                lamps.push_back(lS);

                CityFoliageLampData lN;
                lN.posAndScale = float4(x, y, spec.maxZ - 1.0f, 1.0f);
                lN.metaAndSway = float4(-1.5707963f, 1.0f, 1.0f, 0.0f);
                lamps.push_back(lN);
            }
        }
        else
        {
            for (float z = spec.minZ + 10.0f; z <= spec.maxZ - 10.0f; z += 13.0f)
            {
                CityFoliageLampData lW;
                lW.posAndScale = float4(spec.minX + 1.0f, 0.14f, z, 1.0f);
                lW.metaAndSway = float4(0.0f, 1.0f, 1.0f, 0.0f);
                lamps.push_back(lW);

                CityFoliageLampData lE;
                lE.posAndScale = float4(spec.maxX - 1.0f, 0.14f, z, 1.0f);
                lE.metaAndSway = float4(3.1415926f, 1.0f, 1.0f, 0.0f);
                lamps.push_back(lE);
            }
        }
        return lamps;
    }

    inline eastl::vector<CityVehicleData> generateCorridorLaneVehiclesCPU(IN CityCorridorSpec spec)
    {
        eastl::vector<CityVehicleData> vehicles;
        if (spec.corridorKind == 2u)
        {
            return vehicles;
        }

        const uint cSeed = spec.params.seed ^ (spec.corridorId * 0x27d4eb2du + 701u);
        const uint lanes = spec.laneCount >= 4u ? 4u : 2u;
        const bool isEW = (spec.corridorKind == 0u || spec.corridorKind == 3u);
        const float span = isEW ? (spec.maxX - spec.minX) : (spec.maxZ - spec.minZ);
        const float centerCoord = isEW ? (0.5f * (spec.minZ + spec.maxZ)) : (0.5f * (spec.minX + spec.maxX));
        const uint perLane = (lanes == 4u) ? 5u : 4u;

        for (uint lane = 0u; lane < lanes; ++lane)
        {
            const bool forwardDir = (lane < lanes / 2u);
            const float laneIndexInDir = float(forwardDir ? lane : (lane - lanes / 2u));
            const float lateralMag = (lanes == 4u) ? (1.7f + laneIndexInDir * 3.1f) : 2.1f;
            const float lateralOffset = forwardDir ? lateralMag : -lateralMag;

            for (uint v = 0u; v < perLane; ++v)
            {
                const float j = cityHash01(int(lane) * 17 + int(v), int(spec.corridorId), cSeed);
                const float phase01 = cityClamp((float(v) + 0.15f + 0.65f * j) / float(perLane), 0.02f, 0.98f);
                const float kindRand = cityHash01(int(v) * 29 + int(lane), int(spec.corridorId) + 11, cSeed + 3u);

                float vLen = 4.2f;
                float vWid = 1.85f;
                float vHei = 1.45f;
                float kindPacked = kindRand;
                if (kindRand > 0.88f)
                {
                    // City Bus
                    vLen = 7.8f;
                    vWid = 2.25f;
                    vHei = 2.65f;
                    kindPacked = 2.0f + kindRand;
                }
                else if (kindRand > 0.74f)
                {
                    // Delivery Truck
                    vLen = 6.2f;
                    vWid = 2.15f;
                    vHei = 2.45f;
                    kindPacked = 3.0f + kindRand;
                }
                else if (kindRand > 0.56f)
                {
                    // Taxi / SUV
                    vLen = 4.5f;
                    vWid = 1.95f;
                    vHei = 1.65f;
                    kindPacked = 1.0f + kindRand;
                }

                CityVehicleData veh;
                if (isEW)
                {
                    const float startX = forwardDir ? spec.minX : spec.maxX;
                    const float dirX = forwardDir ? 1.0f : -1.0f;
                    const float yaw = forwardDir ? 0.0f : 3.14159265f;
                    veh.posAndYaw = float4(startX, 0.05f, centerCoord + lateralOffset, yaw);
                    veh.dirAndSpan = float4(dirX, span, 0.0f, phase01);
                }
                else
                {
                    const float startZ = forwardDir ? spec.minZ : spec.maxZ;
                    const float dirZ = forwardDir ? 1.0f : -1.0f;
                    const float yaw = forwardDir ? 1.57079633f : -1.57079633f;
                    veh.posAndYaw = float4(centerCoord + lateralOffset, 0.05f, startZ, yaw);
                    veh.dirAndSpan = float4(0.0f, span, dirZ, phase01);
                }

                veh.sizeAndKind = float4(vLen, vWid, vHei, kindPacked);
                veh.bridgeAndLights = float4(
                    1.0f,
                    1.0f,
                    spec.bridgeCenterX,
                    (spec.corridorKind == 3u) ? 1.0f : 0.0f);
                vehicles.push_back(veh);
            }
        }

        return vehicles;
    }

    inline CityGeometryPack packInfrastructureGeometry(
        IN CityCorridorSpec spec,
        IN CorridorMeshResult mesh,
        IN eastl::vector<CityFoliageLampData> lamps,
        IN eastl::vector<CityVehicleData> vehicles)
    {
        (void)spec;
        CityGeometryPack pack;
        pack.archVertices = mesh.archVertices;
        pack.foliageAndLamps = lamps;
        pack.vehicles = vehicles;
        pack.bridgeCount = mesh.bridgeCount;
        pack.lampCount = uint(lamps.size());
        pack.nestedSubgraphCount = 1u;
        return pack;
    }

    // =========================================================================
    // 5. Parallel Binary-Tree Merge & Deterministic Summary / Pixel Verification
    // =========================================================================

    inline CityGeometryPack mergeCityGeometry(IN CityGeometryPack a, IN CityGeometryPack b)
    {
        CityGeometryPack merged;

        const uint aVerts = uint(a.archVertices.size());
        const uint bVerts = uint(b.archVertices.size());
        merged.archVertices.resize(aVerts + bVerts);
        for (uint i = 0u; i < aVerts; ++i)
        {
            merged.archVertices[i] = a.archVertices[i];
        }
        for (uint i = 0u; i < bVerts; ++i)
        {
            merged.archVertices[aVerts + i] = b.archVertices[i];
        }

        const uint aVeh = uint(a.vehicles.size());
        const uint bVeh = uint(b.vehicles.size());
        merged.vehicles.resize(aVeh + bVeh);
        for (uint i = 0u; i < aVeh; ++i)
        {
            merged.vehicles[i] = a.vehicles[i];
        }
        for (uint i = 0u; i < bVeh; ++i)
        {
            merged.vehicles[aVeh + i] = b.vehicles[i];
        }

        const uint aFL = uint(a.foliageAndLamps.size());
        const uint bFL = uint(b.foliageAndLamps.size());
        merged.foliageAndLamps.resize(aFL + bFL);
        for (uint i = 0u; i < aFL; ++i)
        {
            merged.foliageAndLamps[i] = a.foliageAndLamps[i];
        }
        for (uint i = 0u; i < bFL; ++i)
        {
            merged.foliageAndLamps[aFL + i] = b.foliageAndLamps[i];
        }

        const uint aProps = uint(a.propsAndSignage.size());
        const uint bProps = uint(b.propsAndSignage.size());
        merged.propsAndSignage.resize(aProps + bProps);
        for (uint i = 0u; i < aProps; ++i)
        {
            merged.propsAndSignage[i] = a.propsAndSignage[i];
        }
        for (uint i = 0u; i < bProps; ++i)
        {
            merged.propsAndSignage[aProps + i] = b.propsAndSignage[i];
        }

        merged.cbdTowerCount = a.cbdTowerCount + b.cbdTowerCount;
        merged.commercialBuildingCount = a.commercialBuildingCount + b.commercialBuildingCount;
        merged.residentialBuildingCount = a.residentialBuildingCount + b.residentialBuildingCount;
        merged.parkCount = a.parkCount + b.parkCount;
        merged.bridgeCount = a.bridgeCount + b.bridgeCount;
        merged.treeCount = a.treeCount + b.treeCount;
        merged.lampCount = a.lampCount + b.lampCount;
        merged.nestedSubgraphCount = a.nestedSubgraphCount + b.nestedSubgraphCount;
        return merged;
    }

    inline CityFrameSummary summarizeCityQuadrants(
        IN CityGeometryPack q0,
        IN CityGeometryPack q1,
        IN CityGeometryPack q2,
        IN CityGeometryPack q3)
    {
        CityFrameSummary s;
        s.cbdTowerCount = q0.cbdTowerCount + q1.cbdTowerCount + q2.cbdTowerCount + q3.cbdTowerCount;
        s.commercialBuildingCount = q0.commercialBuildingCount + q1.commercialBuildingCount + q2.commercialBuildingCount + q3.commercialBuildingCount;
        s.residentialBuildingCount = q0.residentialBuildingCount + q1.residentialBuildingCount + q2.residentialBuildingCount + q3.residentialBuildingCount;
        s.buildingCount = s.cbdTowerCount + s.commercialBuildingCount + s.residentialBuildingCount;
        s.parkCount = q0.parkCount + q1.parkCount + q2.parkCount + q3.parkCount;
        s.bridgeCount = q0.bridgeCount + q1.bridgeCount + q2.bridgeCount + q3.bridgeCount;
        s.vehicleCount = uint(q0.vehicles.size() + q1.vehicles.size() + q2.vehicles.size() + q3.vehicles.size());
        s.treeCount = q0.treeCount + q1.treeCount + q2.treeCount + q3.treeCount;
        s.lampCount = q0.lampCount + q1.lampCount + q2.lampCount + q3.lampCount;
        s.propCount = uint(q0.propsAndSignage.size() + q1.propsAndSignage.size() + q2.propsAndSignage.size() + q3.propsAndSignage.size());
        s.nestedSubgraphCount = q0.nestedSubgraphCount + q1.nestedSubgraphCount + q2.nestedSubgraphCount + q3.nestedSubgraphCount;

        const uint totalArchVerts = uint(q0.archVertices.size() + q1.archVertices.size() + q2.archVertices.size() + q3.archVertices.size());
        s.archTriangleCount = totalArchVerts / 3u;
        // Total scene triangles: static arch + 24 tris/vehicle + 16 tris/tree+lamp + 12 tris/prop
        const uint foliageLampTotal = uint(q0.foliageAndLamps.size() + q1.foliageAndLamps.size() + q2.foliageAndLamps.size() + q3.foliageAndLamps.size());
        s.triangleCount = s.archTriangleCount + s.vehicleCount * 24u + foliageLampTotal * 16u + s.propCount * 12u;
        return s;
    }

    inline CityFrameSummary finalizeCitySwapchainSummary(IN CityFrameSummary summary, IN uint presentToken)
    {
        CityFrameSummary out = summary;
        if (presentToken == 0u)
        {
            out.buildingCount = 0u;
        }
        return out;
    }

    inline CityBenchmarkResult evaluateCityBenchmarkPixels(
        IN CityFrameSummary summary,
        IN CityParams params,
        IN eastl::vector<uint8_t> pixels)
    {
        CityBenchmarkResult result;
        result.summary = summary;
        const CityAtmosphere atmo = evaluateCityAtmosphere(params.timeOfDay);
        const int skyR = int(cityClamp(atmo.skyColor.x * 255.0f, 0.0f, 255.0f));
        const int skyG = int(cityClamp(atmo.skyColor.y * 255.0f, 0.0f, 255.0f));
        const int skyB = int(cityClamp(atmo.skyColor.z * 255.0f, 0.0f, 255.0f));

        uint nonSky = 0u;
        uint hash = 2166136261u;
        const uint byteCount = uint(pixels.size());
        for (uint i = 0u; i + 3u < byteCount; i += 4u)
        {
            const uint r = uint(pixels[i + 0u]);
            const uint g = uint(pixels[i + 1u]);
            const uint b = uint(pixels[i + 2u]);
            const int dr = int(r) - skyR;
            const int dg = int(g) - skyG;
            const int db = int(b) - skyB;
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
} // namespace ProceduralCitySample
