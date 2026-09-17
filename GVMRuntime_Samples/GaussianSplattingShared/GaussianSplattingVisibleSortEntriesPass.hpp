#pragma once

/**
 * Builds a compact sort-entry stream for visible source splats without evaluating the full Gaussian ellipse.
 *
 * This pass is used by samples that need a conservative visibility and depth-sort stream from the original
 * position/opacity buffer. It assumes ScenePositionOpacityBindGroup exposes GaussianPositionOpacity records and that
 * the caller has already reset the frame counters before dispatch.
 */
class [[LocalWorkGroupSize(128, 1, 1)]] BuildVisibleSortEntriesPass final : public IComputeClass
{
public:
    constructor(BindGroup<CameraBindGroup> cameraBindGroup [[Slot0]],
                BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot1]],
                BindGroup<ScenePositionOpacityBindGroup> scenePositionOpacityBindGroup [[Slot2]],
                BindGroup<EntryBufferBindGroup> entryBindGroup [[Slot3]],
                BindGroup<RasterAnalysisBindGroup> rasterAnalysisBindGroup [[Slot4]]
    )
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const ViewerGlobals globals = frameStateBindGroup->globals->read();
        const Camera camera = cameraBindGroup->camBuffer->read();
        const uint splatIndex = threadID.x;
        if (splatIndex >= globals.sceneInfo.x)
        {
            return;
        }

        const float4 positionOpacity = scenePositionOpacityBindGroup->splats[splatIndex].positionOpacity;
        const float3 worldPosition = positionOpacity.xyz;
        const float opacity = positionOpacity.w;
        if (opacity <= MinVisibleAlpha)
        {
            return;
        }

        const float3 cameraPosition = mul(camera.view, float4(worldPosition, 1.0f)).xyz;
        if (cameraPosition.z <= 0.0001f)
        {
            return;
        }

        const float4 clip = mul(camera.proj, float4(cameraPosition, 1.0f));
        if (clip.w <= 0.00001f)
        {
            return;
        }

        // Keep sort-entry generation conservative and cheap; precise ellipse bounds stay in raster.
        const float inverseClipW = 1.0f / clip.w;
        const float3 ndcPosition = clip.xyz * inverseClipW;
        const float sortEntryFrustumDilation = max(globals.backgroundAndScale.w, 0.0f);
        if (abs(ndcPosition.x) > 1.0f + sortEntryFrustumDilation ||
            abs(ndcPosition.y) > 1.0f + sortEntryFrustumDilation ||
            ndcPosition.z < -sortEntryFrustumDilation ||
            ndcPosition.z > 1.001f)
        {
            return;
        }

        const uint visibleIndex = atomicAdd(frameStateBindGroup->duplicateCounter[0], 1u);
        atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedSplatCount], 1u);
        atomicAdd(rasterAnalysisBindGroup->counters[RasterAnalysisProjectedEntryCount], 1u);
        atomicMax(rasterAnalysisBindGroup->counters[RasterAnalysisMaxProjectedTileCount], 1u);
        if (visibleIndex >= globals.sceneInfo.y)
        {
            atomicAdd(frameStateBindGroup->overflowCounter[0], 1u);
            return;
        }

        SortEntry entry;
        entry.sortKey = reversePackedDepthKeyForGraphics(packDepthKey(cameraPosition.z, globals), globals);
        entry.splatIndex = splatIndex;
        entryBindGroup->entries[visibleIndex] = entry;
    }
};
