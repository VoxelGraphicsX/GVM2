#ifndef UGLC_TEST_MSL_ATOMIC_STRUCTURED_BUFFER_HELPER_PARAMETER_HPP
#define UGLC_TEST_MSL_ATOMIC_STRUCTURED_BUFFER_HELPER_PARAMETER_HPP

#include "UGL.h"

using namespace UGL;

struct MSLAtomicStructuredBufferHelperParameterBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> payloads [[Binding0]],
                RWStructuredBuffer<uint> counter [[Binding1]],
                RWStructuredBuffer<uint> overflow [[Binding2]])
    {
    }
};

namespace MSLAtomicStructuredBufferHelperParameterHelpers
{
    inline uint allocateIndex(RWStructuredBuffer<uint> counter, RWStructuredBuffer<uint> overflow, uint capacity)
    {
        const uint index = atomicAdd(counter[0], 1u);
        if (index >= capacity)
        {
            atomicMax(overflow[0], 1u);
        }
        return index;
    }
} // namespace MSLAtomicStructuredBufferHelperParameterHelpers

class [[LocalWorkGroupSize(1, 1, 1)]] MSLAtomicStructuredBufferHelperParameterPass final : public IComputeClass
{
public:
    constructor(BindGroup<MSLAtomicStructuredBufferHelperParameterBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = MSLAtomicStructuredBufferHelperParameterHelpers::allocateIndex(bindGroup->counter, bindGroup->overflow, 64u);
        if (index < 64u)
        {
            bindGroup->payloads[index] = threadID.x;
        }
    }
};

#endif
