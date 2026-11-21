#pragma once
#include "SparseVoxelCpuNodes.hpp"

namespace SparseVoxelSample
{
    using VoxelQuadBuffer = Buffer<VoxelQuad, BufferUsage<Storage, CopyDst>>;
    using AtmosLutBuffer = Buffer<float4, BufferUsage<Storage>>;
    using VoxelColorTexture = Texture<TextureFormat::RGBA8Unorm,
        TextureUsage<RenderAttachment, TextureBinding, CopySrc>, TextureDimension::e2D>;
    using VoxelDepthTexture = Texture<TextureFormat::Depth32Float,
        TextureUsage<RenderAttachment>, TextureDimension::e2D>;

    /// Execution-scoped GPU buffers for one world quadrant (opaque, foliage/emissive, water).
    struct VoxelQuadrantGpuPack
    {
        VoxelQuadBuffer opaqueQuads;
        VoxelQuadBuffer foliageQuads;
        VoxelQuadBuffer waterQuads;
        uint opaqueQuadCount = 0u;
        uint foliageQuadCount = 0u;
        uint waterQuadCount = 0u;
    };

    /// Prebuilt world quadrants passed into the GPU verification / swapchain render graph.
    struct PrebuiltVoxelScene
    {
        SparseVoxelParams params;
        VoxelQuadrantPack q0;
        VoxelQuadrantPack q1;
        VoxelQuadrantPack q2;
        VoxelQuadrantPack q3;
        uint octreeInteriorCount = 0u;
    };

    struct PrebuiltVoxelSwapchainScene
    {
        SwapchainQueryResult swapchainTarget;
        SparseVoxelParams params;
        VoxelQuadrantPack q0;
        VoxelQuadrantPack q1;
        VoxelQuadrantPack q2;
        VoxelQuadrantPack q3;
        uint octreeInteriorCount = 0u;
    };

    // =========================================================================
    // 1. GPU Compute Shaders (Eager Atmosphere LUT + Fissioned Quadrant Prep)
    // =========================================================================

    /// Bakes a 256-entry atmosphere, foliage wind sway, emissive crystal pulse & water ripple LUT.
    class [[LocalWorkGroupSize(64, 1, 1)]] BakeVoxelAtmosphereLutCompute final : public IComputeClass
    {
    public:
        constructor(
            RWStructuredBuffer<float4> atmosLut [[Args0]],
            float timeSeconds [[Args1]],
            float waterRippleStrength [[Args2]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < 256u)
            {
                float u = float(id.x) * (6.2831853f / 256.0f);
                float sway = sin(u * 2.0f + timeSeconds * 2.1f) + 0.45f * cos(u * 5.0f - timeSeconds * 3.2f);
                float pulse = 0.78f + 0.22f * sin(u * 3.0f + timeSeconds * 2.6f);
                float ripple = sin(u * 4.0f + timeSeconds * 3.8f) * waterRippleStrength;
                float shimmer = 0.5f + 0.5f * cos(u * 6.0f - timeSeconds * 4.5f);
                atmosLut[id.x] = float4(sway, pulse, ripple, shimmer);
            }
        }
    };

    /// Dispatch #0 in fissioned computePass: prepares opaque terrain/structure voxel quads.
    class [[LocalWorkGroupSize(64, 1, 1)]] PrepareOpaqueVoxelQuadsCompute final : public IComputeClass
    {
    public:
        constructor(
            StructuredBuffer<VoxelQuad> rawQuads [[Args0]],
            RWStructuredBuffer<VoxelQuad> outQuads [[Args1]],
            StructuredBuffer<float4> atmosLut [[Args2]],
            uint count [[Args3]],
            float4 eyePos [[Args4]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                VoxelQuad q = rawQuads[id.x];
                uint lutIdx = uint(abs(q.originAndAnim.x * 3.0f + q.originAndAnim.z * 5.0f)) & 255u;
                float4 lut = atmosLut[lutIdx];
                float dx = q.originAndAnim.x - eyePos.x;
                float dz = q.originAndAnim.z - eyePos.z;
                float dist = sqrt(dx * dx + dz * dz);
                q.originAndAnim.w = dist;
                q.meta.z = uint(clamp(lut.y * 1000.0f, 0.0f, 2000.0f));
                outQuads[id.x] = q;
            }
        }
    };

    /// Dispatch #1 in fissioned computePass: animates cherry-blossom/pine wind flutter and crystal/lantern pulse.
    class [[LocalWorkGroupSize(64, 1, 1)]] PrepareFoliageEmissiveQuadsCompute final : public IComputeClass
    {
    public:
        constructor(
            StructuredBuffer<VoxelQuad> rawQuads [[Args0]],
            RWStructuredBuffer<VoxelQuad> outQuads [[Args1]],
            StructuredBuffer<float4> atmosLut [[Args2]],
            uint count [[Args3]],
            float4 eyePos [[Args4]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                VoxelQuad q = rawQuads[id.x];
                uint lutIdx = uint(abs(q.originAndAnim.x * 7.0f + q.originAndAnim.y * 3.0f + q.originAndAnim.z * 5.0f)) & 255u;
                float4 lut = atmosLut[lutIdx];
                float dx = q.originAndAnim.x - eyePos.x;
                float dz = q.originAndAnim.z - eyePos.z;
                float dist = sqrt(dx * dx + dz * dz);
                q.originAndAnim.w = lut.x;
                q.meta.z = uint(clamp(lut.y * 1000.0f, 0.0f, 2000.0f));
                q.meta.w = uint(clamp(dist * 10.0f, 0.0f, 10000.0f));
                outQuads[id.x] = q;
            }
        }
    };

    /// Dispatch #2 in fissioned computePass: evaluates turquoise river ripple and waterfall foam shimmer.
    class [[LocalWorkGroupSize(64, 1, 1)]] PrepareWaterVoxelQuadsCompute final : public IComputeClass
    {
    public:
        constructor(
            StructuredBuffer<VoxelQuad> rawQuads [[Args0]],
            RWStructuredBuffer<VoxelQuad> outQuads [[Args1]],
            StructuredBuffer<float4> atmosLut [[Args2]],
            uint count [[Args3]])
        {
        }

    private:
        void compute(uint3 id [[DispatchThreadID]])
        {
            if (id.x < count)
            {
                VoxelQuad q = rawQuads[id.x];
                uint lutIdx = uint(abs(q.originAndAnim.x * 9.0f + q.originAndAnim.z * 11.0f)) & 255u;
                float4 lut = atmosLut[lutIdx];
                q.originAndAnim.w = lut.z * 0.032f;
                q.meta.z = uint(clamp(lut.w * 1000.0f, 0.0f, 2000.0f));
                outQuads[id.x] = q;
            }
        }
    };

    // =========================================================================
    // 2. Forward Micro-Voxel Raster Shaders (Opaque/Emissive + Translucent Water)
    // =========================================================================

    struct VoxelSceneFrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
        DepthStencilAttachment<TextureFormat::Depth32Float> depthStencil;
    };

    struct VoxelVarying
    {
        float4 position [[Position]];
        float4 worldPosAndAo [[Attribute0]];
        float4 normalAndMat [[Attribute1]];
        float4 glowAndAnim [[Attribute2]];
        float4 eyeAndVoxelSize [[Attribute3]];
        float4 sunAndAux [[Attribute4]];
    };

    /// Renders greedy-merged 0.125m micro-voxels with 4-corner AO, per-voxel micro-tint & emissive glow.
    class VoxelOpaqueForwardShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<VoxelQuad> quads [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]],
            float voxelSize [[Args7]],
            float emissiveGlowScale [[Args8]],
            uint isFoliagePass [[Args9]])
        {
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(true);
            setCullMode(CullMode::None);
        }

    private:
        VoxelVarying vertex(uint vid [[VertexID]])
        {
            uint quadIdx = vid / 6u;
            uint triCorner = vid % 6u;

            VoxelQuad q = quads[quadIdx];
            uint m0 = q.meta.x;
            uint m1 = q.meta.y;

            uint dir = m0 & 7u;
            uint matId = (m0 >> 3u) & 31u;
            uint ao00 = (m0 >> 8u) & 3u;
            uint ao10 = (m0 >> 10u) & 3u;
            uint ao01 = (m0 >> 12u) & 3u;
            uint ao11 = (m0 >> 14u) & 3u;
            uint flip = (m0 >> 16u) & 1u;
            uint uSpan = ((m0 >> 19u) & 15u) + 1u;
            uint vSpan = ((m0 >> 23u) & 15u) + 1u;

            uint glowLevel = m1 & 7u;
            uint glowPalette = (m1 >> 3u) & 3u;

            uint cu = 0u;
            uint cv = 0u;
            if (flip == 0u)
            {
                if (triCorner == 0u || triCorner == 3u) { cu = 0u; cv = 0u; }
                else if (triCorner == 1u) { cu = 1u; cv = 0u; }
                else if (triCorner == 2u || triCorner == 4u) { cu = 1u; cv = 1u; }
                else { cu = 0u; cv = 1u; }
            }
            else
            {
                if (triCorner == 0u || triCorner == 3u) { cu = 1u; cv = 0u; }
                else if (triCorner == 1u) { cu = 1u; cv = 1u; }
                else if (triCorner == 2u || triCorner == 4u) { cu = 0u; cv = 1u; }
                else { cu = 0u; cv = 0u; }
            }

            uint cornerAo = (cu == 0u) ? ((cv == 0u) ? ao00 : ao01) : ((cv == 0u) ? ao10 : ao11);
            float aoFactor = 0.40f + 0.20f * float(cornerAo);

            float3 nDir = float3(0.0f, 1.0f, 0.0f);
            float3 uDir = float3(1.0f, 0.0f, 0.0f);
            float3 vDir = float3(0.0f, 0.0f, 1.0f);

            if (dir == 0u)      { nDir = float3( 1.0f, 0.0f, 0.0f); uDir = float3(0.0f, 0.0f, 1.0f); vDir = float3(0.0f, 1.0f, 0.0f); }
            else if (dir == 1u) { nDir = float3(-1.0f, 0.0f, 0.0f); uDir = float3(0.0f, 0.0f, 1.0f); vDir = float3(0.0f, 1.0f, 0.0f); }
            else if (dir == 2u) { nDir = float3( 0.0f, 1.0f, 0.0f); uDir = float3(1.0f, 0.0f, 0.0f); vDir = float3(0.0f, 0.0f, 1.0f); }
            else if (dir == 3u) { nDir = float3( 0.0f,-1.0f, 0.0f); uDir = float3(1.0f, 0.0f, 0.0f); vDir = float3(0.0f, 0.0f, 1.0f); }
            else if (dir == 4u) { nDir = float3( 0.0f, 0.0f, 1.0f); uDir = float3(1.0f, 0.0f, 0.0f); vDir = float3(0.0f, 1.0f, 0.0f); }
            else                { nDir = float3( 0.0f, 0.0f,-1.0f); uDir = float3(1.0f, 0.0f, 0.0f); vDir = float3(0.0f, 1.0f, 0.0f); }

            float3 worldPos = q.originAndAnim.xyz +
                uDir * (float(cu * uSpan) * voxelSize) +
                vDir * (float(cv * vSpan) * voxelSize);

            if (isFoliagePass != 0u && (matId == 10u || matId == 11u))
            {
                float sway = q.originAndAnim.w * 0.035f;
                worldPos.x += sway * (0.35f + 0.65f * float(cv));
                worldPos.z += sway * 0.45f * float(cu);
            }

            float4 wp4 = float4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
            VoxelVarying out;
            out.position = float4(
                dot(vpRow0, wp4),
                dot(vpRow1, wp4),
                dot(vpRow2, wp4),
                dot(vpRow3, wp4));
            out.worldPosAndAo = float4(worldPos.x, worldPos.y, worldPos.z, aoFactor);
            out.normalAndMat = float4(nDir.x, nDir.y, nDir.z, float(matId) + 0.5f);
            out.glowAndAnim = float4(
                float(glowLevel) / 7.0f,
                float(glowPalette) + 0.5f,
                float(q.meta.z) * 0.001f,
                emissiveGlowScale);
            out.eyeAndVoxelSize = float4(eyePos.x, eyePos.y, eyePos.z, voxelSize);
            out.sunAndAux = float4(sunDir.x, sunDir.y, sunDir.z, float(isFoliagePass));
            return out;
        }

        VoxelSceneFrameBuffer fragment(VoxelVarying input)
        {
            float3 n = normalize(float3(input.normalAndMat.x, input.normalAndMat.y, input.normalAndMat.z));
            float3 wp = float3(input.worldPosAndAo.x, input.worldPosAndAo.y, input.worldPosAndAo.z);
            float ao = input.worldPosAndAo.w;
            uint matId = uint(input.normalAndMat.w);
            float vs = max(input.eyeAndVoxelSize.w, 0.05f);

            // World-space per-micro-voxel hash preserves crisp 12.5cm voxel detail across greedy-merged quads.
            float3 samplePos = wp - n * (vs * 0.25f);
            float3 vCell = floor(samplePos / vs + 0.001f);
            float hDot = dot(vCell, float3(12.9898f, 78.233f, 37.719f));
            float sVal = sin(hDot) * 43758.5453f;
            float microHash = sVal - floor(sVal);
            float microShift = (microHash - 0.5f) * 0.10f;

            float3 albedo = float3(0.70f, 0.65f, 0.55f);
            float3 selfEmissive = float3(0.0f, 0.0f, 0.0f);

            if (matId == 1u) // Limestone Light
            {
                albedo = float3(0.76f, 0.69f, 0.56f) + microShift * 0.8f;
            }
            else if (matId == 2u) // Limestone Dark
            {
                albedo = float3(0.54f, 0.45f, 0.37f) + microShift * 0.7f;
            }
            else if (matId == 3u) // Mossy Grass
            {
                albedo = lerp(float3(0.21f, 0.52f, 0.18f), float3(0.35f, 0.64f, 0.24f), microHash);
            }
            else if (matId == 6u) // Bridge & Stepping Stone
            {
                albedo = lerp(float3(0.46f, 0.47f, 0.45f), float3(0.58f, 0.58f, 0.53f), microHash);
            }
            else if (matId == 7u) // Pagoda Wood
            {
                albedo = float3(0.44f, 0.16f, 0.11f) + microShift * 0.4f;
            }
            else if (matId == 8u) // Pagoda Glazed Roof
            {
                albedo = lerp(float3(0.14f, 0.44f, 0.43f), float3(0.20f, 0.56f, 0.52f), microHash);
            }
            else if (matId == 9u) // Tree Bark
            {
                albedo = float3(0.31f, 0.21f, 0.14f) + microShift * 0.6f;
            }
            else if (matId == 10u) // Sakura Blossom
            {
                albedo = lerp(float3(0.95f, 0.58f, 0.73f), float3(1.00f, 0.80f, 0.88f), microHash);
            }
            else if (matId == 11u) // Pine Needle
            {
                albedo = lerp(float3(0.10f, 0.31f, 0.14f), float3(0.19f, 0.45f, 0.21f), microHash);
            }
            else if (matId == 12u) // Glowing Crystal Cyan
            {
                albedo = float3(0.22f, 0.94f, 1.00f);
                selfEmissive = float3(0.25f, 0.96f, 1.00f) * (0.95f * input.glowAndAnim.z * input.glowAndAnim.w);
            }
            else if (matId == 13u) // Glowing Crystal Amethyst
            {
                albedo = float3(0.86f, 0.34f, 1.00f);
                selfEmissive = float3(0.90f, 0.32f, 1.00f) * (0.95f * input.glowAndAnim.z * input.glowAndAnim.w);
            }
            else if (matId == 14u) // Lantern Stone Frame
            {
                albedo = float3(0.27f, 0.28f, 0.30f) + microShift * 0.5f;
            }
            else if (matId == 15u) // Lantern Warm Glow Core
            {
                albedo = float3(1.00f, 0.82f, 0.32f);
                selfEmissive = float3(1.00f, 0.74f, 0.22f) * (1.15f * input.glowAndAnim.z * input.glowAndAnim.w);
            }
            else if (matId == 16u) // Mushroom Stem
            {
                albedo = float3(0.87f, 0.82f, 0.72f) + microShift * 0.4f;
            }
            else if (matId == 17u) // Bioluminescent Mushroom Cap
            {
                bool isSpot = microHash > 0.78f;
                albedo = isSpot ? float3(1.00f, 0.84f, 0.92f) : float3(0.92f, 0.28f, 0.56f);
                selfEmissive = (isSpot ? float3(1.00f, 0.72f, 0.86f) : float3(0.75f, 0.18f, 0.44f)) *
                    (0.55f * input.glowAndAnim.z * input.glowAndAnim.w);
            }
            else if (matId == 18u) // Ruin Pillar
            {
                albedo = lerp(float3(0.58f, 0.55f, 0.46f), float3(0.42f, 0.48f, 0.36f), step(0.72f, microHash));
            }

            float3 sunDir = normalize(float3(input.sunAndAux.x, input.sunAndAux.y, input.sunAndAux.z));
            float ndotl = max(dot(n, sunDir), 0.0f);
            float faceShade = 0.88f + 0.12f * n.y + 0.06f * n.x;
            float hemi = (0.34f + 0.24f * (n.y * 0.5f + 0.5f)) * faceShade;

            float3 lit = albedo * ((hemi + ndotl * 0.78f) * ao);

            // Local point-light radiance spilled onto neighboring voxels
            float glowStrength = input.glowAndAnim.x * input.glowAndAnim.w * input.glowAndAnim.z;
            if (glowStrength > 0.01f)
            {
                uint pal = uint(input.glowAndAnim.y);
                float3 glowTint = float3(1.00f, 0.68f, 0.24f);
                if (pal == 1u) { glowTint = float3(0.18f, 0.90f, 1.00f); }
                else if (pal == 2u) { glowTint = float3(0.85f, 0.30f, 1.00f); }
                else if (pal == 3u) { glowTint = float3(1.00f, 0.36f, 0.66f); }
                lit += albedo * glowTint * (glowStrength * 0.75f) + glowTint * (glowStrength * 0.14f);
            }

            lit += selfEmissive;

            float3 eye = float3(input.eyeAndVoxelSize.x, input.eyeAndVoxelSize.y, input.eyeAndVoxelSize.z);
            float dist = length(wp - eye);
            float fog = smoothstep(42.0f, 118.0f, dist);
            float3 skyFog = float3(0.55f, 0.73f, 0.91f);
            float3 finalRgb = clamp(lerp(lit, skyFog, fog), float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));

            VoxelSceneFrameBuffer out;
            out.color = half4(half(finalRgb.x), half(finalRgb.y), half(finalRgb.z), half(1.0f));
            return out;
        }
    };

    /// Renders translucent shimmering turquoise river water and whitewater waterfall foam.
    class VoxelWaterForwardShader final : public IRenderClass
    {
    public:
        constructor(
            StructuredBuffer<VoxelQuad> quads [[Args0]],
            float4 vpRow0 [[Args1]],
            float4 vpRow1 [[Args2]],
            float4 vpRow2 [[Args3]],
            float4 vpRow3 [[Args4]],
            float4 eyePos [[Args5]],
            float4 sunDir [[Args6]],
            float voxelSize [[Args7]])
        {
            BlendState bs;
            bs.color.operation = BlendOperation::Add;
            bs.color.srcFactor = BlendFactor::SrcAlpha;
            bs.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
            bs.alpha.operation = BlendOperation::Add;
            bs.alpha.srcFactor = BlendFactor::One;
            bs.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
            setBlendState(0u, bs);
            setDepthCompareFunction(CompareFunction::Less);
            setDepthWriteEnabled(false);
            setCullMode(CullMode::None);
        }

    private:
        VoxelVarying vertex(uint vid [[VertexID]])
        {
            uint quadIdx = vid / 6u;
            uint triCorner = vid % 6u;

            VoxelQuad q = quads[quadIdx];
            uint m0 = q.meta.x;
            uint dir = m0 & 7u;
            uint matId = (m0 >> 3u) & 31u;
            uint uSpan = ((m0 >> 19u) & 15u) + 1u;
            uint vSpan = ((m0 >> 23u) & 15u) + 1u;

            uint cu = 0u;
            uint cv = 0u;
            if (triCorner == 0u || triCorner == 3u) { cu = 0u; cv = 0u; }
            else if (triCorner == 1u) { cu = 1u; cv = 0u; }
            else if (triCorner == 2u || triCorner == 4u) { cu = 1u; cv = 1u; }
            else { cu = 0u; cv = 1u; }

            float3 nDir = float3(0.0f, 1.0f, 0.0f);
            float3 uDir = float3(1.0f, 0.0f, 0.0f);
            float3 vDir = float3(0.0f, 0.0f, 1.0f);

            if (dir == 0u)      { nDir = float3( 1.0f, 0.0f, 0.0f); uDir = float3(0.0f, 0.0f, 1.0f); vDir = float3(0.0f, 1.0f, 0.0f); }
            else if (dir == 1u) { nDir = float3(-1.0f, 0.0f, 0.0f); uDir = float3(0.0f, 0.0f, 1.0f); vDir = float3(0.0f, 1.0f, 0.0f); }
            else if (dir == 2u) { nDir = float3( 0.0f, 1.0f, 0.0f); uDir = float3(1.0f, 0.0f, 0.0f); vDir = float3(0.0f, 0.0f, 1.0f); }
            else if (dir == 3u) { nDir = float3( 0.0f,-1.0f, 0.0f); uDir = float3(1.0f, 0.0f, 0.0f); vDir = float3(0.0f, 0.0f, 1.0f); }
            else if (dir == 4u) { nDir = float3( 0.0f, 0.0f, 1.0f); uDir = float3(1.0f, 0.0f, 0.0f); vDir = float3(0.0f, 1.0f, 0.0f); }
            else                { nDir = float3( 0.0f, 0.0f,-1.0f); uDir = float3(1.0f, 0.0f, 0.0f); vDir = float3(0.0f, 1.0f, 0.0f); }

            float3 worldPos = q.originAndAnim.xyz +
                uDir * (float(cu * uSpan) * voxelSize) +
                vDir * (float(cv * vSpan) * voxelSize);
            worldPos.y += q.originAndAnim.w;

            float4 wp4 = float4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
            VoxelVarying out;
            out.position = float4(
                dot(vpRow0, wp4),
                dot(vpRow1, wp4),
                dot(vpRow2, wp4),
                dot(vpRow3, wp4));
            out.worldPosAndAo = float4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
            out.normalAndMat = float4(nDir.x, nDir.y, nDir.z, float(matId) + 0.5f);
            out.glowAndAnim = float4(float(q.meta.z) * 0.001f, 0.0f, 0.0f, 0.0f);
            out.eyeAndVoxelSize = float4(eyePos.x, eyePos.y, eyePos.z, voxelSize);
            out.sunAndAux = float4(sunDir.x, sunDir.y, sunDir.z, 0.0f);
            return out;
        }

        VoxelSceneFrameBuffer fragment(VoxelVarying input)
        {
            float3 n = normalize(float3(input.normalAndMat.x, input.normalAndMat.y, input.normalAndMat.z));
            float3 wp = float3(input.worldPosAndAo.x, input.worldPosAndAo.y, input.worldPosAndAo.z);
            float3 eye = float3(input.eyeAndVoxelSize.x, input.eyeAndVoxelSize.y, input.eyeAndVoxelSize.z);
            float3 sunDir = normalize(float3(input.sunAndAux.x, input.sunAndAux.y, input.sunAndAux.z));
            uint matId = uint(input.normalAndMat.w);
            float shimmer = input.glowAndAnim.x;

            float3 viewDir = normalize(eye - wp);
            float fresnel = pow(clamp(1.0f - max(dot(n, viewDir), 0.0f), 0.0f, 1.0f), 3.0f);
            float3 halfVec = normalize(sunDir + viewDir);
            float spec = pow(max(dot(n, halfVec), 0.0f), 42.0f) * (0.65f + 0.35f * shimmer);

            float3 skyColor = float3(0.55f, 0.73f, 0.91f);
            float3 waterColor = float3(0.08f, 0.56f, 0.64f);
            float alpha = 0.76f;

            if (matId == 5u) // Whitewater Foam
            {
                waterColor = lerp(float3(0.74f, 0.92f, 0.96f), float3(0.96f, 0.99f, 1.00f), shimmer);
                alpha = 0.90f;
            }
            else
            {
                waterColor = lerp(waterColor, skyColor, fresnel * 0.55f) + float3(1.0f, 0.98f, 0.90f) * spec;
            }

            float dist = length(wp - eye);
            float fog = smoothstep(42.0f, 118.0f, dist);
            float3 finalRgb = clamp(lerp(waterColor, skyColor, fog), float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));

            VoxelSceneFrameBuffer out;
            out.color = half4(half(finalRgb.x), half(finalRgb.y), half(finalRgb.z), half(alpha));
            return out;
        }
    };

    // =========================================================================
    // 3. Parallel RenderNodes (Eager Atmosphere LUT + Fissioned Prep + Forward)
    // =========================================================================

    class BakeVoxelAtmosphereLutNode final : public IRenderNode<SparseVoxelParams, AtmosLutBuffer>
    {
    public:
        AtmosLutBuffer build(RenderNode &node, IN SparseVoxelParams params) const
        {
            auto lut = node.createBuffer<AtmosLutBuffer>("SVAtmosLut", 256u);
            node.computePass(
                "SVAtmosLutBake",
                node.dispatchThreads<BakeVoxelAtmosphereLutCompute>(
                    lut, params.timeSeconds, params.waterRippleStrength, 256u, 1u, 1u));
            return lut;
        }
    };

    class PrepareVoxelQuadrantGpuNode final : public IRenderNode<VoxelQuadrantPack, VoxelQuadrantGpuPack>
    {
    public:
        VoxelQuadrantGpuPack build(
            RenderNode &node,
            IN SparseVoxelParams params,
            IN AtmosLutBuffer atmosLut,
            IN VoxelQuadrantPack cpuPack) const
        {
            VoxelQuadrantGpuPack out;
            out.opaqueQuadCount = uint(cpuPack.opaqueQuads.size());
            out.foliageQuadCount = uint(cpuPack.foliageQuads.size());
            out.waterQuadCount = uint(cpuPack.waterQuads.size());

            const uint safeOp = out.opaqueQuadCount > 0u ? out.opaqueQuadCount : 1u;
            const uint safeFol = out.foliageQuadCount > 0u ? out.foliageQuadCount : 1u;
            const uint safeWat = out.waterQuadCount > 0u ? out.waterQuadCount : 1u;

            auto rawOp = node.createBuffer<VoxelQuadBuffer>("SVRawOpaque", safeOp);
            auto rawFol = node.createBuffer<VoxelQuadBuffer>("SVRawFoliage", safeFol);
            auto rawWat = node.createBuffer<VoxelQuadBuffer>("SVRawWater", safeWat);

            out.opaqueQuads = node.createBuffer<VoxelQuadBuffer>("SVOutOpaque", safeOp);
            out.foliageQuads = node.createBuffer<VoxelQuadBuffer>("SVOutFoliage", safeFol);
            out.waterQuads = node.createBuffer<VoxelQuadBuffer>("SVOutWater", safeWat);

            if (out.opaqueQuadCount > 0u) { node.writeBuffer(rawOp, cpuPack.opaqueQuads); }
            if (out.foliageQuadCount > 0u) { node.writeBuffer(rawFol, cpuPack.foliageQuads); }
            if (out.waterQuadCount > 0u) { node.writeBuffer(rawWat, cpuPack.waterQuads); }

            // Logical computePass with 3 independent output buffers split into parallel passes.
            node.computePass(
                "SVQuadrantPrepare",
                node.dispatchThreads<PrepareOpaqueVoxelQuadsCompute>(
                    rawOp, out.opaqueQuads, atmosLut, out.opaqueQuadCount, params.eyePosition, safeOp, 1u, 1u),
                node.dispatchThreads<PrepareFoliageEmissiveQuadsCompute>(
                    rawFol, out.foliageQuads, atmosLut, out.foliageQuadCount, params.eyePosition, safeFol, 1u, 1u),
                node.dispatchThreads<PrepareWaterVoxelQuadsCompute>(
                    rawWat, out.waterQuads, atmosLut, out.waterQuadCount, safeWat, 1u, 1u));

            return out;
        }
    };

    class RenderSparseVoxelWorldNode final : public IRenderNode<VoxelQuadrantGpuPack, VoxelColorTexture>
    {
    public:
        VoxelColorTexture build(
            RenderNode &node,
            IN SparseVoxelParams params,
            IN VoxelQuadrantGpuPack q0,
            IN VoxelQuadrantGpuPack q1,
            IN VoxelQuadrantGpuPack q2,
            IN VoxelQuadrantGpuPack q3) const
        {
            const uint2 safeExtent = uint2(
                params.extent.x > 0u ? params.extent.x : 1u,
                params.extent.y > 0u ? params.extent.y : 1u);
            auto colorTex = node.createTexture<VoxelColorTexture>("SVSceneColor", safeExtent);
            auto depthTex = node.createTexture<VoxelDepthTexture>("SVSceneDepth", safeExtent);

            VoxelSceneFrameBuffer fb;
            fb.color = node.view(colorTex);
            fb.color.loadOp = LoadOp::Clear;
            fb.color.clearValue = {0.55, 0.73, 0.91, 1.0};
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
            const float vs = svMax(params.voxelSize, 0.05f);
            const float glowScale = params.emissiveGlowScale;

            node.renderPass(
                "SVForwardWorldPass",
                fb,
                // 1. Opaque terrain, karst cliffs, arches, bridge, pagoda, bark, stone lanterns
                node.draw<VoxelOpaqueForwardShader>(q0.opaqueQuads, r0, r1, r2, r3, eye, sun, vs, glowScale, 0u, q0.opaqueQuadCount * 6u, 1u, 0u, 0u),
                node.draw<VoxelOpaqueForwardShader>(q1.opaqueQuads, r0, r1, r2, r3, eye, sun, vs, glowScale, 0u, q1.opaqueQuadCount * 6u, 1u, 0u, 0u),
                node.draw<VoxelOpaqueForwardShader>(q2.opaqueQuads, r0, r1, r2, r3, eye, sun, vs, glowScale, 0u, q2.opaqueQuadCount * 6u, 1u, 0u, 0u),
                node.draw<VoxelOpaqueForwardShader>(q3.opaqueQuads, r0, r1, r2, r3, eye, sun, vs, glowScale, 0u, q3.opaqueQuadCount * 6u, 1u, 0u, 0u),
                // 2. Sakura blossoms, pine boughs, glowing crystals, lantern cores & mushrooms
                node.draw<VoxelOpaqueForwardShader>(q0.foliageQuads, r0, r1, r2, r3, eye, sun, vs, glowScale, 1u, q0.foliageQuadCount * 6u, 1u, 0u, 0u),
                node.draw<VoxelOpaqueForwardShader>(q1.foliageQuads, r0, r1, r2, r3, eye, sun, vs, glowScale, 1u, q1.foliageQuadCount * 6u, 1u, 0u, 0u),
                node.draw<VoxelOpaqueForwardShader>(q2.foliageQuads, r0, r1, r2, r3, eye, sun, vs, glowScale, 1u, q2.foliageQuadCount * 6u, 1u, 0u, 0u),
                node.draw<VoxelOpaqueForwardShader>(q3.foliageQuads, r0, r1, r2, r3, eye, sun, vs, glowScale, 1u, q3.foliageQuadCount * 6u, 1u, 0u, 0u),
                // 3. Translucent turquoise river water & waterfall foam
                node.draw<VoxelWaterForwardShader>(q0.waterQuads, r0, r1, r2, r3, eye, sun, vs, q0.waterQuadCount * 6u, 1u, 0u, 0u),
                node.draw<VoxelWaterForwardShader>(q1.waterQuads, r0, r1, r2, r3, eye, sun, vs, q1.waterQuadCount * 6u, 1u, 0u, 0u),
                node.draw<VoxelWaterForwardShader>(q2.waterQuads, r0, r1, r2, r3, eye, sun, vs, q2.waterQuadCount * 6u, 1u, 0u, 0u),
                node.draw<VoxelWaterForwardShader>(q3.waterQuads, r0, r1, r2, r3, eye, sun, vs, q3.waterQuadCount * 6u, 1u, 0u, 0u));

            return colorTex;
        }
    };

    class PresentVoxelSwapchainNode final : public IRenderNode<SwapchainQueryResult, uint>
    {
    public:
        uint build(RenderNode &node, IN SwapchainQueryResult swapchainTarget, IN VoxelColorTexture sceneColor) const
        {
            node.renderToSwapchain(swapchainTarget, sceneColor, RenderToSwapchainDescriptor{}, RenderImGui());
            return 1u;
        }
    };

    // =========================================================================
    // 4. Exported Nested Brick-Cluster Subgraph & GPU Verification Entries
    // =========================================================================

    /// Exported UGL subgraph: evaluates 8 micro-voxel 16^3 bricks in parallel and binary-reduces their greedy meshes.
    [[Export]] inline auto buildVoxelBrickCluster(IN VoxelClusterInput input)
    {
        TaskGraph graph;
        Task<VoxelQuadrantPack> bricks[8];
        for (uint b = 0u; b < 8u; ++b)
        {
            EvaluateAndMeshBrickCPU evalBrick;
            evalBrick.subBrickIndex = b;
            bricks[b] = graph.add(evalBrick, input);
        }

        auto p0 = graph.add(mergeVoxelBrickMeshes, bricks[0], bricks[1]);
        auto p1 = graph.add(mergeVoxelBrickMeshes, bricks[2], bricks[3]);
        auto p2 = graph.add(mergeVoxelBrickMeshes, bricks[4], bricks[5]);
        auto p3 = graph.add(mergeVoxelBrickMeshes, bricks[6], bricks[7]);
        auto q0 = graph.add(mergeVoxelBrickMeshes, p0, p1);
        auto q1 = graph.add(mergeVoxelBrickMeshes, p2, p3);
        auto merged = graph.add(mergeVoxelBrickMeshes, q0, q1);
        return graph.add(tagClusterSubgraphCount, merged);
    }

    /// Exported GPU benchmark entry validating all RenderNodes and ReadbackTextureNode with UGLC.
    [[Export]] inline auto renderVoxelWorldBenchmarkFrame(IN PrebuiltVoxelScene input)
    {
        TaskGraph graph;
        auto atmosLut = graph.add(BakeVoxelAtmosphereLutNode{}, input.params);
        auto gq0 = graph.add(PrepareVoxelQuadrantGpuNode{}, input.params, atmosLut, input.q0);
        auto gq1 = graph.add(PrepareVoxelQuadrantGpuNode{}, input.params, atmosLut, input.q1);
        auto gq2 = graph.add(PrepareVoxelQuadrantGpuNode{}, input.params, atmosLut, input.q2);
        auto gq3 = graph.add(PrepareVoxelQuadrantGpuNode{}, input.params, atmosLut, input.q3);
        auto summary = graph.add(
            summarizeVoxelQuadrants, input.q0, input.q1, input.q2, input.q3, input.octreeInteriorCount);
        auto sceneColor = graph.add(
            RenderSparseVoxelWorldNode{}, input.params, gq0, gq1, gq2, gq3);
        auto pixels = graph.add(ReadbackTextureNode<uint8_t>{}, sceneColor);
        return graph.add(evaluateBenchmarkPixels, summary, pixels);
    }

    /// Exported windowed swapchain entry validating PresentVoxelSwapchainNode with UGLC.
    [[Export]] inline auto renderVoxelWorldSwapchainFrame(IN PrebuiltVoxelSwapchainScene input)
    {
        TaskGraph graph;
        auto atmosLut = graph.add(BakeVoxelAtmosphereLutNode{}, input.params);
        auto gq0 = graph.add(PrepareVoxelQuadrantGpuNode{}, input.params, atmosLut, input.q0);
        auto gq1 = graph.add(PrepareVoxelQuadrantGpuNode{}, input.params, atmosLut, input.q1);
        auto gq2 = graph.add(PrepareVoxelQuadrantGpuNode{}, input.params, atmosLut, input.q2);
        auto gq3 = graph.add(PrepareVoxelQuadrantGpuNode{}, input.params, atmosLut, input.q3);
        auto summary = graph.add(
            summarizeVoxelQuadrants, input.q0, input.q1, input.q2, input.q3, input.octreeInteriorCount);
        auto sceneColor = graph.add(
            RenderSparseVoxelWorldNode{}, input.params, gq0, gq1, gq2, gq3);
        auto presented = graph.add(PresentVoxelSwapchainNode{}, input.swapchainTarget, sceneColor);
        return graph.add(finalizeSwapchainSummary, summary, presented);
    }
} // namespace SparseVoxelSample
