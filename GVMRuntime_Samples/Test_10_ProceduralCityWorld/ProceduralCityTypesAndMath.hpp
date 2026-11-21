#pragma once
#include "UGL.h"
#include <EASTL/vector.h>

namespace ProceduralCitySample
{
    using namespace UGL;

    // =========================================================================
    // 1. Self-Contained Deterministic Math (UGLC Static-Purity Validated)
    // =========================================================================

    inline float cityAbs(float x)
    {
        return x < 0.0f ? -x : x;
    }

    inline float cityMin(float a, float b)
    {
        return a < b ? a : b;
    }

    inline float cityMax(float a, float b)
    {
        return a > b ? a : b;
    }

    inline uint cityMinU(uint a, uint b)
    {
        return a < b ? a : b;
    }

    inline uint cityMaxU(uint a, uint b)
    {
        return a > b ? a : b;
    }

    inline float cityClamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline float cityFloor(float x)
    {
        const int i = int(x);
        return (x < float(i)) ? float(i - 1) : float(i);
    }

    inline float cityFract(float x)
    {
        return x - cityFloor(x);
    }

    inline float citySmoothstep(float edge0, float edge1, float x)
    {
        const float denom = edge1 - edge0;
        const float t = denom > 1e-6f ? cityClamp((x - edge0) / denom, 0.0f, 1.0f) : 0.0f;
        return t * t * (3.0f - 2.0f * t);
    }

    inline float cityLerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    inline float3 cityLerp3(float3 a, float3 b, float t)
    {
        return float3(
            cityLerp(a.x, b.x, t),
            cityLerp(a.y, b.y, t),
            cityLerp(a.z, b.z, t));
    }

    inline float citySin(float radians)
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

    inline float cityCos(float radians)
    {
        return citySin(radians + 1.57079632679f);
    }

    inline float citySqrt(float v)
    {
        if (v <= 1e-12f)
        {
            return 0.0f;
        }
        float g = v > 1.0f ? v * 0.5f : 1.0f;
        for (int iter = 0; iter < 6; ++iter)
        {
            g = 0.5f * (g + v / g);
        }
        return g;
    }

    inline uint cityMixBits(uint x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    inline uint cityHashCoords(int ix, int iz, uint seed)
    {
        const uint ux = uint(ix) * 0x9e3779b1u;
        const uint uz = uint(iz) * 0x85ebca77u;
        return cityMixBits(ux ^ uz ^ (seed * 0xc2b2ae3du));
    }

    inline float cityHash01(int ix, int iz, uint seed)
    {
        return float(cityHashCoords(ix, iz, seed) & 0x00ffffffu) * (1.0f / 16777216.0f);
    }

    inline float cityValueNoise2D(float x, float z, uint seed)
    {
        const float fx = cityFloor(x);
        const float fz = cityFloor(z);
        const int ix = int(fx);
        const int iz = int(fz);
        const float tx = x - fx;
        const float tz = z - fz;
        const float ux = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
        const float uz = tz * tz * tz * (tz * (tz * 6.0f - 15.0f) + 10.0f);

        const float v00 = cityHash01(ix, iz, seed);
        const float v10 = cityHash01(ix + 1, iz, seed);
        const float v01 = cityHash01(ix, iz + 1, seed);
        const float v11 = cityHash01(ix + 1, iz + 1, seed);
        return cityLerp(cityLerp(v00, v10, ux), cityLerp(v01, v11, ux), uz);
    }

    // =========================================================================
    // 2. Canal Centerline, Bridge Elevation & Time-of-Day Atmosphere
    // =========================================================================

    inline float cityCanalCenterX(float z, float amplitude, float frequency, uint seed)
    {
        const float phase = cityHash01(int(seed & 0xfffu), 137, seed ^ 0x9e3779b9u) * 6.2831853f;
        const float amp = cityClamp(amplitude, 4.0f, 28.0f);
        return 30.0f
            + amp * 0.30f * citySin(z * frequency + 0.45f + phase * 0.4f)
            + amp * 0.11f * citySin(z * (frequency * 2.15f) - 0.70f + phase * 0.7f);
    }

    inline float cityBridgeDeckHeight(float x, float bridgeCenterX, float halfSpan)
    {
        const float dx = cityAbs(x - bridgeCenterX);
        if (dx >= halfSpan)
        {
            return 0.04f;
        }
        const float t = 1.0f - dx / cityMax(halfSpan, 1.0f);
        const float arch = t * t * (3.0f - 2.0f * t);
        return 0.04f + 2.85f * arch;
    }

    struct CityAtmosphere
    {
        float4 skyColor = float4(0.15f, 0.18f, 0.31f, 1.0f);
        float4 sunDirection = float4(0.60f, 0.28f, -0.75f, 0.0f);
        float4 sunColor = float4(1.00f, 0.60f, 0.32f, 1.0f);
        float4 ambientColor = float4(0.22f, 0.25f, 0.38f, 1.0f);
        float duskFactor = 0.85f;
    };

    inline CityAtmosphere evaluateCityAtmosphere(float timeOfDayHours)
    {
        const float h = cityClamp(timeOfDayHours, 0.0f, 24.0f);
        // Daylight curve: 0 at night, 1 at midday (12h), warm dusk around 18h-19h, dawn around 6h.
        const float sunAngle = (h - 6.0f) * (3.14159265f / 12.0f);
        const float sinElev = citySin(sunAngle);
        const float cosAzim = cityCos(sunAngle);

        const float dayWeight = citySmoothstep(-0.08f, 0.48f, sinElev);
        const float goldenBand = citySmoothstep(-0.28f, 0.08f, sinElev) * (1.0f - citySmoothstep(0.18f, 0.62f, sinElev));
        const float duskFactor = cityClamp(1.0f - citySmoothstep(-0.05f, 0.42f, sinElev), 0.0f, 1.0f);

        const float3 nightSky = float3(0.045f, 0.065f, 0.135f);
        const float3 goldenSky = float3(0.22f, 0.20f, 0.33f);
        const float3 daySky = float3(0.46f, 0.67f, 0.89f);

        float3 sky = cityLerp3(nightSky, daySky, dayWeight);
        sky = cityLerp3(sky, goldenSky, goldenBand * 0.78f);

        float sx = cosAzim * 0.74f;
        float sy = cityMax(sinElev, 0.16f);
        float sz = -0.67f;
        const float invLen = 1.0f / cityMax(citySqrt(sx * sx + sy * sy + sz * sz), 1e-4f);
        sx *= invLen;
        sy *= invLen;
        sz *= invLen;

        const float3 nightSun = float3(0.28f, 0.34f, 0.56f);
        const float3 goldenSun = float3(1.05f, 0.56f, 0.28f);
        const float3 daySun = float3(1.02f, 0.96f, 0.88f);
        float3 sunC = cityLerp3(nightSun, daySun, dayWeight);
        sunC = cityLerp3(sunC, goldenSun, goldenBand * 0.90f);

        const float3 nightAmb = float3(0.10f, 0.13f, 0.23f);
        const float3 goldenAmb = float3(0.26f, 0.24f, 0.35f);
        const float3 dayAmb = float3(0.42f, 0.48f, 0.58f);
        float3 ambC = cityLerp3(nightAmb, dayAmb, dayWeight);
        ambC = cityLerp3(ambC, goldenAmb, goldenBand * 0.75f);

        CityAtmosphere out;
        out.skyColor = float4(sky.x, sky.y, sky.z, 1.0f);
        out.sunDirection = float4(sx, sy, sz, 0.0f);
        out.sunColor = float4(sunC.x, sunC.y, sunC.z, 1.0f);
        out.ambientColor = float4(ambC.x, ambC.y, ambC.z, 1.0f);
        out.duskFactor = duskFactor;
        return out;
    }

    // =========================================================================
    // 3. Shared Procedural City Parameters, Specs & GPU Payload Records
    // =========================================================================

    struct CityParams
    {
        uint2 extent = uint2(960u, 640u);
        uint seed = 2026u;
        float worldHalfSize = 240.0f;

        float timeOfDay = 18.35f;
        float timeSeconds = 1.25f;
        float trafficSpeed = 14.0f;
        float canalAmplitude = 24.0f;
        float canalFrequency = 0.015f;
        float cbdRadius = 114.0f;
        float commercialRadius = 178.0f;

        float4 viewProjRow0 = float4(1.0f, 0.0f, 0.0f, 0.0f);
        float4 viewProjRow1 = float4(0.0f, 1.0f, 0.0f, 0.0f);
        float4 viewProjRow2 = float4(0.0f, 0.0f, 1.0f, 0.0f);
        float4 viewProjRow3 = float4(0.0f, 0.0f, 0.0f, 1.0f);
        float4 eyePosition = float4(-130.0f, 88.0f, -155.0f, 1.0f);
        float4 sunDirection = float4(0.60f, 0.28f, -0.75f, 0.0f);
    };

    /// District kind:
    /// 0 = Downtown CBD Skyscraper
    /// 1 = Commercial & Mid-Rise
    /// 2 = Courtyard Residential
    /// 3 = Central Park & Civic Waterfront
    struct CityBlockSpec
    {
        CityParams params;
        uint quadrantIndex = 0u;
        uint blockId = 0u;
        uint districtKind = 0u;
        float minX = 0.0f;
        float minZ = 0.0f;
        float maxX = 42.0f;
        float maxZ = 42.0f;
        float waterfrontRatio = 0.0f;
        float densityFactor = 1.0f;
    };

    /// Corridor kind:
    /// 0 = East-West Roadway (along X)
    /// 1 = North-South Roadway (along Z)
    /// 2 = Winding Canal + Stone Embankments + Waterfront Promenades
    /// 3 = Multi-Span Arched Bridge over Canal
    struct CityCorridorSpec
    {
        CityParams params;
        uint quadrantIndex = 0u;
        uint corridorId = 0u;
        uint corridorKind = 0u;
        float minX = 0.0f;
        float minZ = 0.0f;
        float maxX = 60.0f;
        float maxZ = 18.0f;
        uint laneCount = 4u;
        float bridgeCenterX = 30.0f;
        float bridgeHalfSpan = 16.0f;
    };

    /// Data-dependent subdivision result produced by Phase 1 per quadrant.
    struct QuadrantSubdivisionPlan
    {
        uint quadrantIndex = 0u;
        eastl::vector<CityBlockSpec> skyscraperBlocks;
        eastl::vector<CityBlockSpec> midriseAndResBlocks;
        eastl::vector<CityCorridorSpec> corridors;
    };

    /// Individual subdivided urban sub-lot inside a commercial, residential, or park block.
    struct UrbanSubLot
    {
        float minX = 0.0f;
        float minZ = 0.0f;
        float maxX = 16.0f;
        float maxZ = 16.0f;
        float height = 16.0f;
        float seed01 = 0.5f;
        uint lotKind = 0u; // 0=commercial, 1=residential, 2=park lawn, 3=park fountain plaza
        uint frontEdgeMask = 15u;
    };

    /// Static architectural & infrastructure vertex (48 bytes, 16-byte aligned).
    /// posAndMat: xyz=worldPos, w=materialId
    ///   0=ground/park grass, 1=asphalt road, 2=lane marking/crosswalk, 3=sidewalk/concrete,
    ///   4=canal water, 5=stone embankment/bridge pier, 6=skyscraper curtain glass,
    ///   7=skyscraper podium/mullion/spire metal, 8=commercial midrise facade,
    ///   9=residential brick, 10=terracotta/slate pitched roof, 11=fountain/plaza marble
    struct CityArchVertex
    {
        float4 posAndMat;
        float4 normalAndTint;
        float4 uvAndMeta;
    };

    /// Vehicle instance (64 bytes): simulated in compute pass along active lanes & bridge decks.
    struct CityVehicleData
    {
        float4 posAndYaw;
        float4 dirAndSpan;
        float4 sizeAndKind;
        float4 bridgeAndLights;
    };

    /// Street tree or streetlamp instance (32 bytes): kind=0 tree, kind=1 streetlamp.
    struct CityFoliageLampData
    {
        float4 posAndScale;
        float4 metaAndSway;
    };

    /// Commercial neon signage, awnings, rooftop HVAC/water tanks, or aviation beacons (48 bytes).
    struct CityPropData
    {
        float4 posAndYaw;
        float4 sizeAndKind;
        float4 colorAndPulse;
    };

    /// Aggregated CPU geometry pack for one block, corridor, or quadrant subtree.
    struct CityGeometryPack
    {
        eastl::vector<CityArchVertex> archVertices;
        eastl::vector<CityVehicleData> vehicles;
        eastl::vector<CityFoliageLampData> foliageAndLamps;
        eastl::vector<CityPropData> propsAndSignage;
        uint cbdTowerCount = 0u;
        uint commercialBuildingCount = 0u;
        uint residentialBuildingCount = 0u;
        uint parkCount = 0u;
        uint bridgeCount = 0u;
        uint treeCount = 0u;
        uint lampCount = 0u;
        uint nestedSubgraphCount = 0u;
    };

    /// Deterministic geometry counters surfaced to ImGui HUD and benchmark verifier.
    struct CityFrameSummary
    {
        uint cbdTowerCount = 0u;
        uint commercialBuildingCount = 0u;
        uint residentialBuildingCount = 0u;
        uint buildingCount = 0u;
        uint parkCount = 0u;
        uint bridgeCount = 0u;
        uint vehicleCount = 0u;
        uint treeCount = 0u;
        uint lampCount = 0u;
        uint propCount = 0u;
        uint nestedSubgraphCount = 0u;
        uint archTriangleCount = 0u;
        uint triangleCount = 0u;
    };

    /// Headless CLI benchmark verification output combining deterministic PCG counts and GPU pixel stats.
    struct CityBenchmarkResult
    {
        CityFrameSummary summary;
        uint nonSkyPixelCount = 0u;
        uint pixelChecksum = 0u;
    };
} // namespace ProceduralCitySample
