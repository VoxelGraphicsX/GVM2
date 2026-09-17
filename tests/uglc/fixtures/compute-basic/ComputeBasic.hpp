#ifndef UGLC_TEST_COMPUTE_BASIC_HPP
#define UGLC_TEST_COMPUTE_BASIC_HPP

#include "UGL.h"

using namespace UGL;

struct ComputeBasicBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(8, 1, 1)]] ComputeBasicPass final : public IComputeClass
{
public:
    constructor(BindGroup<ComputeBasicBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = threadID.x;
        bindGroup->values[index] = bindGroup->values[index] + 1u;
    }
};

#endif
