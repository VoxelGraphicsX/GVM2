#ifndef UGLC_TEST_ATOMIC_ORDINARY_ACCESS_HPP
#define UGLC_TEST_ATOMIC_ORDINARY_ACCESS_HPP

#include "UGL.h"

using namespace UGL;

/**
 * Stores one inferred atomic counter beside an ordinary field used to verify mixed atomic and plain access lowering.
 */
struct AtomicOrdinaryAccessState
{
    uint counter;
    uint ordinary;
};

/**
 * Binds scalar and structured writable buffers used by the atomic ordinary-access regression shader.
 */
struct AtomicOrdinaryAccessBindGroup final : public IBindGroup
{
    /**
     * Binds the scalar counter buffer and structured state buffer for the regression shader.
     */
    constructor(RWStructuredBuffer<uint> counters [[Binding0]],
                RWStructuredBuffer<AtomicOrdinaryAccessState> states [[Binding1]])
    {
    }
};

/**
 * Verifies that ordinary accesses to Metal atomic-backed storage stay non-atomic in DSL semantics.
 */
class [[LocalWorkGroupSize(32, 1, 1)]] AtomicOrdinaryAccessPass final : public IComputeClass
{
public:
    /**
     * Binds the resources used by the ordinary atomic-backed access regression pass.
     */
    constructor(BindGroup<AtomicOrdinaryAccessBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /**
     * Mixes explicit atomic operations with ordinary reads and writes to the same inferred atomic storage.
     */
    void compute(uint3 threadID [[DispatchThreadID]], uint groupIndex [[GroupIndex]])
    {
        GroupShared<uint> scratch[1];

        if (groupIndex == 0u)
        {
            atomicStore(scratch[0], 0u);
        }

        atomicAdd(bindGroup->counters[threadID.x], 1u);
        bindGroup->counters[threadID.x] += 1u;
        const uint counterValue = bindGroup->counters[threadID.x];
        bindGroup->counters[threadID.x] = counterValue + 3u;

        atomicAdd(bindGroup->states[0].counter, 1u);
        bindGroup->states[0].counter += counterValue;
        const uint stateCounter = bindGroup->states[0].counter;
        bindGroup->states[0].counter = stateCounter + atomicLoad(scratch[0]);

        scratch[0] = scratch[0] + 1u;
        const uint scratchValue = scratch[0];
        scratch[0] = scratchValue + 2u;

        bindGroup->states[0].ordinary = scratchValue;
    }
};

#endif
