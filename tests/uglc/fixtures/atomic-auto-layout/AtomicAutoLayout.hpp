#ifndef UGLC_TEST_ATOMIC_AUTO_LAYOUT_HPP
#define UGLC_TEST_ATOMIC_AUTO_LAYOUT_HPP

#include "UGL.h"

using namespace UGL;

struct AtomicAutoLayoutCounter
{
    uint keyCounter;
    uint maskCounter;
    uint ordinaryCounter;
};

struct AtomicAutoLayoutBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> counters [[Binding0]],
                RWStructuredBuffer<AtomicAutoLayoutCounter> records [[Binding1]],
                StructuredBuffer<AtomicAutoLayoutCounter> readonlyRecords [[Binding2]],
                RWStructuredBuffer<uint> masks [[Binding3]])
    {
    }
};

class [[LocalWorkGroupSize(32, 1, 1)]] AtomicAutoLayoutPass final : public IComputeClass
{
public:
    constructor(BindGroup<AtomicAutoLayoutBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]], uint groupIndex [[GroupIndex]])
    {
        GroupShared<uint> scratch[1];
        GroupShared<uint> andScratch[1];

        if (groupIndex == 0u)
        {
            atomicStore(scratch[0], 0u);
        }

        atomicOr(scratch[0], 1u << (groupIndex & 31u));
        atomicAnd(andScratch[0], 0xfffffffeu);
        const uint counterBase = atomicAdd(bindGroup->counters[threadID.x], 1u);
        const uint maskBase = atomicAnd(bindGroup->masks[threadID.x], 0xfffffff0u);
        atomicAdd(bindGroup->records[0].keyCounter, counterBase);
        atomicAnd(bindGroup->records[0].maskCounter, maskBase);
        const uint keyCounter = atomicLoad(bindGroup->readonlyRecords[0].keyCounter);
        bindGroup->records[0].ordinaryCounter = keyCounter + atomicLoad(scratch[0]);
    }
};

#endif
