#pragma once

/**
 * Binds the standard raster-analysis counter buffer used by 3DGS samples without extra visibility state.
 *
 * Include this header only in samples that use the single-counter-buffer layout.
 */
struct RasterAnalysisBindGroup final : public IBindGroup
{
    /**
     * Binds the writable raster-analysis counter buffer used for telemetry and readback diagnostics.
     */
    constructor(RWStructuredBuffer<uint> counters [[Binding0]])
    {
    }
};
