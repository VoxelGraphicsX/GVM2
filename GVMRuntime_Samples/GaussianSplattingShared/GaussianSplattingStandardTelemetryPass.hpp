#pragma once

/**
 * Finalizes the standard 3DGS budget telemetry record from debug counters and raster analysis counters.
 *
 * Use this pass for samples whose RasterAnalysisBindGroup exposes only the common sort/raster counters.
 * Samples with additional telemetry channels, such as voxel HiZ diagnostics, should keep a specialized pass.
 */
class [[LocalWorkGroupSize(1, 1, 1)]] FinalizeBudgetTelemetryPass final : public IComputeClass
{
public:
    constructor(BindGroup<DebugCounterBindGroup> debugCounterBindGroup [[Slot0]],
                BindGroup<BudgetTelemetryBindGroup> budgetTelemetryBindGroup [[Slot1]],
                BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot2]]
    )
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x != 0u)
        {
            return;
        }

        budgetTelemetryBindGroup->telemetry[0].duplicateOverflowCount = atomicLoad(debugCounterBindGroup->duplicateOverflowCounter[0]);
        budgetTelemetryBindGroup->telemetry[0].scatterOverflowCount = atomicLoad(debugCounterBindGroup->scatterOverflowCounter[0]);
        budgetTelemetryBindGroup->telemetry[0].projectedSplatCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedSplatCount]);
        budgetTelemetryBindGroup->telemetry[0].projectedEntryCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedEntryCount]);
        budgetTelemetryBindGroup->telemetry[0].nonEmptyTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisNonEmptyTileCount]);
        budgetTelemetryBindGroup->telemetry[0].lightTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisLightTileCount]);
        budgetTelemetryBindGroup->telemetry[0].heavyTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavyTileCount]);
        budgetTelemetryBindGroup->telemetry[0].heavySegmentCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisHeavySegmentCount]);
        budgetTelemetryBindGroup->telemetry[0].maxProjectedTileCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisMaxProjectedTileCount]);
        budgetTelemetryBindGroup->telemetry[0].maxTileRangeLength = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisMaxTileRangeLength]);
        budgetTelemetryBindGroup->telemetry[0].maxHeavyTileSegmentCount = atomicLoad(rasterAnalysisBindGroup->counters[RasterAnalysisMaxHeavyTileSegmentCount]);
    }
};
