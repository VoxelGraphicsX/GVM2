#ifndef UGLC_TEST_COMPUTE_CONSTRUCTOR_ORDER_HPP
#define UGLC_TEST_COMPUTE_CONSTRUCTOR_ORDER_HPP

#include "UGL.h"

using namespace UGL;

struct ComputeConstructorOrderBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(4, 1, 1)]] ComputeConstructorOrderPass final : public IComputeClass
{
public:
    uint constructorMarker = 0u;

    constructor(BindGroup<ComputeConstructorOrderBindGroup> bindGroup [[Slot0]])
    {
        this->constructorMarker = 19u;
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
