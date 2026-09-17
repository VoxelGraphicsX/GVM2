#pragma once

/**
 * Builds the indirect dispatch used by scan-style sort preparation passes.
 *
 * Include this header inside namespace GsViewer after FrameStateBindGroup,
 * EntryDispatchBindGroup, RenderIndirectBindGroup, EntryPairBindGroup,
 * PrefixDataBindGroup, BucketBaseBindGroup, DebugCounterBindGroup, and the
 * shared 3DGS sort math helpers have been declared.
 */
class [[LocalWorkGroupSize(1, 1, 1)]] BuildSortScanDispatchPass final : public IComputeClass
{
public:
    /**
     * Binds the frame state and dispatch output buffer used by this setup pass.
     */
    constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryDispatchBindGroup> entryDispatchBindGroup [[Slot1]]
    )
    {
    }

private:
    /**
     * Writes one indirect dispatch command sized for scan-style sort workgroups.
     */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x != 0u)
        {
            return;
        }

        const ViewerGlobals globals = frameStateBindGroup->globals->read();
        const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
        const uint workgroupCount = (activeCount + SortEntryScanWorkGroupSize - 1u) / SortEntryScanWorkGroupSize;
        entryDispatchBindGroup->entryDispatch[0].x = workgroupCount;
        entryDispatchBindGroup->entryDispatch[0].y = 1u;
        entryDispatchBindGroup->entryDispatch[0].z = 1u;
    }
};

/**
 * Builds the indirect draw command used by the shared Gaussian billboard render pass.
 *
 * The instance count is the active duplicate/sort entry count clamped to the
 * current sample's sort budget stored in ViewerGlobals::sceneInfo.y.
 */
class [[LocalWorkGroupSize(1, 1, 1)]] BuildGaussianRenderIndirectPass final : public IComputeClass
{
public:
    /**
     * Binds the frame state and indirect draw command buffer for billboard rendering.
     */
    constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<RenderIndirectBindGroup> renderIndirectBindGroup [[Slot1]]
    )
    {
    }

private:
    /**
     * Writes one four-vertex indirect draw command with one instance per active sort entry.
     */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x != 0u)
        {
            return;
        }

        const ViewerGlobals globals = frameStateBindGroup->globals->read();
        const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
        renderIndirectBindGroup->commands[0].vertexCount = 4u;
        renderIndirectBindGroup->commands[0].instanceCount = activeCount;
        renderIndirectBindGroup->commands[0].firstVertex = 0u;
        renderIndirectBindGroup->commands[0].firstInstance = 0u;
    }
};

/**
 * Builds the indirect dispatch dimensions for AMD8 radix sort partitions.
 *
 * The pass expects the active entry count to have already been written to the
 * duplicate counter by the projection or traversal path.
 */
class [[LocalWorkGroupSize(1, 1, 1)]] BuildAmd8DispatchPass final : public IComputeClass
{
public:
    /**
     * Binds the frame state and indirect dispatch command buffer for AMD8 sorting.
     */
    constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryDispatchBindGroup> entryDispatchBindGroup [[Slot1]]
    )
    {
    }

private:
    /**
     * Writes one indirect dispatch command with one workgroup per active AMD8 partition.
     */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x != 0u)
        {
            return;
        }

        const ViewerGlobals globals = frameStateBindGroup->globals->read();
        const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
        const uint partitionCount = computeAmd8PartitionCount(activeCount);
        entryDispatchBindGroup->entryDispatch[0].x = partitionCount;
        entryDispatchBindGroup->entryDispatch[0].y = 1u;
        entryDispatchBindGroup->entryDispatch[0].z = 1u;
    }
};

/**
 * Computes per-partition radix bucket counts for the stable AMD8 sort.
 *
 * This pass writes one PrefixData record per partition and radix bucket; the
 * resolve pass converts those aggregates into global bucket offsets.
 */
class [[LocalWorkGroupSize(Amd8WorkGroupSize, 1, 1)]] Amd8PrefixPass final : public IComputeClass
{
public:
    /**
     * Binds the current sort-pass globals, entry pair, and prefix data buffers.
     */
    constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryPairBindGroup> entryPairBindGroup [[Slot1]], BindGroup<PrefixDataBindGroup> prefixDataBindGroup [[Slot2]]
    )
    {
    }

private:
    /**
     * Counts radix buckets for one active partition and stores per-bucket aggregates.
     */
    void compute(uint3 threadID [[DispatchThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
    {
        GroupShared<uint> localHistogram[Amd8RadixBucketCount];

        const ViewerGlobals globals = frameStateBindGroup->globals->read();
        const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
        const uint partitionCount = computeAmd8PartitionCount(activeCount);
        const uint partitionIndex = groupID.x;
        if (partitionIndex >= partitionCount)
        {
            return;
        }

        atomicStore(localHistogram[groupIndex], 0u);
        GroupMemoryBarrierWithGroupSync();

        const uint waveLaneIndex = WaveGetLaneIndex();
        const uint waveLaneCount = WaveGetLaneCount();
        const uint partitionBase = partitionIndex * Amd8PartitionElementCount;
        for (uint elementIndex = 0u; elementIndex < Amd8ElementsPerThread; ++elementIndex)
        {
            const uint entryIndex = partitionBase + elementIndex * Amd8WorkGroupSize + groupIndex;
            uint bucket = InvalidIndex;
            if (entryIndex < activeCount)
            {
                const SortEntry entry = entryPairBindGroup->inputEntries[entryIndex];
                bucket = radixBucketForPass(entry, globals.sceneInfo.w);
            }

            uint leaderLaneIndex = 0u;
            uint waveBucketCount = 0u;
            bool isBucketLeader = false;
            if (bucket < Amd8RadixBucketCount)
            {
                bool foundLeader = false;
                for (uint laneIndex = 0u; laneIndex < waveLaneCount; ++laneIndex)
                {
                    const uint waveBucket = WaveReadLaneAt(bucket, laneIndex);
                    if (waveBucket == bucket)
                    {
                        if (!foundLeader)
                        {
                            leaderLaneIndex = laneIndex;
                            foundLeader = true;
                        }
                        waveBucketCount += 1u;
                    }
                }
                isBucketLeader = waveLaneIndex == leaderLaneIndex;
            }

            if (isBucketLeader)
            {
                atomicAdd(localHistogram[bucket], waveBucketCount);
            }
        }
        GroupMemoryBarrierWithGroupSync();

        const uint localCount = atomicLoad(localHistogram[groupIndex]);
        const uint stateIndex = partitionIndex * Amd8RadixBucketCount + groupIndex;
        prefixDataBindGroup->prefixData[stateIndex].aggregate = localCount;
        prefixDataBindGroup->prefixData[stateIndex].prefix = 0u;
    }
};

/**
 * Resolves AMD8 per-partition bucket aggregates into global scatter bases.
 *
 * The pass scans all active partitions for one radix bucket per invocation and
 * stores both partition-local prefixes and final bucket base offsets.
 */
class [[LocalWorkGroupSize(Amd8RadixBucketCount, 1, 1)]] Amd8ResolveOffsetsPass final : public IComputeClass
{
public:
    /**
     * Binds the current sort-pass globals, prefix data, and global bucket base buffers.
     */
    constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<PrefixDataBindGroup> prefixDataBindGroup [[Slot1]], BindGroup<BucketBaseBindGroup> bucketBaseBindGroup [[Slot2]]
    )
    {
    }

private:
    /**
     * Scans partition aggregates for one bucket and writes global bucket bases.
     */
    void compute(uint3 threadID [[DispatchThreadID]], uint groupIndex [[GroupIndex]])
    {
        GroupShared<uint> bucketTotals[Amd8RadixBucketCount];

        const ViewerGlobals globals = frameStateBindGroup->globals->read();
        const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
        const uint partitionCount = computeAmd8PartitionCount(activeCount);
        const uint bucketIndex = groupIndex;

        uint running = 0u;
        for (uint partitionIndex = 0u; partitionIndex < partitionCount; ++partitionIndex)
        {
            const uint stateIndex = partitionIndex * Amd8RadixBucketCount + bucketIndex;
            prefixDataBindGroup->prefixData[stateIndex].prefix = running;
            running += prefixDataBindGroup->prefixData[stateIndex].aggregate;
        }
        bucketTotals[bucketIndex] = running;
        GroupMemoryBarrierWithGroupSync();

        if (groupIndex == 0u)
        {
            uint scan = 0u;
            for (uint scanIndex = 0u; scanIndex < Amd8RadixBucketCount; ++scanIndex)
            {
                const uint value = bucketTotals[scanIndex];
                bucketTotals[scanIndex] = scan;
                scan += value;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        bucketBaseBindGroup->bucketBase[bucketIndex] = bucketTotals[bucketIndex];
    }
};

/**
 * Scatters entries into the output buffer for one AMD8 radix pass.
 *
 * The implementation preserves the existing deterministic tile-local ordering
 * and reports overflow through both the frame and debug counters.
 */
class [[LocalWorkGroupSize(Amd8WorkGroupSize, 1, 1)]] Amd8ScatterPass final : public IComputeClass
{
public:
    /**
     * Binds the sort entry pair, prefix data, bucket bases, and overflow counters.
     */
    constructor(BindGroup<FrameStateBindGroup> frameStateBindGroup [[Slot0]], BindGroup<EntryPairBindGroup> entryPairBindGroup [[Slot1]], BindGroup<PrefixDataBindGroup> prefixDataBindGroup [[Slot2]], BindGroup<BucketBaseBindGroup> bucketBaseBindGroup [[Slot3]], BindGroup<DebugCounterBindGroup> debugCounterBindGroup [[Slot4]]
    )
    {
    }

private:
    /**
     * Stably scatters one partition's entries into the output sort buffer for the active radix byte.
     */
    void compute(uint3 threadID [[DispatchThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
    {
        GroupShared<uint> localBuckets[Amd8WorkGroupSize];
        GroupShared<uint> blockBucketCarry[Amd8RadixBucketCount];
        GroupShared<uint> tileBucketCounts[Amd8ScatterTileCount * Amd8RadixBucketCount];

        const ViewerGlobals globals = frameStateBindGroup->globals->read();
        const uint activeCount = clampActiveEntryCount(globals, atomicLoad(frameStateBindGroup->duplicateCounter[0]));
        const uint partitionCount = computeAmd8PartitionCount(activeCount);
        const uint partitionIndex = groupID.x;
        if (partitionIndex >= partitionCount)
        {
            return;
        }

        for (uint bucketIndex = groupIndex; bucketIndex < Amd8RadixBucketCount; bucketIndex += Amd8WorkGroupSize)
        {
            blockBucketCarry[bucketIndex] = 0u;
        }
        GroupMemoryBarrierWithGroupSync();

        const uint tileIndex = groupIndex / Amd8ScatterTileSize;
        const uint tileLane = groupIndex % Amd8ScatterTileSize;
        const uint partitionBase = partitionIndex * Amd8PartitionElementCount;
        for (uint elementIndex = 0u; elementIndex < Amd8ElementsPerThread; ++elementIndex)
        {
            for (uint tileBucketIndex = groupIndex; tileBucketIndex < Amd8ScatterTileCount * Amd8RadixBucketCount; tileBucketIndex += Amd8WorkGroupSize)
            {
                tileBucketCounts[tileBucketIndex] = 0u;
            }
            GroupMemoryBarrierWithGroupSync();

            const uint entryIndex = partitionBase + elementIndex * Amd8WorkGroupSize + groupIndex;
            uint bucket = InvalidIndex;
            SortEntry entry = {};
            if (entryIndex < activeCount)
            {
                entry = entryPairBindGroup->inputEntries[entryIndex];
                bucket = radixBucketForPass(entry, globals.sceneInfo.w);
            }

            localBuckets[groupIndex] = bucket;
            GroupMemoryBarrierWithGroupSync();

            if (tileLane == 0u)
            {
                const uint tileBase = tileIndex * Amd8ScatterTileSize;
                const uint tileCountBase = tileIndex * Amd8RadixBucketCount;
                for (uint laneIndex = 0u; laneIndex < Amd8ScatterTileSize; ++laneIndex)
                {
                    const uint tileBucket = localBuckets[tileBase + laneIndex];
                    if (tileBucket < Amd8RadixBucketCount)
                    {
                        tileBucketCounts[tileCountBase + tileBucket] =
                            tileBucketCounts[tileCountBase + tileBucket] + 1u;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();

            if (bucket < Amd8RadixBucketCount)
            {
                uint localRank = blockBucketCarry[bucket];
                const uint tileBase = tileIndex * Amd8ScatterTileSize;
                for (uint laneIndex = 0u; laneIndex < tileLane; ++laneIndex)
                {
                    if (localBuckets[tileBase + laneIndex] == bucket)
                    {
                        localRank += 1u;
                    }
                }
                for (uint previousTile = 0u; previousTile < tileIndex; ++previousTile)
                {
                    localRank += tileBucketCounts[previousTile * Amd8RadixBucketCount + bucket];
                }

                const uint stateIndex = partitionIndex * Amd8RadixBucketCount + bucket;
                const uint writeIndex = bucketBaseBindGroup->bucketBase[bucket] + prefixDataBindGroup->prefixData[stateIndex].prefix + localRank;
                if (writeIndex >= globals.sceneInfo.y)
                {
                    atomicAdd(frameStateBindGroup->overflowCounter[0], 1u);
                    atomicAdd(debugCounterBindGroup->scatterOverflowCounter[0], 1u);
                }
                else
                {
                    entryPairBindGroup->outputEntries[writeIndex] = entry;
                }
            }
            GroupMemoryBarrierWithGroupSync();

            if (groupIndex < Amd8RadixBucketCount)
            {
                uint batchCount = 0u;
                for (uint tile = 0u; tile < Amd8ScatterTileCount; ++tile)
                {
                    batchCount += tileBucketCounts[tile * Amd8RadixBucketCount + groupIndex];
                }
                blockBucketCarry[groupIndex] = blockBucketCarry[groupIndex] + batchCount;
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }
};
