#pragma once

/**
 * Stores the packed non-DC spherical-harmonic coefficients shared by 3DGS samples.
 *
 * Include this header inside namespace GsViewer after the UGL scalar/vector
 * types are available. The layout matches the Graphdeco channel-major PLY
 * payload used by the shared SH evaluation helpers.
 */
struct GaussianShRestGPU
{
    float4 shCoeff0;
    float4 shCoeff1;
    float4 shCoeff2;
    float4 shCoeff3;
    float4 shCoeff4;
    float4 shCoeff5;
    float4 shCoeff6;
    float4 shCoeff7;
    float4 shCoeff8;
    float4 shCoeff9;
    float4 shCoeff10;
    float4 shCoeff11;
};

/**
 * Stores the optional cold Gaussian payload used by graphics-oriented samples.
 *
 * This currently carries a base color proxy and is kept separate from the hot
 * transform/opacity payload so samples with different hot layouts can still
 * share the same cold buffer layout.
 */
struct GaussianSplatColdGPU
{
    float4 baseColor;
};

/**
 * Stores only Gaussian position and opacity for compact read-only passes.
 *
 * The buffer is used when a pass needs center and opacity but does not need the
 * full covariance or spherical-harmonic payload.
 */
struct GaussianPositionOpacity
{
    float4 positionOpacity;
};

/**
 * Stores a compute indirect dispatch command.
 *
 * The layout matches the three 32-bit workgroup dimensions consumed by the RHI
 * indirect dispatch path.
 */
struct DispatchIndirectCommand
{
    uint x;
    uint y;
    uint z;
};

/**
 * Stores a graphics indirect draw command for billboard and debug bounds draws.
 *
 * The layout matches the four 32-bit fields consumed by the RHI indirect draw
 * path: vertex count, instance count, first vertex, and first instance.
 */
struct GaussianBillboardRenderPassIndirectRenderCommand
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

/**
 * Stores one sortable render entry.
 *
 * The sort key is interpreted by the active sample pass and the splat index
 * references that sample's projected Gaussian payload.
 */
struct SortEntry
{
    uint sortKey;
    uint splatIndex;
};

/**
 * Stores one radix-sort aggregate and prefix pair.
 *
 * AMD8 sort passes write aggregate counts first, then resolve pass-local
 * prefixes before scatter consumes the pair.
 */
struct PrefixData
{
    uint aggregate;
    uint prefix;
};
