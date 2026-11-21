#pragma once
#include "PCGForestCpuNodes.hpp"

namespace PCGForestSample
{
    using TerrainVertexBuffer = Buffer<TerrainVertex, BufferUsage<Storage, CopyDst>>;
    using InstanceBuffer = Buffer<PCGInstanceData, BufferUsage<Storage, CopyDst>>;
    using WindLutBuffer = Buffer<float4, BufferUsage<Storage>>;
    using ForestColorTexture = Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>;
    using ForestDepthTexture = Texture<TextureFormat::Depth32Float,
        TextureUsage<RenderAttachment>, TextureDimension::e2D>;

    /// Windowed swapchain presentation request pairing the swapchain target with owned PCG/camera parameters.
    struct ForestSwapchainRequest
    {
        SwapchainQueryResult swapchainTarget;
        PCGForestParams params;
    };

    /// Execution-scoped GPU buffers produced in parallel for one 4-chunk world quadrant.
    struct QuadrantGpuPack
    {
        TerrainVertexBuffer terrainVertices;
        InstanceBuffer preparedTrees;
        InstanceBuffer preparedRocks;
        InstanceBuffer preparedGrass;
        uint terrainVertexCount = 0u;
        uint treeCount = 0u;
        uint rockCount = 0u;
        uint grassCount = 0u;
    };

    // =========================================================================
    // 1. GPU Compute Shaders (Eager Wind LUT + Auto-Split Instance Preparation)
    // =========================================================================

    /// Runs eagerly on the GPU while CPU worker threads / marl fibers are still scattering chunks.
    class [[LocalWorkGroupSize(64, 1, 1)]] BakeWindTurbulenceCompute final : public IComputeClass
    {
    public:
        constructor(RWStructuredBuffer<float4> windLut [[Args0]], float timeSeconds [[Args1]], float windStrength [[Args2]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < 256u)
            {
                float u = float(id.x) * (6.2831853f / 256.0f);
                float gustX = sin(u * 2.0f + timeSeconds * 1.8f) + 0.45f * cos(u * 5.0f - timeSeconds * 2.7f);
                float gustZ = cos(u * 1.5f + timeSeconds * 1.3f) + 0.35f * sin(u * 4.0f + timeSeconds * 2.1f);
                float flutter = 0.5f + 0.5f * sin(u * 7.0f + timeSeconds * 4.2f);
                windLut[id.x] = float4(gustX * windStrength, gustZ * windStrength, flutter, 1.0f);
            }
        }
    };

    /// Animates tree canopy sway and applies distance scale tapering.
    class [[LocalWorkGroupSize(64, 1, 1)]] PrepareTreeInstancesCompute final : public IComputeClass
    {
    public:
        constructor(
            StructuredBuffer<PCGInstanceData> rawTrees [[Args0]],
            RWStructuredBuffer<PCGInstanceData> outTrees [[Args1]],
            StructuredBuffer<float4> windLut [[Args2]],
            uint count [[Args3]],
            float4 eyePos [[Args4]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                PCGInstanceData inst = rawTrees[id.x];
                uint lutIdx = uint(abs(inst.posScale.x * 3.1f + inst.posScale.z * 2.7f)) & 255u;
                float4 gust = windLut[lutIdx];
                float dx = inst.posScale.x - eyePos.x;
                float dz = inst.posScale.z - eyePos.z;
                float dist = sqrt(dx * dx + dz * dz);
                float fade = 1.0f - smoothstep(190.0f, 250.0f, dist);
                inst.posScale.w = inst.posScale.w * fade;
                inst.rotWindTint.y = gust.x * 0.14f;
                outTrees[id.x] = inst;
            }
        }
    };

    /// Prepares understory rock instances with distance fade in an independent compute dispatch.
    class [[LocalWorkGroupSize(64, 1, 1)]] PrepareRockInstancesCompute final : public IComputeClass
    {
    public:
        constructor(
            StructuredBuffer<PCGInstanceData> rawRocks [[Args0]],
            RWStructuredBuffer<PCGInstanceData> outRocks [[Args1]],
            uint count [[Args2]],
            float4 eyePos [[Args3]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                PCGInstanceData inst = rawRocks[id.x];
                float dx = inst.posScale.x - eyePos.x;
                float dz = inst.posScale.z - eyePos.z;
                float dist = sqrt(dx * dx + dz * dz);
                inst.posScale.w = inst.posScale.w * (1.0f - smoothstep(175.0f, 235.0f, dist));
                outRocks[id.x] = inst;
            }
        }
    };

    /// Evaluates multi-harmonic meadow wind waves across tens of thousands of grass blades.
    class [[LocalWorkGroupSize(64, 1, 1)]] PrepareGrassInstancesCompute final : public IComputeClass
    {
    public:
        constructor(
            StructuredBuffer<PCGInstanceData> rawGrass [[Args0]],
            RWStructuredBuffer<PCGInstanceData> outGrass [[Args1]],
            StructuredBuffer<float4> windLut [[Args2]],
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
                PCGInstanceData inst = rawGrass[id.x];
                uint lutIdx = uint(abs(inst.posScale.x * 5.3f + inst.posScale.z * 4.1f)) & 255u;
                float4 gust = windLut[lutIdx];
                float wave = sin(inst.posScale.x * 0.22f + inst.posScale.z * 0.17f + timeSeconds * 3.1f);
                float dx = inst.posScale.x - eyePos.x;
                float dz = inst.posScale.z - eyePos.z;
                float dist = sqrt(dx * dx + dz * dz);
                float fade = 1.0f - smoothstep(115.0f, 175.0f, dist);
                inst.posScale.w = inst.posScale.w * fade;
                inst.rotWindTint.y = (gust.x * 0.35f + wave * 0.22f * gust.z);
                outGrass[id.x] = inst;
            }
        }
    };

    // =========================================================================
    // 2. Simple Forward 3D Raster Shaders (Terrain, Trees, Rocks, Grass Blades)
    // =========================================================================

    struct ForestSceneFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
        DepthStencilAttachment<TextureFormat::Depth32Float> depthStencil;
    };

    struct LitVarying
    {
        float4 position [[Position]];
        float4 colorAndFog [[Attribute0]];
    };

    /// Renders FBM terrain triangles with height/slope biome blending, CurveWave dirt road shading, and optional LOD tint.
    class TerrainForwardShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<TerrainVertex> vertices [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]],
            uint visualizeLodLevels [[Args7]])
        {
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(true);
            setCullMode(CullMode::None);
        }

    private:
        LitVarying vertex(uint vid [[VertexID]])
        {
            TerrainVertex v = vertices[vid];
            float4 worldPos = float4(v.posAndRoad.x, v.posAndRoad.y, v.posAndRoad.z, 1.0f);
            LitVarying out;
            out.position = float4(
                dot(vpRow0, worldPos),
                dot(vpRow1, worldPos),
                dot(vpRow2, worldPos),
                dot(vpRow3, worldPos));

            float3 n = normalize(float3(v.normalAndSlope.x, v.normalAndSlope.y, v.normalAndSlope.z));
            float3 l = normalize(float3(sunDir.x, sunDir.y, sunDir.z));
            float ndotl = max(dot(n, l), 0.0f);
            float hemi = 0.34f + 0.22f * (n.y * 0.5f + 0.5f);

            float h = v.posAndRoad.y;
            float slope = v.normalAndSlope.w;
            float road = v.posAndRoad.w;

            float3 lushGrass = float3(0.18f, 0.44f, 0.15f);
            float3 highlandMoss = float3(0.28f, 0.40f, 0.18f);
            float3 cliffRock = float3(0.42f, 0.39f, 0.35f);
            float3 snowCap = float3(0.84f, 0.87f, 0.90f);
            float3 trailEarth = float3(0.50f, 0.39f, 0.26f);

            float3 biome = lerp(lushGrass, highlandMoss, smoothstep(-2.0f, 12.0f, h));
            biome = lerp(biome, cliffRock, smoothstep(0.26f, 0.55f, slope));
            biome = lerp(biome, snowCap, smoothstep(17.5f, 23.5f, h) * (1.0f - slope));
            biome = lerp(biome, trailEarth, clamp(road * 0.92f, 0.0f, 1.0f));

            if (visualizeLodLevels != 0u)
            {
                float3 lodTint = float3(0.32f, 0.90f, 0.35f);
                if (v.lodInfo.x < 1.5f)
                {
                    lodTint = float3(0.95f, 0.62f, 0.20f);
                }
                else if (v.lodInfo.x < 2.5f)
                {
                    lodTint = float3(0.20f, 0.78f, 0.88f);
                }
                biome = lerp(biome, lodTint, 0.45f);
            }

            float3 lit = biome * (hemi + ndotl * 0.78f);
            float dist = length(float3(worldPos.x - eyePos.x, worldPos.y - eyePos.y, worldPos.z - eyePos.z));
            float fog = smoothstep(105.0f, 260.0f, dist);
            out.colorAndFog = float4(lit.x, lit.y, lit.z, fog);
            return out;
        }

        ForestSceneFrameBuffer fragment(LitVarying input)
        {
            float3 skyFog = float3(0.58f, 0.74f, 0.91f);
            float3 finalRgb = lerp(
                float3(input.colorAndFog.x, input.colorAndFog.y, input.colorAndFog.z),
                skyFog,
                input.colorAndFog.w);
            ForestSceneFrameBuffer out;
            out.color = half4(half(finalRgb.x), half(finalRgb.y), half(finalRgb.z), half(1.0f));
            return out;
        }
    };

    /// Renders procedural low-poly trunk + two-tier conifer canopy (18 vertices / 6 triangles per instance).
    class TreeForwardShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<PCGInstanceData> instances [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]])
        {
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(true);
            setCullMode(CullMode::None);
        }

    private:
        LitVarying vertex(uint vid [[VertexID]], uint iid [[InstanceID]])
        {
            PCGInstanceData inst = instances[iid];
            float scale = inst.posScale.w;
            float yaw = inst.rotWindTint.x;
            float sway = inst.rotWindTint.y;
            float tint = inst.rotWindTint.z;

            uint tri = vid / 3u;
            uint corner = vid % 3u;

            float3 localPos = float3(0.0f, 0.0f, 0.0f);
            float3 localNormal = float3(0.0f, 1.0f, 0.0f);
            float3 baseColor = float3(0.14f, 0.36f, 0.14f);

            if (tri < 2u)
            {
                // Crossed trunk fins (triangles 0 & 1)
                float planeAngle = float(tri) * 1.5707963f;
                float px = cos(planeAngle) * 0.24f;
                float pz = sin(planeAngle) * 0.24f;
                if (corner == 0u) { localPos = float3(-px, 0.0f, -pz); }
                else if (corner == 1u) { localPos = float3(px, 0.0f, pz); }
                else { localPos = float3(sway * 0.25f, 2.2f, 0.0f); }
                localNormal = normalize(float3(-pz, 0.25f, px));
                baseColor = lerp(float3(0.30f, 0.20f, 0.12f), float3(0.40f, 0.27f, 0.16f), tint);
            }
            else if (tri < 4u)
            {
                // Lower wide conifer canopy pyramid (crossed triangles 2 & 3)
                float planeAngle = float(tri - 2u) * 1.5707963f + 0.4f;
                float rx = cos(planeAngle) * 1.55f;
                float rz = sin(planeAngle) * 1.55f;
                if (corner == 0u) { localPos = float3(-rx + sway * 0.35f, 1.25f, -rz); }
                else if (corner == 1u) { localPos = float3(rx + sway * 0.35f, 1.25f, rz); }
                else { localPos = float3(sway * 0.85f, 4.15f, 0.0f); }
                localNormal = normalize(float3(localPos.x * 0.45f, 0.85f, localPos.z * 0.45f));
                baseColor = lerp(float3(0.10f, 0.31f, 0.12f), float3(0.20f, 0.44f, 0.16f), tint);
            }
            else
            {
                // Upper crown canopy pyramid (crossed triangles 4 & 5)
                float planeAngle = float(tri - 4u) * 1.5707963f + 1.1f;
                float rx = cos(planeAngle) * 1.08f;
                float rz = sin(planeAngle) * 1.08f;
                if (corner == 0u) { localPos = float3(-rx + sway * 0.7f, 2.95f, -rz); }
                else if (corner == 1u) { localPos = float3(rx + sway * 0.7f, 2.95f, rz); }
                else { localPos = float3(sway * 1.25f, 5.65f, 0.0f); }
                localNormal = normalize(float3(localPos.x * 0.5f, 0.9f, localPos.z * 0.5f));
                baseColor = lerp(float3(0.14f, 0.39f, 0.15f), float3(0.28f, 0.52f, 0.19f), tint);
            }

            float cy = cos(yaw);
            float sy = sin(yaw);
            float3 rotated = float3(
                localPos.x * cy - localPos.z * sy,
                localPos.y,
                localPos.x * sy + localPos.z * cy) * scale;
            float3 worldN = normalize(float3(
                localNormal.x * cy - localNormal.z * sy,
                localNormal.y,
                localNormal.x * sy + localNormal.z * cy));

            float4 worldPos = float4(
                inst.posScale.x + rotated.x,
                inst.posScale.y + rotated.y,
                inst.posScale.z + rotated.z,
                1.0f);

            LitVarying out;
            out.position = float4(
                dot(vpRow0, worldPos),
                dot(vpRow1, worldPos),
                dot(vpRow2, worldPos),
                dot(vpRow3, worldPos));

            float3 l = normalize(float3(sunDir.x, sunDir.y, sunDir.z));
            float diff = max(dot(worldN, l), 0.0f);
            float3 lit = baseColor * (0.40f + diff * 0.72f);
            float dist = length(float3(worldPos.x - eyePos.x, worldPos.y - eyePos.y, worldPos.z - eyePos.z));
            float fog = smoothstep(105.0f, 260.0f, dist);
            out.colorAndFog = float4(lit.x, lit.y, lit.z, fog);
            return out;
        }

        ForestSceneFrameBuffer fragment(LitVarying input)
        {
            float3 skyFog = float3(0.58f, 0.74f, 0.91f);
            float3 finalRgb = lerp(
                float3(input.colorAndFog.x, input.colorAndFog.y, input.colorAndFog.z),
                skyFog,
                input.colorAndFog.w);
            ForestSceneFrameBuffer out;
            out.color = half4(half(finalRgb.x), half(finalRgb.y), half(finalRgb.z), half(1.0f));
            return out;
        }
    };

    /// Renders 4-triangle faceted granite/moss boulders per instance.
    class RockForwardShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<PCGInstanceData> instances [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]])
        {
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(true);
            setCullMode(CullMode::None);
        }

    private:
        LitVarying vertex(uint vid [[VertexID]], uint iid [[InstanceID]])
        {
            PCGInstanceData inst = instances[iid];
            float scale = inst.posScale.w;
            float yaw = inst.rotWindTint.x;
            float tint = inst.rotWindTint.z;

            uint face = vid / 3u;
            uint corner = vid % 3u;
            float a0 = float(face) * 1.5707963f + yaw;
            float a1 = a0 + 1.5707963f;

            float3 p0 = float3(cos(a0) * 0.85f, -0.05f, sin(a0) * 0.75f);
            float3 p1 = float3(cos(a1) * 0.85f, -0.05f, sin(a1) * 0.75f);
            float3 apex = float3(0.08f * cos(yaw), 0.72f, -0.06f * sin(yaw));

            float3 local = corner == 0u ? p0 : (corner == 1u ? p1 : apex);
            float3 faceNormal = normalize(float3((p0.x + p1.x) * 0.5f, 0.65f, (p0.z + p1.z) * 0.5f));

            float4 worldPos = float4(
                inst.posScale.x + local.x * scale,
                inst.posScale.y + local.y * scale,
                inst.posScale.z + local.z * scale,
                1.0f);

            LitVarying out;
            out.position = float4(
                dot(vpRow0, worldPos),
                dot(vpRow1, worldPos),
                dot(vpRow2, worldPos),
                dot(vpRow3, worldPos));

            float3 warmRock = float3(0.49f, 0.46f, 0.42f);
            float3 mossyRock = float3(0.36f, 0.41f, 0.34f);
            float3 albedo = lerp(warmRock, mossyRock, tint);
            float3 l = normalize(float3(sunDir.x, sunDir.y, sunDir.z));
            float3 lit = albedo * (0.38f + max(dot(faceNormal, l), 0.0f) * 0.70f);
            float dist = length(float3(worldPos.x - eyePos.x, worldPos.y - eyePos.y, worldPos.z - eyePos.z));
            float fog = smoothstep(105.0f, 260.0f, dist);
            out.colorAndFog = float4(lit.x, lit.y, lit.z, fog);
            return out;
        }

        ForestSceneFrameBuffer fragment(LitVarying input)
        {
            float3 skyFog = float3(0.58f, 0.74f, 0.91f);
            float3 finalRgb = lerp(
                float3(input.colorAndFog.x, input.colorAndFog.y, input.colorAndFog.z),
                skyFog,
                input.colorAndFog.w);
            ForestSceneFrameBuffer out;
            out.color = half4(half(finalRgb.x), half(finalRgb.y), half(finalRgb.z), half(1.0f));
            return out;
        }
    };

    /// Renders swaying tapered 3-vertex grass blades per instance with root-to-tip gradient shading.
    class GrassForwardShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<PCGInstanceData> instances [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]])
        {
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(true);
            setCullMode(CullMode::None);
        }

    private:
        LitVarying vertex(uint vid [[VertexID]], uint iid [[InstanceID]])
        {
            PCGInstanceData inst = instances[iid];
            float scale = inst.posScale.w;
            float yaw = inst.rotWindTint.x;
            float sway = inst.rotWindTint.y;
            float tint = inst.rotWindTint.z;
            float shade = inst.rotWindTint.w;

            float halfWidth = 0.085f * scale;
            float bladeHeight = 0.78f * scale;
            float dx = cos(yaw) * halfWidth;
            float dz = sin(yaw) * halfWidth;

            float3 offset = float3(0.0f, 0.0f, 0.0f);
            float tipFactor = 0.0f;
            if (vid == 0u)
            {
                offset = float3(-dx, 0.0f, -dz);
            }
            else if (vid == 1u)
            {
                offset = float3(dx, 0.0f, dz);
            }
            else
            {
                offset = float3(sway * scale, bladeHeight, sway * 0.45f * scale);
                tipFactor = 1.0f;
            }

            float4 worldPos = float4(
                inst.posScale.x + offset.x,
                inst.posScale.y + offset.y,
                inst.posScale.z + offset.z,
                1.0f);

            LitVarying out;
            out.position = float4(
                dot(vpRow0, worldPos),
                dot(vpRow1, worldPos),
                dot(vpRow2, worldPos),
                dot(vpRow3, worldPos));

            float3 rootColor = float3(0.11f, 0.28f, 0.09f);
            float3 tipMeadow = lerp(float3(0.28f, 0.58f, 0.16f), float3(0.46f, 0.64f, 0.20f), tint);
            float3 bladeColor = lerp(rootColor, tipMeadow, tipFactor);
            float sunBoost = 0.62f + 0.38f * max(sunDir.y, 0.2f);
            float canopyFactor = 0.68f + 0.32f * shade;
            float3 lit = bladeColor * sunBoost * canopyFactor;

            float dist = length(float3(worldPos.x - eyePos.x, worldPos.y - eyePos.y, worldPos.z - eyePos.z));
            float fog = smoothstep(105.0f, 260.0f, dist);
            out.colorAndFog = float4(lit.x, lit.y, lit.z, fog);
            return out;
        }

        ForestSceneFrameBuffer fragment(LitVarying input)
        {
            float3 skyFog = float3(0.58f, 0.74f, 0.91f);
            float3 finalRgb = lerp(
                float3(input.colorAndFog.x, input.colorAndFog.y, input.colorAndFog.z),
                skyFog,
                input.colorAndFog.w);
            ForestSceneFrameBuffer out;
            out.color = half4(half(finalRgb.x), half(finalRgb.y), half(finalRgb.z), half(1.0f));
            return out;
        }
    };

    // =========================================================================
    // 3. Parallel RenderNodes (Eager Wind Bake + 4 Quadrant GPU Prep + Forward Pass)
    // =========================================================================

    /// Bakes the 256-entry GPU wind harmonic LUT concurrently with CPU chunk scatter.
    class BakeEnvironmentLutNode final : public IRenderNode<PCGForestParams, WindLutBuffer>
    {
    public:
        WindLutBuffer build(RenderNode &node, IN PCGForestParams params) const
        {
            auto lut = node.createBuffer<WindLutBuffer>("PCGWindLut", 256u);
            node.computePass(
                "PCGWindLutBake",
                node.dispatchThreads<BakeWindTurbulenceCompute>(
                    lut, params.timeSeconds, params.windStrength, 256u, 1u, 1u));
            return lut;
        }
    };

    /// Uploads one world quadrant's merged quadtree geometry and runs an auto-split computePass across Trees, Rocks, and Grass.
    class PrepareQuadrantGpuNode final : public IRenderNode<ChunkGeometryPack, QuadrantGpuPack>
    {
    public:
        QuadrantGpuPack build(
            RenderNode &node,
            IN PCGForestParams params,
            IN WindLutBuffer windLut,
            IN ChunkGeometryPack cpuPack) const
        {
            QuadrantGpuPack out;
            out.terrainVertexCount = uint(cpuPack.terrainVertices.size());
            out.treeCount = uint(cpuPack.trees.size());
            out.rockCount = uint(cpuPack.rocks.size());
            out.grassCount = uint(cpuPack.grass.size());

            const uint safeVerts = out.terrainVertexCount > 0u ? out.terrainVertexCount : 1u;
            const uint safeTrees = out.treeCount > 0u ? out.treeCount : 1u;
            const uint safeRocks = out.rockCount > 0u ? out.rockCount : 1u;
            const uint safeGrass = out.grassCount > 0u ? out.grassCount : 1u;

            out.terrainVertices = node.createBuffer<TerrainVertexBuffer>("PCGQuadTerrainVB", safeVerts);
            auto rawTrees = node.createBuffer<InstanceBuffer>("PCGQuadRawTrees", safeTrees);
            auto rawRocks = node.createBuffer<InstanceBuffer>("PCGQuadRawRocks", safeRocks);
            auto rawGrass = node.createBuffer<InstanceBuffer>("PCGQuadRawGrass", safeGrass);

            out.preparedTrees = node.createBuffer<InstanceBuffer>("PCGQuadPreparedTrees", safeTrees);
            out.preparedRocks = node.createBuffer<InstanceBuffer>("PCGQuadPreparedRocks", safeRocks);
            out.preparedGrass = node.createBuffer<InstanceBuffer>("PCGQuadPreparedGrass", safeGrass);

            if (out.terrainVertexCount > 0u)
            {
                node.writeBuffer(out.terrainVertices, cpuPack.terrainVertices);
            }
            if (out.treeCount > 0u)
            {
                node.writeBuffer(rawTrees, cpuPack.trees);
            }
            if (out.rockCount > 0u)
            {
                node.writeBuffer(rawRocks, cpuPack.rocks);
            }
            if (out.grassCount > 0u)
            {
                node.writeBuffer(rawGrass, cpuPack.grass);
            }

            // Single logical computePass with 3 disjoint output buffers split into independent passes.
            node.computePass(
                "PCGInstancePrepare",
                node.dispatchThreads<PrepareTreeInstancesCompute>(
                    rawTrees, out.preparedTrees, windLut, out.treeCount, params.eyePosition, safeTrees, 1u, 1u),
                node.dispatchThreads<PrepareRockInstancesCompute>(
                    rawRocks, out.preparedRocks, out.rockCount, params.eyePosition, safeRocks, 1u, 1u),
                node.dispatchThreads<PrepareGrassInstancesCompute>(
                    rawGrass, out.preparedGrass, windLut, out.grassCount,
                    params.eyePosition, params.timeSeconds, safeGrass, 1u, 1u));

            return out;
        }
    };

    /// Joins all 4 prepared GPU quadrants into a single forward 3D pass with hardware depth testing.
    class RenderPCGForestSceneNode final : public IRenderNode<QuadrantGpuPack, ForestColorTexture>
    {
    public:
        ForestColorTexture build(
            RenderNode &node,
            IN PCGForestParams params,
            IN QuadrantGpuPack q0,
            IN QuadrantGpuPack q1,
            IN QuadrantGpuPack q2,
            IN QuadrantGpuPack q3) const
        {
            const uint2 safeExtent = uint2(
                params.extent.x > 0u ? params.extent.x : 1u,
                params.extent.y > 0u ? params.extent.y : 1u);
            auto colorTex = node.createTexture<ForestColorTexture>("PCGForestColor", safeExtent);
            auto depthTex = node.createTexture<ForestDepthTexture>("PCGForestDepth", safeExtent);

            ForestSceneFrameBuffer fb;
            fb.color = node.view(colorTex);
            fb.color.loadOp = LoadOp::Clear;
            fb.color.clearValue = {0.58, 0.74, 0.91, 1.0};
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
            const float4 sun = params.sunDirection;
            const uint visLod = params.visualizeLodLevels;

            node.renderPass(
                "PCGForestForwardPass",
                fb,
                // Terrain draws across 4 quadrants
                node.draw<TerrainForwardShader>(q0.terrainVertices, r0, r1, r2, r3, eye, sun, visLod, q0.terrainVertexCount, 1u, 0u, 0u),
                node.draw<TerrainForwardShader>(q1.terrainVertices, r0, r1, r2, r3, eye, sun, visLod, q1.terrainVertexCount, 1u, 0u, 0u),
                node.draw<TerrainForwardShader>(q2.terrainVertices, r0, r1, r2, r3, eye, sun, visLod, q2.terrainVertexCount, 1u, 0u, 0u),
                node.draw<TerrainForwardShader>(q3.terrainVertices, r0, r1, r2, r3, eye, sun, visLod, q3.terrainVertexCount, 1u, 0u, 0u),
                // Forest tree canopies & trunks (18 verts/tree)
                node.draw<TreeForwardShader>(q0.preparedTrees, r0, r1, r2, r3, eye, sun, 18u, q0.treeCount, 0u, 0u),
                node.draw<TreeForwardShader>(q1.preparedTrees, r0, r1, r2, r3, eye, sun, 18u, q1.treeCount, 0u, 0u),
                node.draw<TreeForwardShader>(q2.preparedTrees, r0, r1, r2, r3, eye, sun, 18u, q2.treeCount, 0u, 0u),
                node.draw<TreeForwardShader>(q3.preparedTrees, r0, r1, r2, r3, eye, sun, 18u, q3.treeCount, 0u, 0u),
                // Understory boulders (12 verts/rock)
                node.draw<RockForwardShader>(q0.preparedRocks, r0, r1, r2, r3, eye, sun, 12u, q0.rockCount, 0u, 0u),
                node.draw<RockForwardShader>(q1.preparedRocks, r0, r1, r2, r3, eye, sun, 12u, q1.rockCount, 0u, 0u),
                node.draw<RockForwardShader>(q2.preparedRocks, r0, r1, r2, r3, eye, sun, 12u, q2.rockCount, 0u, 0u),
                node.draw<RockForwardShader>(q3.preparedRocks, r0, r1, r2, r3, eye, sun, 12u, q3.rockCount, 0u, 0u),
                // Clustered swaying grass blades (3 verts/blade)
                node.draw<GrassForwardShader>(q0.preparedGrass, r0, r1, r2, r3, eye, sun, 3u, q0.grassCount, 0u, 0u),
                node.draw<GrassForwardShader>(q1.preparedGrass, r0, r1, r2, r3, eye, sun, 3u, q1.grassCount, 0u, 0u),
                node.draw<GrassForwardShader>(q2.preparedGrass, r0, r1, r2, r3, eye, sun, 3u, q2.grassCount, 0u, 0u),
                node.draw<GrassForwardShader>(q3.preparedGrass, r0, r1, r2, r3, eye, sun, 3u, q3.grassCount, 0u, 0u));

            return colorTex;
        }
    };

    /// Presents the rendered PCG forest scene to the window swapchain and composites the ImGui HUD.
    class PresentForestSwapchainNode final : public IRenderNode<SwapchainQueryResult, uint>
    {
    public:
        uint build(RenderNode &node, IN SwapchainQueryResult swapchainTarget, IN ForestColorTexture sceneColor) const
        {
            node.renderToSwapchain(swapchainTarget, sceneColor, RenderToSwapchainDescriptor{}, RenderImGui());
            return 1u;
        }
    };

    // =========================================================================
    // 4. Exported Adaptive Quadtree Leaf PCG Subgraph + GPU Verification Entry
    // =========================================================================

    /// Exported UGL leaf PCG subgraph executed recursively per quadtree leaf chunk.
    [[Export]] inline auto buildQuadtreeLeafChunk(IN QuadtreeLeafInput input)
    {
        TaskGraph graph;

        GenerateQuadtreeLeafTerrainCPU terrainGen;
        terrainGen.leaf = input;
        auto terrain = graph.add(terrainGen, input.params);

        ScatterForestObstaclesCPU treeGen;
        treeGen.leaf = input;
        auto obstacles = graph.add(treeGen, input.params, terrain);

        ScatterUnderstoryRocksCPU rockGen;
        rockGen.leaf = input;
        auto rocks = graph.add(rockGen, input.params, terrain, obstacles);

        ScatterClusteredGrassCPU grassGen;
        grassGen.leaf = input;
        auto grass = graph.add(grassGen, input.params, terrain, obstacles);

        return graph.add(packChunkGeometry, terrain, obstacles, rocks, grass, input.depth);
    }

    struct PrebuiltForestQuadrants
    {
        PCGForestParams params;
        ChunkGeometryPack q0;
        ChunkGeometryPack q1;
        ChunkGeometryPack q2;
        ChunkGeometryPack q3;
        uint nestedSubgraphCount = 0u;
    };

    /// Exported GPU render subgraph validating all RenderNodes with UGLC.
    [[Export]] inline auto renderPrebuiltForestBenchmarkFrame(IN PrebuiltForestQuadrants input)
    {
        TaskGraph graph;
        auto windLut = graph.add(BakeEnvironmentLutNode{}, input.params);
        auto gq0 = graph.add(PrepareQuadrantGpuNode{}, input.params, windLut, input.q0);
        auto gq1 = graph.add(PrepareQuadrantGpuNode{}, input.params, windLut, input.q1);
        auto gq2 = graph.add(PrepareQuadrantGpuNode{}, input.params, windLut, input.q2);
        auto gq3 = graph.add(PrepareQuadrantGpuNode{}, input.params, windLut, input.q3);
        auto summary = graph.add(
            summarizeQuadrants, input.q0, input.q1, input.q2, input.q3, input.nestedSubgraphCount);
        auto sceneColor = graph.add(
            RenderPCGForestSceneNode{}, input.params, gq0, gq1, gq2, gq3);
        auto pixels = graph.add(ReadbackTextureNode<uint8_t>{}, sceneColor);
        return graph.add(evaluateBenchmarkPixels, summary, pixels);
    }
} // namespace PCGForestSample
