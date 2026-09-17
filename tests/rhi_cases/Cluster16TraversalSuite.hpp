#ifndef GVM_TEST_RHI_CLUSTER16_TRAVERSAL_SUITE_HPP
#define GVM_TEST_RHI_CLUSTER16_TRAVERSAL_SUITE_HPP

#include "UGL.h"

using namespace UGL;

namespace Test16ClusterTraversalTest
{
    static const uint WorkGroupSize = 128u;
    static const uint InvalidIndex = 0xffffffffu;

    static const uint StateCurrentFrontierCount = 0u;
    static const uint StateNextFrontierCount = 1u;
    static const uint StateSelectedClusterCount = 2u;
    static const uint StateProcessedClusterCount = 3u;
    static const uint StateOverflowCount = 4u;
    static const uint StateMaxNextFrontierCount = 5u;
    static const uint StateEvaluatedLevelCount = 6u;
    static const uint StateEstimatedEntryCount = 7u;
    static const uint StateCount = 16u;

    static const uint ClusterFlagLeaf = 1u;

    /**
     * Stores the unified Test16 cluster node layout used by CPU build code and GPU traversal.
     */
    struct ClusterTreeNode
    {
        float4 positionOpacityProfile;
        float4 axis0;
        float4 axis1;
        float4 axis2;
        float4 shDc;
        float4 boundsCenterRadius;
        float4 errorInfo;
        uint4 childInfo;
        uint4 clusterInfo;
        uint4 budgetInfo;
    };

    /**
     * Stores immutable traversal constants for one Test16 cluster traversal dispatch sequence.
     */
    struct ClusterTraversalGlobals
    {
        // x: cluster node count, y: frontier capacity, z: max selected output count, w: root entry budget.
        uint4 counts;
        // x/y: focal length in pixels, z/w: unused by the node test.
        float4 projection;
        // x: split threshold in pixels, y: appearance error weight, z: minimum depth, w: unused.
        float4 lod;
    };

    /**
     * Binds the Test16 cluster tree, frontier buffers, state counters, and selected cluster output.
     */
    struct ClusterTraversalBindGroup final : public IBindGroup
    {
        constructor(UniformBuffer<ClusterTraversalGlobals> globals [[Binding0]],
                    StructuredBuffer<ClusterTreeNode> clusterNodes [[Binding1]],
                    StructuredBuffer<uint> frontierIn [[Binding2]],
                    StructuredBuffer<uint> frontierBudgetIn [[Binding3]],
                    RWStructuredBuffer<uint> frontierOut [[Binding4]],
                    RWStructuredBuffer<uint> frontierBudgetOut [[Binding5]],
                    RWStructuredBuffer<uint> traversalState [[Binding6]],
                    RWStructuredBuffer<uint> activeClusterIndex [[Binding7]])
        {
        }
    };

    /**
     * Binds state needed to publish the next frontier as the current frontier for the following pass.
     */
    struct ClusterTraversalStateBindGroup final : public IBindGroup
    {
        constructor(UniformBuffer<ClusterTraversalGlobals> globals [[Binding0]],
                    RWStructuredBuffer<uint> traversalState [[Binding1]])
        {
        }
    };

    /**
     * Binds the completion flag used by the host-side watchdog.
     */
    struct CompletionBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<uint> completionValue [[Binding0]])
        {
        }
    };

    /**
     * Computes the Test16 screen-space split error for one cluster without frustum culling.
     */
    inline float computeClusterScreenErrorPx(const ClusterTreeNode &cluster, const ClusterTraversalGlobals &globals)
    {
        const float depth = max(cluster.boundsCenterRadius.z, globals.lod.z);
        const float focal = max(globals.projection.x, globals.projection.y);
        const float geometricError = max(cluster.errorInfo.x, 0.0f);
        const float appearanceError = max(cluster.errorInfo.y, 0.0f) * globals.lod.y;
        return max(geometricError, appearanceError) * focal / depth;
    }

    /**
     * Returns true when a cluster has a contiguous child range inside the uploaded node array.
     */
    inline bool clusterHasValidChildren(const ClusterTreeNode &cluster, uint clusterNodeCount)
    {
        const uint childStart = cluster.childInfo.x;
        const uint childCount = cluster.childInfo.y;
        return childCount > 0u && childStart < clusterNodeCount && childCount <= clusterNodeCount - childStart;
    }

    /**
     * Returns true when a cluster should refine to its children for the current traversal globals.
     */
    inline bool clusterShouldSplit(const ClusterTreeNode &cluster, const ClusterTraversalGlobals &globals, uint clusterNodeCount)
    {
        return clusterHasValidChildren(cluster, clusterNodeCount) && computeClusterScreenErrorPx(cluster, globals) > globals.lod.x;
    }

    /**
     * Returns the conservative projected-entry cost for rendering one selected cluster in the node test.
     */
    inline uint clusterSelectedEntryCost(const ClusterTreeNode &cluster)
    {
        return cluster.childInfo.y == 0u ? max(min(cluster.clusterInfo.x, 32u), 1u) : 1u;
    }

    /**
     * Returns the minimum entry budget needed for this node to remain renderable in the node test.
     */
    inline uint clusterBudgetMinEntryCost(const ClusterTreeNode &cluster)
    {
        return max(cluster.budgetInfo.x, clusterSelectedEntryCost(cluster));
    }

    /**
     * Returns the precomputed full-subtree entry demand used by deterministic node-test budget propagation.
     */
    inline uint clusterBudgetFullEntryCost(const ClusterTreeNode &cluster)
    {
        return max(cluster.budgetInfo.y, clusterBudgetMinEntryCost(cluster));
    }

    /**
     * Computes the Test15-style radial pixel-scale priority used by deterministic node-test budget propagation.
     */
    inline float clusterBudgetPriority(const ClusterTreeNode &cluster, const ClusterTraversalGlobals &globals)
    {
        const float depth = max(cluster.boundsCenterRadius.z, globals.lod.z);
        const float focal = max(globals.projection.x, globals.projection.y);
        const float featureSize = max(cluster.errorInfo.z * 2.0f * max(cluster.positionOpacityProfile.w, 1.0f), 0.00001f);
        return max(featureSize * focal / depth, 0.0001f);
    }

    /**
     * Evaluates one frontier level, compacting split children and selected clusters with group-local prefixes.
     */
    class [[LocalWorkGroupSize(WorkGroupSize, 1, 1)]] EvaluateClusterFrontierPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<ClusterTraversalBindGroup> traversal [[Slot0]])
        {
        }

    private:
        /**
         * Evaluates one frontier item and publishes split children or selected cluster output.
         */
        void compute(uint3 threadID [[DispatchThreadID]], uint groupIndex [[GroupIndex]])
        {
            const ClusterTraversalGlobals globals = traversal->globals->read();
            const uint clusterNodeCount = globals.counts.x;
            const uint frontierCapacity = globals.counts.y;
            const uint maxSelectedClusterCount = globals.counts.z;
            if (clusterNodeCount == 0u || frontierCapacity == 0u || maxSelectedClusterCount == 0u)
            {
                return;
            }

            const uint currentFrontierCount = atomicLoad(traversal->traversalState[StateCurrentFrontierCount]);
            const uint frontierOrdinal = threadID.x;
            const uint groupStart = frontierOrdinal - groupIndex;

            GroupShared<uint> groupClusterIndex[WorkGroupSize];
            GroupShared<uint> groupChildStart[WorkGroupSize];
            GroupShared<uint> groupChildCount[WorkGroupSize];
            GroupShared<uint> groupChildPrefix[WorkGroupSize];
            GroupShared<uint> groupOutputPrefix[WorkGroupSize];
            GroupShared<uint> groupOutputEntryCost[WorkGroupSize];
            GroupShared<uint> groupEntryBudget[WorkGroupSize];
            GroupShared<uint> groupShouldSplit[WorkGroupSize];
            GroupShared<uint> groupShouldOutput[WorkGroupSize];
            GroupShared<uint> groupChildTotal;
            GroupShared<uint> groupOutputTotal;
            GroupShared<uint> groupOutputEntryTotal;
            GroupShared<uint> groupActiveTotal;
            GroupShared<uint> groupChildBase;
            GroupShared<uint> groupOutputBase;
            GroupShared<uint> groupChildWriteCount;
            GroupShared<uint> groupOutputWriteCount;

            groupClusterIndex[groupIndex] = InvalidIndex;
            groupChildStart[groupIndex] = 0u;
            groupChildCount[groupIndex] = 0u;
            groupChildPrefix[groupIndex] = 0u;
            groupOutputPrefix[groupIndex] = 0u;
            groupOutputEntryCost[groupIndex] = 0u;
            groupEntryBudget[groupIndex] = 0u;
            groupShouldSplit[groupIndex] = 0u;
            groupShouldOutput[groupIndex] = 0u;
            if (groupIndex == 0u)
            {
                groupChildTotal = 0u;
                groupOutputTotal = 0u;
                groupOutputEntryTotal = 0u;
                groupActiveTotal = 0u;
                groupChildBase = 0u;
                groupOutputBase = 0u;
                groupChildWriteCount = 0u;
                groupOutputWriteCount = 0u;
            }
            GroupMemoryBarrierWithGroupSync();

            const bool activeLane = frontierOrdinal < currentFrontierCount && frontierOrdinal < frontierCapacity;
            if (activeLane)
            {
                const uint clusterIndex = traversal->frontierIn[frontierOrdinal];
                if (clusterIndex < clusterNodeCount)
                {
                    ClusterTreeNode cluster = traversal->clusterNodes[clusterIndex];
                    const uint entryBudget = max(traversal->frontierBudgetIn[frontierOrdinal], clusterBudgetMinEntryCost(cluster));
                    const uint childStart = cluster.childInfo.x;
                    const bool hasChildren = clusterHasValidChildren(cluster, clusterNodeCount);
                    const uint childCount = hasChildren ? cluster.childInfo.y : 0u;
                    const uint childMinEntryCost = hasChildren ? max(cluster.budgetInfo.z, childCount) : 0u;
                    const bool shouldSplit = childCount > 0u && entryBudget >= childMinEntryCost;
                    const bool shouldOutput = !shouldSplit;

                    groupClusterIndex[groupIndex] = clusterIndex;
                    groupChildStart[groupIndex] = childStart;
                    groupChildCount[groupIndex] = shouldSplit ? childCount : 0u;
                    groupOutputEntryCost[groupIndex] = shouldOutput ? clusterSelectedEntryCost(cluster) : 0u;
                    groupEntryBudget[groupIndex] = entryBudget;
                    groupShouldSplit[groupIndex] = shouldSplit ? 1u : 0u;
                    groupShouldOutput[groupIndex] = shouldOutput ? 1u : 0u;
                }
                else
                {
                    atomicAdd(traversal->traversalState[StateOverflowCount], 1u);
                }
            }
            GroupMemoryBarrierWithGroupSync();

            if (groupIndex == 0u)
            {
                uint childTotal = 0u;
                uint outputTotal = 0u;
                uint outputEntryTotal = 0u;
                uint activeTotal = 0u;
                for (uint lane = 0u; lane < WorkGroupSize; ++lane)
                {
                    const uint laneFrontierOrdinal = groupStart + lane;
                    if (laneFrontierOrdinal < currentFrontierCount && laneFrontierOrdinal < frontierCapacity)
                    {
                        activeTotal += 1u;
                    }

                    groupChildPrefix[lane] = childTotal;
                    if (groupShouldSplit[lane] != 0u)
                    {
                        childTotal += groupChildCount[lane];
                    }

                    groupOutputPrefix[lane] = outputTotal;
                    if (groupShouldOutput[lane] != 0u && groupClusterIndex[lane] != InvalidIndex)
                    {
                        outputTotal += 1u;
                        outputEntryTotal += groupOutputEntryCost[lane];
                    }
                }
                groupChildTotal = childTotal;
                groupOutputTotal = outputTotal;
                groupOutputEntryTotal = outputEntryTotal;
                groupActiveTotal = activeTotal;
            }
            GroupMemoryBarrierWithGroupSync();

            if (groupIndex == 0u)
            {
                if (groupActiveTotal > 0u)
                {
                    atomicAdd(traversal->traversalState[StateProcessedClusterCount], groupActiveTotal);
                }

                if (groupChildTotal > 0u)
                {
                    const uint requestedChildCount = groupChildTotal;
                    const uint observedChildCount = atomicAdd(traversal->traversalState[StateNextFrontierCount], requestedChildCount);
                    groupChildBase = observedChildCount;
                    if (observedChildCount >= frontierCapacity)
                    {
                        groupChildWriteCount = 0u;
                        atomicAdd(traversal->traversalState[StateOverflowCount], requestedChildCount);
                    }
                    else
                    {
                        groupChildWriteCount = min(requestedChildCount, frontierCapacity - observedChildCount);
                        if (groupChildWriteCount < requestedChildCount)
                        {
                            atomicAdd(traversal->traversalState[StateOverflowCount], requestedChildCount - groupChildWriteCount);
                        }
                    }
                    atomicMax(traversal->traversalState[StateMaxNextFrontierCount], observedChildCount + requestedChildCount);
                }

                if (groupOutputTotal > 0u)
                {
                    const uint requestedOutputCount = groupOutputTotal;
                    const uint observedOutputCount = atomicAdd(traversal->traversalState[StateSelectedClusterCount], requestedOutputCount);
                    atomicAdd(traversal->traversalState[StateEstimatedEntryCount], groupOutputEntryTotal);
                    groupOutputBase = observedOutputCount;
                    if (observedOutputCount >= maxSelectedClusterCount)
                    {
                        groupOutputWriteCount = 0u;
                        atomicAdd(traversal->traversalState[StateOverflowCount], requestedOutputCount);
                    }
                    else
                    {
                        groupOutputWriteCount = min(requestedOutputCount, maxSelectedClusterCount - observedOutputCount);
                        if (groupOutputWriteCount < requestedOutputCount)
                        {
                            atomicAdd(traversal->traversalState[StateOverflowCount], requestedOutputCount - groupOutputWriteCount);
                        }
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();

            if (groupShouldSplit[groupIndex] != 0u && groupClusterIndex[groupIndex] != InvalidIndex)
            {
                const uint childStart = groupChildStart[groupIndex];
                const uint parentIndex = groupClusterIndex[groupIndex];
                ClusterTreeNode parentCluster = traversal->clusterNodes[parentIndex];
                const uint childCount = parentCluster.childInfo.y;
                const uint childWriteBase = groupChildBase + groupChildPrefix[groupIndex];
                uint validChildOrdinal = 0u;
                for (uint childOrdinal = 0u; childOrdinal < childCount; ++childOrdinal)
                {
                    const uint childNodeIndex = childStart + childOrdinal;
                    if (childNodeIndex < clusterNodeCount)
                    {
                        const uint childOutputOrdinal = groupChildPrefix[groupIndex] + validChildOrdinal;
                        if (childOutputOrdinal < groupChildWriteCount && childWriteBase + validChildOrdinal < frontierCapacity)
                        {
                            ClusterTreeNode childCluster = traversal->clusterNodes[childNodeIndex];
                            const uint targetBaseCost = clusterBudgetMinEntryCost(childCluster);
                            const uint targetFullCost = clusterBudgetFullEntryCost(childCluster);
                            uint childEntryBudget = targetBaseCost;
                            const uint minimumEntryTotal = max(parentCluster.budgetInfo.z, childCount);

                            if (groupEntryBudget[groupIndex] > minimumEntryTotal)
                            {
                                const uint unlockBudgetStart = groupEntryBudget[groupIndex] - minimumEntryTotal;
                                uint remainingBudget = unlockBudgetStart;
                                bool targetUnlocked = false;
                                uint targetChunkCost = 0u;
                                uint unlockedChunkTotal = 0u;
                                bool hasLastUnlockedChild = false;
                                float lastUnlockedPriority = 3.402823466e+38f;
                                uint lastUnlockedNodeIndex = InvalidIndex;
                                for (uint unlockOrdinal = 0u; unlockOrdinal < childCount; ++unlockOrdinal)
                                {
                                    float bestPriority = -1.0f;
                                    uint bestNodeIndex = InvalidIndex;
                                    uint bestUnlockCost = 0u;
                                    uint bestFirstSplitCost = 0u;
                                    uint bestChunkCost = 0u;
                                    for (uint budgetChildOrdinal = 0u; budgetChildOrdinal < childCount; ++budgetChildOrdinal)
                                    {
                                        const uint budgetChildNodeIndex = childStart + budgetChildOrdinal;
                                        if (budgetChildNodeIndex < clusterNodeCount)
                                        {
                                            ClusterTreeNode budgetChild = traversal->clusterNodes[budgetChildNodeIndex];
                                            const uint baseCost = clusterBudgetMinEntryCost(budgetChild);
                                            const uint firstSplitCost = max(budgetChild.budgetInfo.z, baseCost);
                                            const uint unlockCost = firstSplitCost > baseCost ? firstSplitCost - baseCost : 0u;
                                            const float priority = clusterBudgetPriority(budgetChild, globals);
                                            const bool belowLastUnlockedChild = !hasLastUnlockedChild ||
                                                priority < lastUnlockedPriority ||
                                                (priority == lastUnlockedPriority && budgetChildNodeIndex > lastUnlockedNodeIndex);
                                            if (unlockCost > 0u && unlockCost <= remainingBudget && belowLastUnlockedChild &&
                                                (bestNodeIndex == InvalidIndex || priority > bestPriority || (priority == bestPriority && budgetChildNodeIndex < bestNodeIndex)))
                                            {
                                                bestPriority = priority;
                                                bestNodeIndex = budgetChildNodeIndex;
                                                bestUnlockCost = unlockCost;
                                                bestFirstSplitCost = firstSplitCost;
                                                bestChunkCost = budgetChild.budgetInfo.w;
                                            }
                                        }
                                    }
                                    if (bestNodeIndex == InvalidIndex)
                                    {
                                        break;
                                    }

                                    remainingBudget -= bestUnlockCost;
                                    hasLastUnlockedChild = true;
                                    lastUnlockedPriority = bestPriority;
                                    lastUnlockedNodeIndex = bestNodeIndex;
                                    if (bestChunkCost > 0u)
                                    {
                                        unlockedChunkTotal += bestChunkCost;
                                    }
                                    if (bestNodeIndex == childNodeIndex)
                                    {
                                        childEntryBudget = bestFirstSplitCost;
                                        targetChunkCost = bestChunkCost;
                                        targetUnlocked = true;
                                    }
                                }

                                if (targetUnlocked && unlockedChunkTotal > 0u && targetChunkCost > 0u && childEntryBudget < targetFullCost)
                                {
                                    const uint fullRoundCount = remainingBudget / unlockedChunkTotal;
                                    uint targetExtra = min(fullRoundCount * targetChunkCost, targetFullCost - childEntryBudget);
                                    childEntryBudget += targetExtra;
                                    const uint roundBudget = fullRoundCount * unlockedChunkTotal;
                                    uint leftoverBudget = remainingBudget > roundBudget ? remainingBudget - roundBudget : 0u;
                                    uint replayRemainingBudget = unlockBudgetStart;
                                    bool replayHasLastUnlockedChild = false;
                                    float replayLastUnlockedPriority = 3.402823466e+38f;
                                    uint replayLastUnlockedNodeIndex = InvalidIndex;
                                    for (uint replayOrdinal = 0u; replayOrdinal < childCount && leftoverBudget > 0u; ++replayOrdinal)
                                    {
                                        float bestPriority = -1.0f;
                                        uint bestNodeIndex = InvalidIndex;
                                        uint bestUnlockCost = 0u;
                                        uint bestChunkCost = 0u;
                                        for (uint budgetChildOrdinal = 0u; budgetChildOrdinal < childCount; ++budgetChildOrdinal)
                                        {
                                            const uint budgetChildNodeIndex = childStart + budgetChildOrdinal;
                                            if (budgetChildNodeIndex < clusterNodeCount)
                                            {
                                                ClusterTreeNode budgetChild = traversal->clusterNodes[budgetChildNodeIndex];
                                                const uint baseCost = clusterBudgetMinEntryCost(budgetChild);
                                                const uint firstSplitCost = max(budgetChild.budgetInfo.z, baseCost);
                                                const uint unlockCost = firstSplitCost > baseCost ? firstSplitCost - baseCost : 0u;
                                                const float priority = clusterBudgetPriority(budgetChild, globals);
                                                const bool belowLastUnlockedChild = !replayHasLastUnlockedChild ||
                                                    priority < replayLastUnlockedPriority ||
                                                    (priority == replayLastUnlockedPriority && budgetChildNodeIndex > replayLastUnlockedNodeIndex);
                                                if (unlockCost > 0u && unlockCost <= replayRemainingBudget && belowLastUnlockedChild &&
                                                    (bestNodeIndex == InvalidIndex || priority > bestPriority || (priority == bestPriority && budgetChildNodeIndex < bestNodeIndex)))
                                                {
                                                    bestPriority = priority;
                                                    bestNodeIndex = budgetChildNodeIndex;
                                                    bestUnlockCost = unlockCost;
                                                    bestChunkCost = budgetChild.budgetInfo.w;
                                                }
                                            }
                                        }
                                        if (bestNodeIndex == InvalidIndex)
                                        {
                                            break;
                                        }

                                        replayRemainingBudget -= bestUnlockCost;
                                        replayHasLastUnlockedChild = true;
                                        replayLastUnlockedPriority = bestPriority;
                                        replayLastUnlockedNodeIndex = bestNodeIndex;
                                        if (bestChunkCost > 0u && bestChunkCost <= leftoverBudget)
                                        {
                                            if (bestNodeIndex == childNodeIndex && childEntryBudget < targetFullCost)
                                            {
                                                childEntryBudget += min(bestChunkCost, targetFullCost - childEntryBudget);
                                            }
                                            leftoverBudget -= bestChunkCost;
                                        }
                                    }
                                }
                            }
                            traversal->frontierOut[childWriteBase + validChildOrdinal] = childNodeIndex;
                            traversal->frontierBudgetOut[childWriteBase + validChildOrdinal] = childEntryBudget;
                        }
                        else
                        {
                            atomicAdd(traversal->traversalState[StateOverflowCount], 1u);
                        }
                        validChildOrdinal += 1u;
                    }
                    else
                    {
                        atomicAdd(traversal->traversalState[StateOverflowCount], 1u);
                    }
                }
            }

            if (groupShouldOutput[groupIndex] != 0u && groupClusterIndex[groupIndex] != InvalidIndex)
            {
                const uint outputOrdinal = groupOutputPrefix[groupIndex];
                const uint outputIndex = groupOutputBase + outputOrdinal;
                if (outputOrdinal < groupOutputWriteCount && outputIndex < maxSelectedClusterCount)
                {
                    traversal->activeClusterIndex[outputIndex] = groupClusterIndex[groupIndex];
                }
                else
                {
                    atomicAdd(traversal->traversalState[StateOverflowCount], 1u);
                }
            }
        }
    };

    /**
     * Advances the double-buffered traversal state after one frontier evaluation pass.
     */
    class [[LocalWorkGroupSize(1, 1, 1)]] PrepareNextFrontierPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<ClusterTraversalStateBindGroup> traversalState [[Slot0]])
        {
        }

    private:
        /**
         * Publishes the next frontier count for the following fixed traversal dispatch.
         */
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x == 0u)
            {
                const ClusterTraversalGlobals globals = traversalState->globals->read();
                const uint frontierCapacity = globals.counts.y;
                const uint nextFrontierCount = atomicLoad(traversalState->traversalState[StateNextFrontierCount]);
                atomicStore(traversalState->traversalState[StateCurrentFrontierCount], min(nextFrontierCount, frontierCapacity));
                atomicStore(traversalState->traversalState[StateNextFrontierCount], 0u);
                atomicAdd(traversalState->traversalState[StateEvaluatedLevelCount], 1u);
            }
        }
    };

    /**
     * Writes the completion flag after all fixed frontier passes have been encoded.
     */
    class [[LocalWorkGroupSize(1, 1, 1)]] SignalCompletionPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<CompletionBindGroup> completion [[Slot0]])
        {
        }

    private:
        /**
         * Signals host-visible completion after the traversal command sequence finishes.
         */
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x == 0u)
            {
                atomicStore(completion->completionValue[0], 1u);
            }
        }
    };
} // namespace Test16ClusterTraversalTest

#endif
