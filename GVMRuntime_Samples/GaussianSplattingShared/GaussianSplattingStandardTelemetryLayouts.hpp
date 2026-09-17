#pragma once

/**
 * Stores the standard 3DGS budget and raster telemetry read back by diagnostics.
 *
 * Include this header inside namespace GsViewer for samples that use the base
 * projection/sort/render telemetry layout without additional HiZ or voxel counters.
 */
struct BudgetTelemetry
{
    uint frameTag;
    uint requiredEntryCount;
    uint duplicateOverflowCount;
    uint scatterOverflowCount;
    uint projectedSplatCount;
    uint projectedEntryCount;
    uint nonEmptyTileCount;
    uint lightTileCount;
    uint heavyTileCount;
    uint heavySegmentCount;
    uint maxProjectedTileCount;
    uint maxTileRangeLength;
    uint maxHeavyTileSegmentCount;
};
