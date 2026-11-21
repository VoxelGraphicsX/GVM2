#pragma once
#include "ProceduralCityCpuNodes.hpp"

namespace ProceduralCitySample
{
    using CityArchVertexBuffer = Buffer<CityArchVertex, BufferUsage<Storage, CopyDst>>;
    using CityVehicleBuffer = Buffer<CityVehicleData, BufferUsage<Storage, CopyDst>>;
    using CityFoliageLampBuffer = Buffer<CityFoliageLampData, BufferUsage<Storage, CopyDst>>;
    using CityPropBuffer = Buffer<CityPropData, BufferUsage<Storage, CopyDst>>;
    using CityEnvLutBuffer = Buffer<float4, BufferUsage<Storage>>;

    using CityColorTexture = Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>;
    using CityDepthTexture = Texture<TextureFormat::Depth32Float,
        TextureUsage<RenderAttachment>, TextureDimension::e2D>;

    struct CitySwapchainRequest
    {
        SwapchainQueryResult swapchainTarget;
        CityParams params;
    };

    struct CityQuadrantGpuPack
    {
        CityArchVertexBuffer archVertices;
        CityVehicleBuffer preparedVehicles;
        CityFoliageLampBuffer preparedFoliageAndLamps;
        CityPropBuffer preparedPropsAndSignage;
        uint archVertexCount = 0u;
        uint vehicleCount = 0u;
        uint foliageLampCount = 0u;
        uint propCount = 0u;
    };

    // =========================================================================
    // 1. GPU Compute Shaders (Eager Environment LUT + 3-Way Fissioned Dynamic Prep)
    // =========================================================================

    /// Bakes a 256-entry GPU environment LUT concurrently with CPU lot subdivision & subgraphs.
    class [[LocalWorkGroupSize(64, 1, 1)]] BakeCityEnvironmentLutCompute final : public IComputeClass
    {
    public:
        constructor(
            RWStructuredBuffer<float4> envLut [[Args0]],
            float timeOfDay [[Args1]],
            float timeSeconds [[Args2]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < 256u)
            {
                float u = float(id.x) * (6.2831853f / 256.0f);
                float h = clamp(timeOfDay, 0.0f, 24.0f);
                float sunAngle = (h - 6.0f) * (3.14159265f / 12.0f);
                float sinElev = sin(sunAngle);
                float dusk = clamp(1.0f - smoothstep(-0.05f, 0.42f, sinElev), 0.0f, 1.0f);

                float windowFlicker = 0.85f + 0.15f * sin(u * 3.0f + timeSeconds * 0.7f);
                float neonPulse = 0.78f + 0.22f * sin(u * 5.0f + timeSeconds * 3.4f);
                float waterRipple = sin(u * 4.0f + timeSeconds * 2.1f);
                envLut[id.x] = float4(windowFlicker, neonPulse, dusk, waterRipple);
            }
        }
    };

    /// Advances vehicles along their lane vectors and elevates them smoothly over arched canal bridges.
    class [[LocalWorkGroupSize(64, 1, 1)]] SimulateTrafficVehiclesCompute final : public IComputeClass
    {
    public:
        constructor(
            StructuredBuffer<CityVehicleData> rawVehicles [[Args0]],
            RWStructuredBuffer<CityVehicleData> outVehicles [[Args1]],
            StructuredBuffer<float4> envLut [[Args2]],
            uint count [[Args3]],
            float timeSeconds [[Args4]],
            float trafficSpeed [[Args5]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                CityVehicleData inst = rawVehicles[id.x];
                float span = max(inst.dirAndSpan.y, 1.0f);
                float travel = frac(inst.dirAndSpan.w + (timeSeconds * trafficSpeed) / span);
                float dist = travel * span;

                float px = inst.posAndYaw.x + inst.dirAndSpan.x * dist;
                float pz = inst.posAndYaw.z + inst.dirAndSpan.z * dist;
                float py = inst.posAndYaw.y;

                if (inst.bridgeAndLights.w > 0.5f)
                {
                    float dx = abs(px - inst.bridgeAndLights.z);
                    if (dx < 16.5f)
                    {
                        float t = 1.0f - dx / 16.5f;
                        py = 0.05f + 2.85f * t * t * (3.0f - 2.0f * t);
                    }
                }

                float dusk = envLut[0].z;
                float lightBoost = 0.30f + 0.95f * dusk;
                inst.posAndYaw = float4(px, py, pz, inst.posAndYaw.w);
                inst.bridgeAndLights = float4(
                    inst.bridgeAndLights.x * lightBoost,
                    inst.bridgeAndLights.y * lightBoost,
                    inst.bridgeAndLights.z,
                    inst.bridgeAndLights.w);
                outVehicles[id.x] = inst;
            }
        }
    };

    /// Computes tree wind sway and dusk streetlamp activation in parallel with traffic & signage.
    class [[LocalWorkGroupSize(64, 1, 1)]] PrepareStreetFoliageAndLampsCompute final : public IComputeClass
    {
    public:
        constructor(
            StructuredBuffer<CityFoliageLampData> rawItems [[Args0]],
            RWStructuredBuffer<CityFoliageLampData> outItems [[Args1]],
            StructuredBuffer<float4> envLut [[Args2]],
            uint count [[Args3]],
            float4 eyePos [[Args4]],
            float timeSeconds [[Args5]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                CityFoliageLampData inst = rawItems[id.x];
                float dusk = envLut[0].z;
                float dx = inst.posAndScale.x - eyePos.x;
                float dz = inst.posAndScale.z - eyePos.z;
                float dist = sqrt(dx * dx + dz * dz);
                float fade = 1.0f - smoothstep(320.0f, 440.0f, dist);

                if (inst.metaAndSway.y < 0.5f)
                {
                    // Tree wind sway
                    float sway = sin(inst.posAndScale.x * 0.14f + inst.posAndScale.z * 0.11f + timeSeconds * 2.1f) * 0.14f;
                    inst.posAndScale.w = inst.posAndScale.w * fade;
                    inst.metaAndSway.w = sway;
                }
                else
                {
                    // Streetlamp dusk activation
                    inst.posAndScale.w = inst.posAndScale.w * fade;
                    inst.metaAndSway.z = dusk;
                }
                outItems[id.x] = inst;
            }
        }
    };

    /// Animates commercial neon signage shimmer and skyscraper aviation beacons in a 3rd disjoint compute pass.
    class [[LocalWorkGroupSize(64, 1, 1)]] PrepareRooftopAndSignageCompute final : public IComputeClass
    {
    public:
        constructor(
            StructuredBuffer<CityPropData> rawProps [[Args0]],
            RWStructuredBuffer<CityPropData> outProps [[Args1]],
            StructuredBuffer<float4> envLut [[Args2]],
            uint count [[Args3]],
            float timeSeconds [[Args4]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                CityPropData inst = rawProps[id.x];
                float dusk = envLut[0].z;
                uint lutIdx = uint(abs(inst.posAndYaw.x * 2.3f + inst.posAndYaw.z * 3.1f)) & 255u;
                float shimmer = envLut[lutIdx].y;
                float kind = inst.sizeAndKind.w;

                if (kind < 0.5f)
                {
                    // Commercial neon signage
                    inst.colorAndPulse.w = (0.35f + 0.90f * dusk) * shimmer;
                }
                else if (kind > 2.5f)
                {
                    // Rooftop aviation beacon blink
                    float s = 0.5f + 0.5f * sin(timeSeconds * 4.5f + inst.posAndYaw.x * 0.17f);
                    inst.colorAndPulse.w = (0.25f + 1.45f * s * s * s * s) * (0.4f + 0.6f * dusk);
                }
                outProps[id.x] = inst;
            }
        }
    };

    // =========================================================================
    // 2. Procedural City Forward Shaders (Curtain-Wall Glass, Asphalt, Canal, Fleet)
    // =========================================================================

    struct CitySceneFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
        DepthStencilAttachment<TextureFormat::Depth32Float> depthStencil;
    };

    struct CityArchVarying
    {
        float4 position [[Position]];
        float4 worldPosMat [[Attribute0]];
        float4 normalTint [[Attribute1]];
        float4 uvMeta [[Attribute2]];
    };

    struct CitySimpleVarying
    {
        float4 position [[Position]];
        float4 colorAndFog [[Attribute0]];
        float4 worldAndEmissive [[Attribute1]];
    };

    /// Renders architectural buildings, procedural glass window grids, roads, bridges, and canal water.
    class CityArchitectureShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<CityArchVertex> vertices [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]],
            float4 skyColor [[Args7]],
            float4 sunColor [[Args8]],
            float4 ambientColor [[Args9]],
            float duskFactor [[Args10]],
            float timeSeconds [[Args11]])
        {
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(true);
            setCullMode(CullMode::None);
        }

    private:
        CityArchVarying vertex(uint vid [[VertexID]])
        {
            CityArchVertex v = vertices[vid];
            float4 wp = float4(v.posAndMat.x, v.posAndMat.y, v.posAndMat.z, 1.0f);
            CityArchVarying out;
            out.position = float4(
                dot(vpRow0, wp),
                dot(vpRow1, wp),
                dot(vpRow2, wp),
                dot(vpRow3, wp));
            out.worldPosMat = v.posAndMat;
            out.normalTint = v.normalAndTint;
            out.uvMeta = v.uvAndMeta;
            return out;
        }

        CitySceneFrameBuffer fragment(CityArchVarying input)
        {
            float3 wp = float3(input.worldPosMat.x, input.worldPosMat.y, input.worldPosMat.z);
            float matId = input.worldPosMat.w;
            float3 n = normalize(float3(input.normalTint.x, input.normalTint.y, input.normalTint.z));
            float tint = input.normalTint.w;
            float2 uv = float2(input.uvMeta.x, input.uvMeta.y);
            float bSeed = input.uvMeta.z;
            float floorH = max(input.uvMeta.w, 1.0f);

            float3 l = normalize(float3(sunDir.x, sunDir.y, sunDir.z));
            float3 v = normalize(float3(eyePos.x - wp.x, eyePos.y - wp.y, eyePos.z - wp.z));
            float ndotl = max(dot(n, l), 0.0f);
            float3 amb = float3(ambientColor.x, ambientColor.y, ambientColor.z);
            float3 sunC = float3(sunColor.x, sunColor.y, sunColor.z);
            float3 skyC = float3(skyColor.x, skyColor.y, skyColor.z);

            float3 albedo = float3(0.5f, 0.5f, 0.5f);
            float3 emissive = float3(0.0f, 0.0f, 0.0f);
            float specStrength = 0.08f;

            if (matId < 0.5f)
            {
                // 0: Park & courtyard turf lawn
                float turf = 0.5f + 0.5f * sin(wp.x * 0.25f) * cos(wp.z * 0.25f);
                albedo = lerp(float3(0.14f, 0.36f, 0.14f), float3(0.22f, 0.45f, 0.17f), turf * 0.65f + tint * 0.25f);
            }
            else if (matId < 1.5f)
            {
                // 1: Asphalt roadway
                float grain = frac(sin(floor(wp.x * 3.0f) * 12.9898f + floor(wp.z * 3.0f) * 78.233f) * 43758.5453f);
                albedo = float3(0.11f, 0.12f, 0.14f) * (0.88f + 0.22f * grain);
                specStrength = 0.22f;
            }
            else if (matId < 2.5f)
            {
                // 2: Road lane markings & crosswalks
                albedo = tint < 0.94f ? float3(0.90f, 0.72f, 0.20f) : float3(0.88f, 0.90f, 0.92f);
                emissive = albedo * (0.08f * duskFactor);
            }
            else if (matId < 3.5f)
            {
                // 3: Concrete sidewalks & plazas with paving slab grid
                float gx = frac(wp.x * 0.35f);
                float gz = frac(wp.z * 0.35f);
                float joint = (gx < 0.06f || gz < 0.06f) ? 0.84f : 1.0f;
                albedo = lerp(float3(0.42f, 0.43f, 0.45f), float3(0.55f, 0.54f, 0.52f), tint) * joint;
            }
            else if (matId < 4.5f)
            {
                // 4: Canal & fountain water with multi-wave ripples + Fresnel sky reflection
                float w1 = sin(wp.z * 0.55f + timeSeconds * 2.2f + wp.x * 0.25f);
                float w2 = cos(wp.x * 0.72f - timeSeconds * 1.7f + wp.z * 0.30f);
                float3 wn = normalize(float3(w1 * 0.08f, 1.0f, w2 * 0.08f));
                float fres = pow(clamp(1.0f - max(dot(wn, v), 0.0f), 0.0f, 1.0f), 3.0f);
                float3 deepWater = float3(0.05f, 0.16f, 0.24f);
                float3 reflSky = lerp(skyC, sunC * 0.85f, fres * 0.55f);
                albedo = lerp(deepWater, reflSky, 0.35f + 0.55f * fres);
                emissive = float3(0.18f, 0.12f, 0.06f) * (duskFactor * (0.3f + 0.7f * fres));
                specStrength = 0.75f;
                n = wn;
            }
            else if (matId < 5.5f)
            {
                // 5: Stone canal embankments & bridge arches/piers
                float course = frac(wp.y * 1.1f) < 0.10f ? 0.80f : 1.0f;
                albedo = float3(0.46f, 0.43f, 0.38f) * course;
            }
            else if (matId < 6.5f)
            {
                // 6: Skyscraper glass curtain wall (procedural mullions and lit office rooms)
                float colIdx = floor(uv.x / 2.6f);
                float rowIdx = floor(uv.y / floorH);
                float fx = frac(uv.x / 2.6f);
                float fy = frac(uv.y / floorH);
                bool isMullion = (fx < 0.11f || fx > 0.89f || fy < 0.15f || fy > 0.89f);

                if (isMullion)
                {
                    albedo = float3(0.18f, 0.21f, 0.26f);
                    specStrength = 0.45f;
                }
                else
                {
                    float fres = pow(clamp(1.0f - max(dot(n, v), 0.0f), 0.0f, 1.0f), 2.5f);
                    float3 glassBase = lerp(float3(0.10f, 0.18f, 0.28f), float3(0.14f, 0.24f, 0.34f), tint);
                    albedo = lerp(glassBase, skyC * 1.15f, 0.32f + 0.52f * fres);
                    specStrength = 0.85f;

                    float roomHash = frac(sin(colIdx * 127.1f + rowIdx * 311.7f + bSeed * 17.3f) * 43758.5453f);
                    float floorBand = frac(sin(rowIdx * 53.7f + bSeed * 7.1f) * 19432.17f);
                    if (roomHash > (0.44f - 0.18f * floorBand))
                    {
                        float3 warmOffice = float3(1.00f, 0.77f, 0.40f);
                        float3 coolOffice = float3(0.72f, 0.89f, 1.00f);
                        float3 roomGlow = (roomHash > 0.80f) ? coolOffice : warmOffice;
                        emissive = roomGlow * ((0.35f + 0.65f * roomHash) * (0.20f + 1.05f * duskFactor));
                    }
                }
            }
            else if (matId < 7.5f)
            {
                // 7: Skyscraper podium, mullions, crown & spire brushed metal/stone
                albedo = lerp(float3(0.20f, 0.22f, 0.26f), float3(0.42f, 0.44f, 0.48f), tint);
                specStrength = 0.42f;
            }
            else if (matId < 8.5f)
            {
                // 8: Commercial mid-rise facade with procedural lit windows
                float colIdx = floor(uv.x / 2.4f);
                float rowIdx = floor(uv.y / floorH);
                float fx = frac(uv.x / 2.4f);
                float fy = frac(uv.y / floorH);
                bool isWindow = (fx > 0.20f && fx < 0.80f && fy > 0.24f && fy < 0.82f);
                float3 wallColor = lerp(float3(0.56f, 0.51f, 0.44f), float3(0.40f, 0.44f, 0.48f), tint);
                if (isWindow)
                {
                    albedo = float3(0.14f, 0.18f, 0.24f);
                    float rHash = frac(sin(colIdx * 91.3f + rowIdx * 217.9f + bSeed * 11.1f) * 29173.41f);
                    if (rHash > 0.45f)
                    {
                        emissive = float3(1.00f, 0.78f, 0.44f) * (0.22f + 0.95f * duskFactor);
                    }
                }
                else
                {
                    albedo = wallColor;
                }
            }
            else if (matId < 9.5f)
            {
                // 9: Residential brick townhouse facade + cozy home windows
                float colIdx = floor(uv.x / 2.2f);
                float rowIdx = floor(uv.y / floorH);
                float fx = frac(uv.x / 2.2f);
                float fy = frac(uv.y / floorH);
                bool isWindow = (fx > 0.26f && fx < 0.74f && fy > 0.26f && fy < 0.78f);
                float3 brick = lerp(float3(0.58f, 0.30f, 0.22f), float3(0.68f, 0.48f, 0.34f), tint);
                if (isWindow)
                {
                    albedo = float3(0.15f, 0.16f, 0.20f);
                    float rHash = frac(sin(colIdx * 61.7f + rowIdx * 173.3f + bSeed * 5.9f) * 18421.7f);
                    if (rHash > 0.36f)
                    {
                        emissive = float3(1.00f, 0.74f, 0.38f) * (0.20f + 0.90f * duskFactor);
                    }
                }
                else
                {
                    float course = frac(uv.y * 3.5f) < 0.12f ? 0.88f : 1.0f;
                    albedo = brick * course;
                }
            }
            else if (matId < 10.5f)
            {
                // 10: Terracotta / slate pitched roofs
                albedo = lerp(float3(0.56f, 0.24f, 0.17f), float3(0.24f, 0.27f, 0.31f), tint);
            }
            else
            {
                // 11: Travertine marble plaza / helipad / fountain monument
                albedo = float3(0.76f, 0.74f, 0.70f);
            }

            float3 hVec = normalize(l + v);
            float spec = pow(max(dot(n, hVec), 0.0f), 28.0f) * specStrength;
            float3 lit = albedo * (amb + sunC * ndotl * 0.95f) + sunC * spec + emissive;

            float dist = length(float3(wp.x - eyePos.x, wp.y - eyePos.y, wp.z - eyePos.z));
            float fog = smoothstep(210.0f, 520.0f, dist);
            float3 finalRgb = lerp(lit, skyC, fog);

            CitySceneFrameBuffer out;
            out.color = half4(half(clamp(finalRgb.x, 0.0f, 1.0f)), half(clamp(finalRgb.y, 0.0f, 1.0f)), half(clamp(finalRgb.z, 0.0f, 1.0f)), half(1.0f));
            return out;
        }
    };

    /// Renders boulevard/park/courtyard trees and glowing dusk streetlamps (48 verts = 16 tris/instance).
    class CityFoliageAndLampShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<CityFoliageLampData> instances [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]],
            float4 skyColor [[Args7]],
            float4 sunColor [[Args8]],
            float4 ambientColor [[Args9]])
        {
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(true);
            setCullMode(CullMode::None);
        }

    private:
        CitySimpleVarying vertex(uint vid [[VertexID]], uint iid [[InstanceID]])
        {
            CityFoliageLampData inst = instances[iid];
            float scale = inst.posAndScale.w;
            float yaw = inst.metaAndSway.x;
            float kind = inst.metaAndSway.y;
            float tintOrOn = inst.metaAndSway.z;
            float sway = inst.metaAndSway.w;

            uint tri = vid / 3u;
            uint corner = vid % 3u;

            float3 localPos = float3(0.0f, 0.0f, 0.0f);
            float3 localN = float3(0.0f, 1.0f, 0.0f);
            float3 baseColor = float3(0.2f, 0.4f, 0.2f);
            float3 emissive = float3(0.0f, 0.0f, 0.0f);

            if (kind < 0.5f)
            {
                // Urban Tree: tris 0..3 crossed trunk, tris 4..15 3-tier lush foliage pyramids
                if (tri < 4u)
                {
                    uint fin = tri / 2u;
                    uint halfTri = tri % 2u;
                    float ang = float(fin) * 1.5707963f;
                    float px = cos(ang) * 0.22f;
                    float pz = sin(ang) * 0.22f;
                    float3 b0 = float3(-px, 0.0f, -pz);
                    float3 b1 = float3(px, 0.0f, pz);
                    float3 t0 = float3(-px * 0.55f + sway * 0.2f, 2.3f, -pz * 0.55f);
                    float3 t1 = float3(px * 0.55f + sway * 0.2f, 2.3f, pz * 0.55f);
                    if (halfTri == 0u)
                    {
                        localPos = corner == 0u ? b0 : (corner == 1u ? b1 : t1);
                    }
                    else
                    {
                        localPos = corner == 0u ? b0 : (corner == 1u ? t1 : t0);
                    }
                    localN = normalize(float3(-pz, 0.3f, px));
                    baseColor = float3(0.32f, 0.22f, 0.14f);
                }
                else
                {
                    uint tier = (tri - 4u) / 4u;
                    uint fin = (tri - 4u) % 4u;
                    float ang = float(fin) * 0.785398f + float(tier) * 0.4f;
                    float baseY = 1.4f + float(tier) * 1.15f;
                    float apexY = baseY + 2.25f - float(tier) * 0.25f;
                    float rad = 1.65f - float(tier) * 0.32f;
                    float rx = cos(ang) * rad;
                    float rz = sin(ang) * rad;
                    float swayTier = sway * (0.4f + 0.3f * float(tier));
                    if (corner == 0u) { localPos = float3(-rx + swayTier, baseY, -rz); }
                    else if (corner == 1u) { localPos = float3(rx + swayTier, baseY, rz); }
                    else { localPos = float3(swayTier * 1.4f, apexY, 0.0f); }
                    localN = normalize(float3(localPos.x * 0.5f, 0.85f, localPos.z * 0.5f));
                    baseColor = lerp(float3(0.13f, 0.34f, 0.13f), float3(0.26f, 0.48f, 0.17f), tintOrOn);
                }
            }
            else
            {
                // Streetlamp: tris 0..7 pole, tris 8..11 mast arm, tris 12..15 glowing lamp head
                if (tri < 8u)
                {
                    uint side = tri / 2u;
                    uint hTri = tri % 2u;
                    float a0 = float(side) * 1.5707963f;
                    float a1 = a0 + 1.5707963f;
                    float rB = 0.14f;
                    float rT = 0.09f;
                    float3 p0 = float3(cos(a0) * rB, 0.0f, sin(a0) * rB);
                    float3 p1 = float3(cos(a1) * rB, 0.0f, sin(a1) * rB);
                    float3 p2 = float3(cos(a1) * rT, 5.4f, sin(a1) * rT);
                    float3 p3 = float3(cos(a0) * rT, 5.4f, sin(a0) * rT);
                    localPos = (hTri == 0u) ? (corner == 0u ? p0 : (corner == 1u ? p1 : p2))
                                            : (corner == 0u ? p0 : (corner == 1u ? p2 : p3));
                    localN = normalize(float3(cos(a0 + 0.78f), 0.1f, sin(a0 + 0.78f)));
                    baseColor = float3(0.16f, 0.18f, 0.21f);
                }
                else if (tri < 12u)
                {
                    uint hTri = (tri - 8u) % 2u;
                    float yOff = (tri < 10u) ? 5.35f : 5.52f;
                    float3 p0 = float3(0.0f, yOff, -0.07f);
                    float3 p1 = float3(1.15f, yOff + 0.15f, -0.07f);
                    float3 p2 = float3(1.15f, yOff + 0.15f, 0.07f);
                    float3 p3 = float3(0.0f, yOff, 0.07f);
                    localPos = (hTri == 0u) ? (corner == 0u ? p0 : (corner == 1u ? p1 : p2))
                                            : (corner == 0u ? p0 : (corner == 1u ? p2 : p3));
                    localN = float3(0.0f, 1.0f, 0.0f);
                    baseColor = float3(0.18f, 0.20f, 0.23f);
                }
                else
                {
                    uint side = tri - 12u;
                    float a0 = float(side) * 1.5707963f;
                    float a1 = a0 + 1.5707963f;
                    float3 p0 = float3(1.10f + cos(a0) * 0.28f, 5.42f, sin(a0) * 0.28f);
                    float3 p1 = float3(1.10f + cos(a1) * 0.28f, 5.42f, sin(a1) * 0.28f);
                    float3 apex = float3(1.10f, 5.75f, 0.0f);
                    localPos = corner == 0u ? p0 : (corner == 1u ? p1 : apex);
                    localN = float3(0.0f, -1.0f, 0.0f);
                    baseColor = float3(0.95f, 0.85f, 0.65f);
                    emissive = float3(1.00f, 0.78f, 0.36f) * (0.25f + 1.35f * tintOrOn);
                }
            }

            float cy = cos(yaw);
            float sy = sin(yaw);
            float3 rotP = float3(localPos.x * cy - localPos.z * sy, localPos.y, localPos.x * sy + localPos.z * cy) * scale;
            float3 worldN = normalize(float3(localN.x * cy - localN.z * sy, localN.y, localN.x * sy + localN.z * cy));
            float4 wp = float4(inst.posAndScale.x + rotP.x, inst.posAndScale.y + rotP.y, inst.posAndScale.z + rotP.z, 1.0f);

            CitySimpleVarying out;
            out.position = float4(dot(vpRow0, wp), dot(vpRow1, wp), dot(vpRow2, wp), dot(vpRow3, wp));

            float3 l = normalize(float3(sunDir.x, sunDir.y, sunDir.z));
            float ndotl = max(dot(worldN, l), 0.0f);
            float3 amb = float3(ambientColor.x, ambientColor.y, ambientColor.z);
            float3 sunC = float3(sunColor.x, sunColor.y, sunColor.z);
            float3 lit = baseColor * (amb + sunC * ndotl * 0.85f) + emissive;
            float dist = length(float3(wp.x - eyePos.x, wp.y - eyePos.y, wp.z - eyePos.z));
            float fog = smoothstep(210.0f, 520.0f, dist);
            out.colorAndFog = float4(lit.x, lit.y, lit.z, fog);
            out.worldAndEmissive = float4(skyColor.x, skyColor.y, skyColor.z, 1.0f);
            return out;
        }

        CitySceneFrameBuffer fragment(CitySimpleVarying input)
        {
            float3 skyC = float3(input.worldAndEmissive.x, input.worldAndEmissive.y, input.worldAndEmissive.z);
            float3 finalRgb = lerp(
                float3(input.colorAndFog.x, input.colorAndFog.y, input.colorAndFog.z),
                skyC,
                input.colorAndFog.w);
            CitySceneFrameBuffer out;
            out.color = half4(half(clamp(finalRgb.x, 0.0f, 1.0f)), half(clamp(finalRgb.y, 0.0f, 1.0f)), half(clamp(finalRgb.z, 0.0f, 1.0f)), half(1.0f));
            return out;
        }
    };

    /// Renders commercial neon signage, storefront awnings, rooftop HVAC, and aviation beacons (36 verts = 12 tris/prop).
    class CityPropAndSignageShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<CityPropData> instances [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]],
            float4 skyColor [[Args7]],
            float4 sunColor [[Args8]],
            float4 ambientColor [[Args9]])
        {
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(true);
            setCullMode(CullMode::None);
        }

    private:
        CitySimpleVarying vertex(uint vid [[VertexID]], uint iid [[InstanceID]])
        {
            CityPropData inst = instances[iid];
            float3 halfExt = float3(inst.sizeAndKind.x, inst.sizeAndKind.y, inst.sizeAndKind.z);
            float kind = inst.sizeAndKind.w;
            float3 rgb = float3(inst.colorAndPulse.x, inst.colorAndPulse.y, inst.colorAndPulse.z);
            float pulse = inst.colorAndPulse.w;

            uint face = vid / 6u;
            uint inFace = vid % 6u;
            uint corner = (inFace == 0u || inFace == 3u) ? 0u : ((inFace == 1u) ? 1u : ((inFace == 2u || inFace == 4u) ? 2u : 3u));

            float3 p0 = float3(0.0f, 0.0f, 0.0f);
            float3 p1 = float3(0.0f, 0.0f, 0.0f);
            float3 p2 = float3(0.0f, 0.0f, 0.0f);
            float3 p3 = float3(0.0f, 0.0f, 0.0f);
            float3 fn = float3(0.0f, 1.0f, 0.0f);

            if (face == 0u) // +Y
            {
                p0 = float3(-1.0f, 1.0f, -1.0f); p1 = float3(1.0f, 1.0f, -1.0f);
                p2 = float3(1.0f, 1.0f, 1.0f); p3 = float3(-1.0f, 1.0f, 1.0f);
                fn = float3(0.0f, 1.0f, 0.0f);
            }
            else if (face == 1u) // -Y
            {
                p0 = float3(-1.0f, -1.0f, 1.0f); p1 = float3(1.0f, -1.0f, 1.0f);
                p2 = float3(1.0f, -1.0f, -1.0f); p3 = float3(-1.0f, -1.0f, -1.0f);
                fn = float3(0.0f, -1.0f, 0.0f);
            }
            else if (face == 2u) // +Z
            {
                p0 = float3(-1.0f, -1.0f, 1.0f); p1 = float3(1.0f, -1.0f, 1.0f);
                p2 = float3(1.0f, 1.0f, 1.0f); p3 = float3(-1.0f, 1.0f, 1.0f);
                fn = float3(0.0f, 0.0f, 1.0f);
            }
            else if (face == 3u) // -Z
            {
                p0 = float3(1.0f, -1.0f, -1.0f); p1 = float3(-1.0f, -1.0f, -1.0f);
                p2 = float3(-1.0f, 1.0f, -1.0f); p3 = float3(1.0f, 1.0f, -1.0f);
                fn = float3(0.0f, 0.0f, -1.0f);
            }
            else if (face == 4u) // +X
            {
                p0 = float3(1.0f, -1.0f, 1.0f); p1 = float3(1.0f, -1.0f, -1.0f);
                p2 = float3(1.0f, 1.0f, -1.0f); p3 = float3(1.0f, 1.0f, 1.0f);
                fn = float3(1.0f, 0.0f, 0.0f);
            }
            else // -X
            {
                p0 = float3(-1.0f, -1.0f, -1.0f); p1 = float3(-1.0f, -1.0f, 1.0f);
                p2 = float3(-1.0f, 1.0f, 1.0f); p3 = float3(-1.0f, 1.0f, -1.0f);
                fn = float3(-1.0f, 0.0f, 0.0f);
            }

            float3 boxCorner = (corner == 0u) ? p0 : ((corner == 1u) ? p1 : ((corner == 2u) ? p2 : p3));
            float4 wp = float4(
                inst.posAndYaw.x + boxCorner.x * halfExt.x,
                inst.posAndYaw.y + boxCorner.y * halfExt.y,
                inst.posAndYaw.z + boxCorner.z * halfExt.z,
                1.0f);

            CitySimpleVarying out;
            out.position = float4(dot(vpRow0, wp), dot(vpRow1, wp), dot(vpRow2, wp), dot(vpRow3, wp));
            float3 l = normalize(float3(sunDir.x, sunDir.y, sunDir.z));
            float ndotl = max(dot(fn, l), 0.0f);
            float3 amb = float3(ambientColor.x, ambientColor.y, ambientColor.z);
            float3 sunC = float3(sunColor.x, sunColor.y, sunColor.z);
            float3 emissive = (kind < 0.5f || kind > 2.5f) ? (rgb * pulse) : float3(0.0f, 0.0f, 0.0f);
            float3 lit = rgb * (amb + sunC * ndotl * 0.85f) + emissive;
            float dist = length(float3(wp.x - eyePos.x, wp.y - eyePos.y, wp.z - eyePos.z));
            float fog = smoothstep(210.0f, 520.0f, dist);
            out.colorAndFog = float4(lit.x, lit.y, lit.z, fog);
            out.worldAndEmissive = float4(skyColor.x, skyColor.y, skyColor.z, 1.0f);
            return out;
        }

        CitySceneFrameBuffer fragment(CitySimpleVarying input)
        {
            float3 skyC = float3(input.worldAndEmissive.x, input.worldAndEmissive.y, input.worldAndEmissive.z);
            float3 finalRgb = lerp(
                float3(input.colorAndFog.x, input.colorAndFog.y, input.colorAndFog.z),
                skyC,
                input.colorAndFog.w);
            CitySceneFrameBuffer out;
            out.color = half4(half(clamp(finalRgb.x, 0.0f, 1.0f)), half(clamp(finalRgb.y, 0.0f, 1.0f)), half(clamp(finalRgb.z, 0.0f, 1.0f)), half(1.0f));
            return out;
        }
    };

    /// Renders procedural city vehicles (cars, taxis, buses, trucks) with glowing headlights & red taillights (72 verts = 24 tris).
    class CityVehicleShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<CityVehicleData> instances [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]],
            float4 skyColor [[Args7]],
            float4 sunColor [[Args8]],
            float4 ambientColor [[Args9]])
        {
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(true);
            setCullMode(CullMode::None);
        }

    private:
        CitySimpleVarying vertex(uint vid [[VertexID]], uint iid [[InstanceID]])
        {
            CityVehicleData inst = instances[iid];
            float vLen = inst.sizeAndKind.x;
            float vWid = inst.sizeAndKind.y;
            float vHei = inst.sizeAndKind.z;
            float kindPacked = inst.sizeAndKind.w;
            float yaw = inst.posAndYaw.w;
            float headIntensity = inst.bridgeAndLights.x;
            float tailIntensity = inst.bridgeAndLights.y;

            uint quad = vid / 6u;
            uint inQuad = vid % 6u;
            uint corner = (inQuad == 0u || inQuad == 3u) ? 0u : ((inQuad == 1u) ? 1u : ((inQuad == 2u || inQuad == 4u) ? 2u : 3u));

            // Select realistic vehicle body color from packed kind/variant
            float3 bodyColor = float3(0.70f, 0.74f, 0.78f);
            float tint = frac(kindPacked);
            if (kindPacked >= 2.0f && kindPacked < 3.0f)
            {
                // City Bus (blue/white transit)
                bodyColor = float3(0.18f, 0.42f, 0.76f);
            }
            else if (kindPacked >= 3.0f)
            {
                // Delivery Truck
                bodyColor = tint > 0.5f ? float3(0.86f, 0.86f, 0.84f) : float3(0.84f, 0.42f, 0.16f);
            }
            else if (kindPacked >= 1.0f)
            {
                // Taxi / SUV
                bodyColor = tint > 0.45f ? float3(0.95f, 0.76f, 0.14f) : float3(0.22f, 0.25f, 0.30f);
            }
            else
            {
                // Sedans
                if (tint < 0.25f) { bodyColor = float3(0.78f, 0.16f, 0.18f); }
                else if (tint < 0.55f) { bodyColor = float3(0.18f, 0.24f, 0.34f); }
                else if (tint < 0.80f) { bodyColor = float3(0.82f, 0.84f, 0.88f); }
                else { bodyColor = float3(0.28f, 0.30f, 0.33f); }
            }

            float hl = vLen * 0.5f;
            float hw = vWid * 0.5f;
            float lowTop = (kindPacked >= 2.0f) ? (vHei * 0.65f) : (vHei * 0.52f);
            float cabMinX = (kindPacked >= 2.0f && kindPacked < 3.0f) ? (-hl * 0.90f) : (-hl * 0.52f);
            float cabMaxX = (kindPacked >= 3.0f) ? (hl * 0.88f) : ((kindPacked >= 2.0f) ? (hl * 0.92f) : (hl * 0.42f));

            float3 p0 = float3(0.0f, 0.0f, 0.0f);
            float3 p1 = float3(0.0f, 0.0f, 0.0f);
            float3 p2 = float3(0.0f, 0.0f, 0.0f);
            float3 p3 = float3(0.0f, 0.0f, 0.0f);
            float3 fn = float3(0.0f, 1.0f, 0.0f);
            float3 col = bodyColor;
            float3 emissive = float3(0.0f, 0.0f, 0.0f);

            // Quads 0..4: lower chassis (+Y, +X front, -X back, +Z side, -Z side)
            if (quad == 0u)
            {
                p0 = float3(-hl, lowTop, -hw); p1 = float3(hl, lowTop, -hw);
                p2 = float3(hl, lowTop, hw); p3 = float3(-hl, lowTop, hw);
                fn = float3(0.0f, 1.0f, 0.0f);
            }
            else if (quad == 1u)
            {
                p0 = float3(hl, 0.18f, -hw); p1 = float3(hl, 0.18f, hw);
                p2 = float3(hl, lowTop, hw); p3 = float3(hl, lowTop, -hw);
                fn = float3(1.0f, 0.0f, 0.0f);
            }
            else if (quad == 2u)
            {
                p0 = float3(-hl, 0.18f, hw); p1 = float3(-hl, 0.18f, -hw);
                p2 = float3(-hl, lowTop, -hw); p3 = float3(-hl, lowTop, hw);
                fn = float3(-1.0f, 0.0f, 0.0f);
            }
            else if (quad == 3u)
            {
                p0 = float3(-hl, 0.18f, hw); p1 = float3(hl, 0.18f, hw);
                p2 = float3(hl, lowTop, hw); p3 = float3(-hl, lowTop, hw);
                fn = float3(0.0f, 0.0f, 1.0f);
            }
            else if (quad == 4u)
            {
                p0 = float3(hl, 0.18f, -hw); p1 = float3(-hl, 0.18f, -hw);
                p2 = float3(-hl, lowTop, -hw); p3 = float3(hl, lowTop, -hw);
                fn = float3(0.0f, 0.0f, -1.0f);
            }
            // Quads 5..9: upper cabin (roof + 4 glass/cabin sides)
            else if (quad == 5u)
            {
                p0 = float3(cabMinX, vHei, -hw * 0.85f); p1 = float3(cabMaxX, vHei, -hw * 0.85f);
                p2 = float3(cabMaxX, vHei, hw * 0.85f); p3 = float3(cabMinX, vHei, hw * 0.85f);
                fn = float3(0.0f, 1.0f, 0.0f);
            }
            else if (quad == 6u)
            {
                p0 = float3(cabMaxX + 0.18f, lowTop, -hw * 0.88f); p1 = float3(cabMaxX + 0.18f, lowTop, hw * 0.88f);
                p2 = float3(cabMaxX, vHei, hw * 0.85f); p3 = float3(cabMaxX, vHei, -hw * 0.85f);
                fn = float3(0.8f, 0.6f, 0.0f);
                col = float3(0.12f, 0.18f, 0.24f);
            }
            else if (quad == 7u)
            {
                p0 = float3(cabMinX - 0.15f, lowTop, hw * 0.88f); p1 = float3(cabMinX - 0.15f, lowTop, -hw * 0.88f);
                p2 = float3(cabMinX, vHei, -hw * 0.85f); p3 = float3(cabMinX, vHei, hw * 0.85f);
                fn = float3(-0.8f, 0.6f, 0.0f);
                col = float3(0.12f, 0.18f, 0.24f);
            }
            else if (quad == 8u)
            {
                p0 = float3(cabMinX - 0.15f, lowTop, hw * 0.88f); p1 = float3(cabMaxX + 0.18f, lowTop, hw * 0.88f);
                p2 = float3(cabMaxX, vHei, hw * 0.85f); p3 = float3(cabMinX, vHei, hw * 0.85f);
                fn = float3(0.0f, 0.3f, 0.95f);
                col = float3(0.14f, 0.20f, 0.28f);
            }
            else if (quad == 9u)
            {
                p0 = float3(cabMaxX + 0.18f, lowTop, -hw * 0.88f); p1 = float3(cabMinX - 0.15f, lowTop, -hw * 0.88f);
                p2 = float3(cabMinX, vHei, -hw * 0.85f); p3 = float3(cabMaxX, vHei, -hw * 0.85f);
                fn = float3(0.0f, 0.3f, -0.95f);
                col = float3(0.14f, 0.20f, 0.28f);
            }
            // Quad 10: Bright Front Headlight Bar (+X face)
            else if (quad == 10u)
            {
                p0 = float3(hl + 0.04f, 0.30f, -hw * 0.82f); p1 = float3(hl + 0.04f, 0.30f, hw * 0.82f);
                p2 = float3(hl + 0.04f, 0.62f, hw * 0.82f); p3 = float3(hl + 0.04f, 0.62f, -hw * 0.82f);
                fn = float3(1.0f, 0.0f, 0.0f);
                col = float3(1.0f, 0.96f, 0.82f);
                emissive = float3(1.0f, 0.95f, 0.78f) * headIntensity;
            }
            // Quad 11: Glowing Ruby-Red Rear Taillight Bar (-X face)
            else
            {
                p0 = float3(-hl - 0.04f, 0.32f, hw * 0.82f); p1 = float3(-hl - 0.04f, 0.32f, -hw * 0.82f);
                p2 = float3(-hl - 0.04f, 0.64f, -hw * 0.82f); p3 = float3(-hl - 0.04f, 0.64f, hw * 0.82f);
                fn = float3(-1.0f, 0.0f, 0.0f);
                col = float3(1.0f, 0.14f, 0.10f);
                emissive = float3(1.0f, 0.12f, 0.08f) * tailIntensity;
            }

            float3 localP = (corner == 0u) ? p0 : ((corner == 1u) ? p1 : ((corner == 2u) ? p2 : p3));
            float cy = cos(yaw);
            float sy = sin(yaw);
            float3 rotP = float3(localP.x * cy - localP.z * sy, localP.y, localP.x * sy + localP.z * cy);
            float3 worldN = normalize(float3(fn.x * cy - fn.z * sy, fn.y, fn.x * sy + fn.z * cy));
            float4 wp = float4(inst.posAndYaw.x + rotP.x, inst.posAndYaw.y + rotP.y, inst.posAndYaw.z + rotP.z, 1.0f);

            CitySimpleVarying out;
            out.position = float4(dot(vpRow0, wp), dot(vpRow1, wp), dot(vpRow2, wp), dot(vpRow3, wp));

            float3 l = normalize(float3(sunDir.x, sunDir.y, sunDir.z));
            float ndotl = max(dot(worldN, l), 0.0f);
            float3 amb = float3(ambientColor.x, ambientColor.y, ambientColor.z);
            float3 sunC = float3(sunColor.x, sunColor.y, sunColor.z);
            float3 lit = col * (amb + sunC * ndotl * 0.85f) + emissive;
            float dist = length(float3(wp.x - eyePos.x, wp.y - eyePos.y, wp.z - eyePos.z));
            float fog = smoothstep(210.0f, 520.0f, dist);
            out.colorAndFog = float4(lit.x, lit.y, lit.z, fog);
            out.worldAndEmissive = float4(skyColor.x, skyColor.y, skyColor.z, 1.0f);
            return out;
        }

        CitySceneFrameBuffer fragment(CitySimpleVarying input)
        {
            float3 skyC = float3(input.worldAndEmissive.x, input.worldAndEmissive.y, input.worldAndEmissive.z);
            float3 finalRgb = lerp(
                float3(input.colorAndFog.x, input.colorAndFog.y, input.colorAndFog.z),
                skyC,
                input.colorAndFog.w);
            CitySceneFrameBuffer out;
            out.color = half4(half(clamp(finalRgb.x, 0.0f, 1.0f)), half(clamp(finalRgb.y, 0.0f, 1.0f)), half(clamp(finalRgb.z, 0.0f, 1.0f)), half(1.0f));
            return out;
        }
    };

    // =========================================================================
    // 3. Parallel RenderNodes (Eager LUT + 4x Quadrant Fissioned Compute + Forward Pass)
    // =========================================================================

    class BakeCityEnvironmentLutNode final : public IRenderNode<CityParams, CityEnvLutBuffer>
    {
    public:
        CityEnvLutBuffer build(RenderNode &node, IN CityParams params) const
        {
            auto lut = node.createBuffer<CityEnvLutBuffer>("CityEnvLut", 256u);
            node.computePass(
                "CityEnvLutBake",
                node.dispatchThreads<BakeCityEnvironmentLutCompute>(
                    lut, params.timeOfDay, params.timeSeconds, 256u, 1u, 1u));
            return lut;
        }
    };

    /// Uploads one urban quadrant's merged geometry & executes a 3-way fissioned computePass.
    class PrepareCityQuadrantGpuNode final : public IRenderNode<CityGeometryPack, CityQuadrantGpuPack>
    {
    public:
        CityQuadrantGpuPack build(
            RenderNode &node,
            IN CityParams params,
            IN CityEnvLutBuffer envLut,
            IN CityGeometryPack cpuPack) const
        {
            CityQuadrantGpuPack out;
            out.archVertexCount = uint(cpuPack.archVertices.size());
            out.vehicleCount = uint(cpuPack.vehicles.size());
            out.foliageLampCount = uint(cpuPack.foliageAndLamps.size());
            out.propCount = uint(cpuPack.propsAndSignage.size());

            const uint safeArch = out.archVertexCount > 0u ? out.archVertexCount : 1u;
            const uint safeVeh = out.vehicleCount > 0u ? out.vehicleCount : 1u;
            const uint safeFL = out.foliageLampCount > 0u ? out.foliageLampCount : 1u;
            const uint safeProps = out.propCount > 0u ? out.propCount : 1u;

            out.archVertices = node.createBuffer<CityArchVertexBuffer>("CityQuadArchVB", safeArch);
            auto rawVehicles = node.createBuffer<CityVehicleBuffer>("CityQuadRawVehicles", safeVeh);
            auto rawFL = node.createBuffer<CityFoliageLampBuffer>("CityQuadRawFoliageLamps", safeFL);
            auto rawProps = node.createBuffer<CityPropBuffer>("CityQuadRawProps", safeProps);

            out.preparedVehicles = node.createBuffer<CityVehicleBuffer>("CityQuadPrepVehicles", safeVeh);
            out.preparedFoliageAndLamps = node.createBuffer<CityFoliageLampBuffer>("CityQuadPrepFoliageLamps", safeFL);
            out.preparedPropsAndSignage = node.createBuffer<CityPropBuffer>("CityQuadPrepProps", safeProps);

            if (out.archVertexCount > 0u)
            {
                node.writeBuffer(out.archVertices, cpuPack.archVertices);
            }
            if (out.vehicleCount > 0u)
            {
                node.writeBuffer(rawVehicles, cpuPack.vehicles);
            }
            if (out.foliageLampCount > 0u)
            {
                node.writeBuffer(rawFL, cpuPack.foliageAndLamps);
            }
            if (out.propCount > 0u)
            {
                node.writeBuffer(rawProps, cpuPack.propsAndSignage);
            }

            // 3 independent dispatches writing to 3 disjoint buffers split into parallel passes.
            node.computePass(
                "CityDynamicPrepare",
                node.dispatchThreads<SimulateTrafficVehiclesCompute>(
                    rawVehicles, out.preparedVehicles, envLut,
                    out.vehicleCount, params.timeSeconds, params.trafficSpeed, safeVeh, 1u, 1u),
                node.dispatchThreads<PrepareStreetFoliageAndLampsCompute>(
                    rawFL, out.preparedFoliageAndLamps, envLut,
                    out.foliageLampCount, params.eyePosition, params.timeSeconds, safeFL, 1u, 1u),
                node.dispatchThreads<PrepareRooftopAndSignageCompute>(
                    rawProps, out.preparedPropsAndSignage, envLut,
                    out.propCount, params.timeSeconds, safeProps, 1u, 1u));

            return out;
        }
    };

    /// Renders all 4 prepared city quadrants into an RGBA8Unorm + Depth32Float forward pass.
    class RenderProceduralCityNode final : public IRenderNode<CityQuadrantGpuPack, CityColorTexture>
    {
    public:
        CityColorTexture build(
            RenderNode &node,
            IN CityParams params,
            IN CityQuadrantGpuPack q0,
            IN CityQuadrantGpuPack q1,
            IN CityQuadrantGpuPack q2,
            IN CityQuadrantGpuPack q3) const
        {
            const uint2 safeExtent = uint2(
                params.extent.x > 0u ? params.extent.x : 1u,
                params.extent.y > 0u ? params.extent.y : 1u);
            auto colorTex = node.createTexture<CityColorTexture>("ProceduralCityColor", safeExtent);
            auto depthTex = node.createTexture<CityDepthTexture>("ProceduralCityDepth", safeExtent);

            const CityAtmosphere atmo = evaluateCityAtmosphere(params.timeOfDay);

            CitySceneFrameBuffer fb;
            fb.color = node.view(colorTex);
            fb.color.loadOp = LoadOp::Clear;
            fb.color.clearValue = {atmo.skyColor.x, atmo.skyColor.y, atmo.skyColor.z, 1.0};
            fb.color.storeOp = StoreOp::Store;

            fb.depthStencil = node.view(depthTex);
            fb.depthStencil.depthLoadOp = LoadOp::Clear;
            fb.depthStencil.depthStoreOp = StoreOp::Store;
            fb.depthStencil.depthClearValue = 1.0f;

            const float4 r0 = params.viewProjRow0;
            const float4 r1 = params.viewProjRow1;
            const float4 r2 = params.viewProjRow2;
            const float4 r3 = params.viewProjRow3;
            const float4 eye = params.eyePosition;
            const float4 sun = atmo.sunDirection;
            const float4 skyC = atmo.skyColor;
            const float4 sunC = atmo.sunColor;
            const float4 ambC = atmo.ambientColor;
            const float dusk = atmo.duskFactor;
            const float tSec = params.timeSeconds;

            node.renderPass(
                "ProceduralCityForwardPass",
                fb,
                // Static architectural & infrastructure geometry (buildings, roads, canal, bridges)
                node.draw<CityArchitectureShader>(q0.archVertices, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, dusk, tSec, q0.archVertexCount, 1u, 0u, 0u),
                node.draw<CityArchitectureShader>(q1.archVertices, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, dusk, tSec, q1.archVertexCount, 1u, 0u, 0u),
                node.draw<CityArchitectureShader>(q2.archVertices, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, dusk, tSec, q2.archVertexCount, 1u, 0u, 0u),
                node.draw<CityArchitectureShader>(q3.archVertices, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, dusk, tSec, q3.archVertexCount, 1u, 0u, 0u),
                // Commercial neon signage, awnings, rooftop HVAC & aviation beacons (36 verts/prop)
                node.draw<CityPropAndSignageShader>(q0.preparedPropsAndSignage, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 36u, q0.propCount, 0u, 0u),
                node.draw<CityPropAndSignageShader>(q1.preparedPropsAndSignage, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 36u, q1.propCount, 0u, 0u),
                node.draw<CityPropAndSignageShader>(q2.preparedPropsAndSignage, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 36u, q2.propCount, 0u, 0u),
                node.draw<CityPropAndSignageShader>(q3.preparedPropsAndSignage, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 36u, q3.propCount, 0u, 0u),
                // Boulevard, park & courtyard trees + glowing streetlamps (48 verts/instance)
                node.draw<CityFoliageAndLampShader>(q0.preparedFoliageAndLamps, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 48u, q0.foliageLampCount, 0u, 0u),
                node.draw<CityFoliageAndLampShader>(q1.preparedFoliageAndLamps, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 48u, q1.foliageLampCount, 0u, 0u),
                node.draw<CityFoliageAndLampShader>(q2.preparedFoliageAndLamps, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 48u, q2.foliageLampCount, 0u, 0u),
                node.draw<CityFoliageAndLampShader>(q3.preparedFoliageAndLamps, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 48u, q3.foliageLampCount, 0u, 0u),
                // Live road & bridge traffic fleet (72 verts/vehicle)
                node.draw<CityVehicleShader>(q0.preparedVehicles, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 72u, q0.vehicleCount, 0u, 0u),
                node.draw<CityVehicleShader>(q1.preparedVehicles, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 72u, q1.vehicleCount, 0u, 0u),
                node.draw<CityVehicleShader>(q2.preparedVehicles, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 72u, q2.vehicleCount, 0u, 0u),
                node.draw<CityVehicleShader>(q3.preparedVehicles, r0, r1, r2, r3, eye, sun, skyC, sunC, ambC, 72u, q3.vehicleCount, 0u, 0u));

            return colorTex;
        }
    };

    /// Presents the rendered procedural metropolis to the swapchain and composites the ImGui HUD.
    class PresentCitySwapchainNode final : public IRenderNode<SwapchainQueryResult, uint>
    {
    public:
        uint build(RenderNode &node, IN SwapchainQueryResult swapchainTarget, IN CityColorTexture sceneColor) const
        {
            node.renderToSwapchain(swapchainTarget, sceneColor, RenderToSwapchainDescriptor{}, RenderImGui());
            return 1u;
        }
    };

    // =========================================================================
    // 4. Specialized Heterogeneous UGL [[Export]] Parcel Subgraphs
    // =========================================================================

    /// UGL Subgraph A: Downtown CBD Skyscraper block (massing -> facade/rooftop props -> sidewalk trees -> pack).
    [[Export]] inline auto buildSkyscraperDistrictSubgraph(IN CityBlockSpec spec)
    {
        TaskGraph graph;
        auto massing = graph.add(generateSkyscraperMassingCPU, spec);
        auto props = graph.add(generateSkyscraperFacadeAndRoofPropsCPU, spec, massing);
        auto furniture = graph.add(generateBlockSidewalkTreesAndLampsCPU, spec);
        return graph.add(packSkyscraperBlockGeometry, spec, massing, props, furniture);
    }

    /// UGL Subgraph B: Commercial Mid-Rise, Courtyard Residential & Park block (lot split -> massing -> signage/awnings -> trees -> pack).
    [[Export]] inline auto buildMidriseAndResidentialSubgraph(IN CityBlockSpec spec)
    {
        TaskGraph graph;
        auto lots = graph.add(splitUrbanBlockIntoLotsCPU, spec);
        auto buildings = graph.add(generateMidriseAndTownhouseMassingCPU, spec, lots);
        auto signage = graph.add(generateAwningsSignageAndRoofPropsCPU, spec, lots);
        auto foliage = graph.add(generateCourtyardParkTreesAndLampsCPU, spec, lots);
        return graph.add(packMidriseResBlockGeometry, spec, lots, buildings, signage, foliage);
    }

    /// UGL Subgraph C: Roadway, Canal & Arched Bridge corridor (mesh -> streetlamps -> lane vehicles -> pack).
    [[Export]] inline auto buildInfrastructureAndBridgeSubgraph(IN CityCorridorSpec spec)
    {
        TaskGraph graph;
        auto mesh = graph.add(generateRoadCanalBridgeMeshCPU, spec);
        auto lamps = graph.add(generateCorridorStreetlampsCPU, spec);
        auto vehicles = graph.add(generateCorridorLaneVehiclesCPU, spec);
        return graph.add(packInfrastructureGeometry, spec, mesh, lamps, vehicles);
    }

    /// UGL Exported single-quadrant GPU pipeline smoke entry (ensures UGLC emits all RenderNodes & ReadbackTextureNode).
    [[Export]] inline auto compileCityGpuReadbackPipeline(IN CityParams params)
    {
        TaskGraph graph;
        auto envLut = graph.add(BakeCityEnvironmentLutNode{}, params);
        CityBlockSpec sampleBlock;
        sampleBlock.params = params;
        auto massing = graph.add(generateSkyscraperMassingCPU, sampleBlock);
        auto props = graph.add(generateSkyscraperFacadeAndRoofPropsCPU, sampleBlock, massing);
        auto furn = graph.add(generateBlockSidewalkTreesAndLampsCPU, sampleBlock);
        auto pack = graph.add(packSkyscraperBlockGeometry, sampleBlock, massing, props, furn);
        auto gpuQuad = graph.add(PrepareCityQuadrantGpuNode{}, params, envLut, pack);
        auto summary = graph.add(summarizeCityQuadrants, pack, pack, pack, pack);
        auto color = graph.add(RenderProceduralCityNode{}, params, gpuQuad, gpuQuad, gpuQuad, gpuQuad);
        auto pixels = graph.add(ReadbackTextureNode<uint8_t>{}, color);
        return graph.add(evaluateCityBenchmarkPixels, summary, params, pixels);
    }

    /// UGL Exported swapchain pipeline smoke entry (ensures UGLC emits PresentCitySwapchainNode).
    [[Export]] inline auto compileCityGpuSwapchainPipeline(IN CitySwapchainRequest request)
    {
        TaskGraph graph;
        auto envLut = graph.add(BakeCityEnvironmentLutNode{}, request.params);
        CityBlockSpec sampleBlock;
        sampleBlock.params = request.params;
        auto massing = graph.add(generateSkyscraperMassingCPU, sampleBlock);
        auto props = graph.add(generateSkyscraperFacadeAndRoofPropsCPU, sampleBlock, massing);
        auto furn = graph.add(generateBlockSidewalkTreesAndLampsCPU, sampleBlock);
        auto pack = graph.add(packSkyscraperBlockGeometry, sampleBlock, massing, props, furn);
        auto gpuQuad = graph.add(PrepareCityQuadrantGpuNode{}, request.params, envLut, pack);
        auto summary = graph.add(summarizeCityQuadrants, pack, pack, pack, pack);
        auto color = graph.add(RenderProceduralCityNode{}, request.params, gpuQuad, gpuQuad, gpuQuad, gpuQuad);
        auto presented = graph.add(PresentCitySwapchainNode{}, request.swapchainTarget, color);
        return graph.add(finalizeCitySwapchainSummary, summary, presented);
    }
} // namespace ProceduralCitySample
