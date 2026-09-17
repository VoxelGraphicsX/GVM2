#ifndef UGLC_TEST_INVALID_EXPLICIT_THIS_POINTER_ACCESS_HPP
#define UGLC_TEST_INVALID_EXPLICIT_THIS_POINTER_ACCESS_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidExplicitThisPointerAccessBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(8, 1, 1)]] InvalidExplicitThisPointerAccessPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidExplicitThisPointerAccessBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = threadID.x;
        this->bindGroup->values[index] = 1u;
    }
};

#endif
