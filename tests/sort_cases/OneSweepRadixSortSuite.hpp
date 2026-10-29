#ifndef GVM_TEST_ONESWEEP_RADIX_SORT_SUITE_HPP
#define GVM_TEST_ONESWEEP_RADIX_SORT_SUITE_HPP

#include "UGL.h"

using namespace UGL;

namespace OneSweepSortTest
{
    static const uint RadixBucketCount = 256u;
    static const uint RadixPassBits = 8u;
    static const uint RadixPassCount = 4u;
    static const uint WorkGroupSize = 256u;
    static const uint SubgroupSize = 32u;
    static const uint SubgroupCount = WorkGroupSize / SubgroupSize;
    static const uint InvalidBucket = 0xffffffffu;

    struct SortGlobals
    {
        uint elementCount;
        uint passIndex;
        uint partitionCount;
        uint reserved;
    };

    struct WritePatternGlobals
    {
        uint elementCount;
        uint keyMultiplier;
        uint keyBias;
        uint payloadSalt;
    };

    struct SortEntry
    {
        uint sortKey;
        uint payload;
    };

    struct PrefixData
    {
        uint aggregate;
        uint prefix;
    };

    struct SortGlobalsBindGroup final : public IBindGroup
    {
        constructor(StructuredBuffer<SortGlobals> globals [[Binding0]])
        {
        }
    };

    struct EntryPairBindGroup final : public IBindGroup
    {
        constructor(StructuredBuffer<SortEntry> inputEntries [[Binding0]], RWStructuredBuffer<SortEntry> outputEntries [[Binding1]])
        {
        }
    };

    struct PrefixDataBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<PrefixData> prefixData [[Binding0]])
        {
        }
    };

    struct BucketBaseBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<uint> bucketBase [[Binding0]])
        {
        }
    };

    struct WritePatternGlobalsBindGroup final : public IBindGroup
    {
        constructor(UniformBuffer<WritePatternGlobals> globals [[Binding0]])
        {
        }
    };

    struct OutputEntriesBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<SortEntry> outputEntries [[Binding0]])
        {
        }
    };

    struct CompletionBindGroup final : public IBindGroup
    {
        constructor(RWStructuredBuffer<uint> completionValue [[Binding0]])
        {
        }
    };

    inline uint radixBucketForPass(uint sortKey, uint passIndex)
    {
        return (sortKey >> (passIndex * RadixPassBits)) & (RadixBucketCount - 1u);
    }

    class [[LocalWorkGroupSize(WorkGroupSize, 1, 1)]] WritePatternPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<WritePatternGlobalsBindGroup> writePatternGlobalsBindGroup [[Slot0]], BindGroup<OutputEntriesBindGroup> outputEntriesBindGroup [[Slot1]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const WritePatternGlobals globals = writePatternGlobalsBindGroup->globals->read();
            if (threadID.x >= globals.elementCount)
            {
                return;
            }

            SortEntry entry = {};
            entry.sortKey = threadID.x * globals.keyMultiplier + globals.keyBias;
            entry.payload = threadID.x ^ globals.payloadSalt;
            outputEntriesBindGroup->outputEntries[threadID.x] = entry;
        }
    };

    class [[LocalWorkGroupSize(WorkGroupSize, 1, 1)]] OneSweepPrefixPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<SortGlobalsBindGroup> sortGlobalsBindGroup [[Slot0]], BindGroup<EntryPairBindGroup> entryPairBindGroup [[Slot1]], BindGroup<PrefixDataBindGroup> prefixDataBindGroup [[Slot2]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
        {
            GroupShared<uint> localHistogram[RadixBucketCount];
            const SortGlobals globals = sortGlobalsBindGroup->globals[0];
            const uint partitionIndex = groupID.x;
            if (partitionIndex >= globals.partitionCount)
            {
                return;
            }

            for (uint bucketIndex = groupIndex; bucketIndex < RadixBucketCount; bucketIndex += WorkGroupSize)
            {
                atomicStore(localHistogram[bucketIndex], 0u);
            }
            GroupMemoryBarrierWithGroupSync();

            const uint entryIndex = threadID.x;
            if (entryIndex < globals.elementCount)
            {
                const SortEntry entry = entryPairBindGroup->inputEntries[entryIndex];
                const uint bucket = radixBucketForPass(entry.sortKey, globals.passIndex);
                atomicAdd(localHistogram[bucket], 1u);
            }
            GroupMemoryBarrierWithGroupSync();

            for (uint bucketIndex = groupIndex; bucketIndex < RadixBucketCount; bucketIndex += WorkGroupSize)
            {
                const uint localCount = atomicLoad(localHistogram[bucketIndex]);
                const uint stateIndex = partitionIndex * RadixBucketCount + bucketIndex;
                prefixDataBindGroup->prefixData[stateIndex].aggregate = localCount;
                prefixDataBindGroup->prefixData[stateIndex].prefix = 0u;
            }
        }
    };

    class [[LocalWorkGroupSize(RadixBucketCount, 1, 1)]] OneSweepResolveOffsetsPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<SortGlobalsBindGroup> sortGlobalsBindGroup [[Slot0]], BindGroup<PrefixDataBindGroup> prefixDataBindGroup [[Slot1]], BindGroup<BucketBaseBindGroup> bucketBaseBindGroup [[Slot2]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]], uint groupIndex [[GroupIndex]])
        {
            GroupShared<uint> bucketTotals[RadixBucketCount];

            const SortGlobals globals = sortGlobalsBindGroup->globals[0];
            const uint bucketIndex = groupIndex;
            uint running = 0u;
            for (uint partitionIndex = 0u; partitionIndex < globals.partitionCount; ++partitionIndex)
            {
                const uint stateIndex = partitionIndex * RadixBucketCount + bucketIndex;
                prefixDataBindGroup->prefixData[stateIndex].prefix = running;
                const uint aggregate = prefixDataBindGroup->prefixData[stateIndex].aggregate;
                running += aggregate;
            }
            bucketTotals[bucketIndex] = running;
            GroupMemoryBarrierWithGroupSync();

            if (groupIndex == 0u)
            {
                uint bucketRunning = 0u;
                for (uint scanIndex = 0u; scanIndex < RadixBucketCount; ++scanIndex)
                {
                    const uint value = bucketTotals[scanIndex];
                    bucketTotals[scanIndex] = bucketRunning;
                    bucketRunning += value;
                }
            }
            GroupMemoryBarrierWithGroupSync();

            bucketBaseBindGroup->bucketBase[bucketIndex] = bucketTotals[bucketIndex];
        }
    };

    class [[LocalWorkGroupSize(WorkGroupSize, 1, 1)]] OneSweepScatterPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<SortGlobalsBindGroup> sortGlobalsBindGroup [[Slot0]], BindGroup<EntryPairBindGroup> entryPairBindGroup [[Slot1]], BindGroup<PrefixDataBindGroup> prefixDataBindGroup [[Slot2]], BindGroup<BucketBaseBindGroup> bucketBaseBindGroup [[Slot3]]
        )
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]], uint3 groupID [[GroupID]], uint groupIndex [[GroupIndex]])
        {
            GroupShared<uint> localBuckets[WorkGroupSize];
            GroupShared<uint> subgroupBucketCounts[SubgroupCount * RadixBucketCount];

            const SortGlobals globals = sortGlobalsBindGroup->globals[0];
            const uint partitionIndex = groupID.x;
            if (partitionIndex >= globals.partitionCount)
            {
                return;
            }

            for (uint bucketIndex = groupIndex; bucketIndex < SubgroupCount * RadixBucketCount; bucketIndex += WorkGroupSize)
            {
                subgroupBucketCounts[bucketIndex] = 0u;
            }
            GroupMemoryBarrierWithGroupSync();

            const uint entryIndex = threadID.x;
            uint bucket = InvalidBucket;
            SortEntry entry = {};
            if (entryIndex < globals.elementCount)
            {
                entry = entryPairBindGroup->inputEntries[entryIndex];
                bucket = radixBucketForPass(entry.sortKey, globals.passIndex);
            }
            localBuckets[groupIndex] = bucket;
            GroupMemoryBarrierWithGroupSync();

            const uint subgroupIndex = groupIndex / SubgroupSize;
            const uint subgroupLane = groupIndex % SubgroupSize;
            if (subgroupLane == 0u)
            {
                const uint subgroupBase = subgroupIndex * SubgroupSize;
                const uint subgroupCountBase = subgroupIndex * RadixBucketCount;
                for (uint laneIndex = 0u; laneIndex < SubgroupSize; ++laneIndex)
                {
                    const uint subgroupBucket = localBuckets[subgroupBase + laneIndex];
                    if (subgroupBucket < RadixBucketCount)
                    {
                        subgroupBucketCounts[subgroupCountBase + subgroupBucket] = subgroupBucketCounts[subgroupCountBase + subgroupBucket] + 1u;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();

            if (entryIndex >= globals.elementCount)
            {
                return;
            }

            uint localRank = 0u;
            const uint subgroupBase = subgroupIndex * SubgroupSize;
            for (uint laneIndex = 0u; laneIndex < subgroupLane; ++laneIndex)
            {
                if (localBuckets[subgroupBase + laneIndex] == bucket)
                {
                    localRank += 1u;
                }
            }
            for (uint previousSubgroup = 0u; previousSubgroup < subgroupIndex; ++previousSubgroup)
            {
                localRank += subgroupBucketCounts[previousSubgroup * RadixBucketCount + bucket];
            }

            const uint stateIndex = partitionIndex * RadixBucketCount + bucket;
            const uint writeIndex = bucketBaseBindGroup->bucketBase[bucket] + prefixDataBindGroup->prefixData[stateIndex].prefix + localRank;
            entryPairBindGroup->outputEntries[writeIndex] = entry;
        }
    };

    class [[LocalWorkGroupSize(1, 1, 1)]] SignalCompletionPass final : public IComputeClass
    {
    public:
        constructor(BindGroup<CompletionBindGroup> completionBindGroup [[Slot0]])
        {
        }

    private:
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            if (threadID.x == 0u)
            {
                atomicStore(completionBindGroup->completionValue[0], 1u);
            }
        }
    };
} // namespace OneSweepSortTest

#endif
