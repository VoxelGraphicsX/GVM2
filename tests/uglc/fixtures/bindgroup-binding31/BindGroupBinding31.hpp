#ifndef UGLC_TEST_BINDGROUP_BINDING31_HPP
#define UGLC_TEST_BINDGROUP_BINDING31_HPP

#include "UGL.h"

using namespace UGL;

struct Binding31BindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding31]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] Binding31Pass final : public IComputeClass
{
public:
    constructor(BindGroup<Binding31BindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0u)
        {
            bindGroup->values[0] = 1u;
        }
    }
};

#endif
