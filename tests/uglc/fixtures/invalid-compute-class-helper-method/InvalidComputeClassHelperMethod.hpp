#ifndef UGLC_TEST_INVALID_COMPUTE_CLASS_HELPER_METHOD_HPP
#define UGLC_TEST_INVALID_COMPUTE_CLASS_HELPER_METHOD_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidComputeClassHelperBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidComputeClassHelperPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidComputeClassHelperBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    uint helper(uint value)
    {
        return value + 1u;
    }

    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
