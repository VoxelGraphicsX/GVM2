#pragma once

/**
 * Binds frame-wide viewer globals and the counters that define the active sort-entry range.
 *
 * Include this header inside namespace GsViewer after ViewerGlobals, BudgetTelemetry,
 * and the shared 3DGS GPU layouts are declared.
 */
struct FrameStateBindGroup final : public IBindGroup
{
    /**
     * Binds the per-frame globals and the mutable counters shared by projection, sort, and render setup.
     */
    constructor(UniformBuffer<ViewerGlobals> globals [[Binding0]], RWStructuredBuffer<uint> duplicateCounter [[Binding1]], RWStructuredBuffer<uint> overflowCounter [[Binding2]]
    )
    {
    }
};

/**
 * Binds duplicate and scatter overflow counters for diagnostics and safe budget checks.
 */
struct DebugCounterBindGroup final : public IBindGroup
{
    /**
     * Binds the overflow counters used to diagnose duplicate emission and scatter budget failures.
     */
    constructor(RWStructuredBuffer<uint> duplicateOverflowCounter [[Binding0]], RWStructuredBuffer<uint> scatterOverflowCounter [[Binding1]]
    )
    {
    }
};

/**
 * Binds the CPU-readable budget telemetry buffer written by 3DGS projection and traversal passes.
 */
struct BudgetTelemetryBindGroup final : public IBindGroup
{
    /**
     * Binds the telemetry buffer that is copied back to the CPU by diagnostics capture.
     */
    constructor(RWStructuredBuffer<BudgetTelemetry> telemetry [[Binding0]])
    {
    }
};

/**
 * Binds packed spherical-harmonic rest coefficients for Gaussian color evaluation.
 */
struct SceneShBindGroup final : public IBindGroup
{
    /**
     * Binds the read-only packed SH rest coefficient stream.
     */
    constructor(StructuredBuffer<GaussianShRestGPU> shRest [[Binding0]])
    {
    }
};

/**
 * Binds a writable sort-entry buffer used by projection or traversal producers.
 */
struct EntryBufferBindGroup final : public IBindGroup
{
    /**
     * Binds the writable sort-entry buffer produced by visibility or projection passes.
     */
    constructor(RWStructuredBuffer<SortEntry> entries [[Binding0]])
    {
    }
};

/**
 * Binds the input and output sort-entry buffers used by radix scatter passes.
 */
struct EntryPairBindGroup final : public IBindGroup
{
    /**
     * Binds one read-only sort-entry input and one writable sort-entry output for ping-pong sorting.
     */
    constructor(StructuredBuffer<SortEntry> inputEntries [[Binding0]], RWStructuredBuffer<SortEntry> outputEntries [[Binding1]])
    {
    }
};

/**
 * Binds the indirect dispatch command buffer used by AMD8 sort setup passes.
 */
struct EntryDispatchBindGroup final : public IBindGroup
{
    /**
     * Binds the writable indirect dispatch command used by sort setup.
     */
    constructor(RWStructuredBuffer<DispatchIndirectCommand> entryDispatch [[Binding0]])
    {
    }
};

/**
 * Binds per-partition radix aggregate and prefix records for AMD8 sort.
 */
struct PrefixDataBindGroup final : public IBindGroup
{
    /**
     * Binds the writable prefix data records used by radix-count and radix-resolve passes.
     */
    constructor(RWStructuredBuffer<PrefixData> prefixData [[Binding0]])
    {
    }
};

/**
 * Binds final radix bucket base offsets for AMD8 scatter.
 */
struct BucketBaseBindGroup final : public IBindGroup
{
    /**
     * Binds the writable per-bucket global base offsets consumed by radix scatter.
     */
    constructor(RWStructuredBuffer<uint> bucketBase [[Binding0]])
    {
    }
};

/**
 * Binds the HDR output texture written by compute or graphics sample passes.
 */
struct OutputBindGroup final : public IBindGroup
{
    /**
     * Binds the HDR output texture targeted by compute and graphics sample passes.
     */
    constructor(RWTexture2D<UGL::TextureFormat::RGBA16Float> outputTexture [[Binding0]])
    {
    }
};

/**
 * Binds a read-only sort-entry buffer for graphics rendering and diagnostics.
 */
struct EntryReadBindGroup final : public IBindGroup
{
    /**
     * Binds the read-only sorted entry stream consumed by rendering and diagnostics.
     */
    constructor(StructuredBuffer<SortEntry> entries [[Binding0]])
    {
    }
};

/**
 * Binds the graphics indirect draw command buffer for Gaussian billboard rendering.
 */
struct RenderIndirectBindGroup final : public IBindGroup
{
    /**
     * Binds the writable indirect draw command used by billboard rendering.
     */
    constructor(RWStructuredBuffer<GaussianBillboardRenderPassIndirectRenderCommand> commands [[Binding0]])
    {
    }
};
