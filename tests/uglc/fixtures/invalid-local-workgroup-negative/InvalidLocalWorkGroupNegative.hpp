#ifndef UGLC_TEST_INVALID_LOCAL_WORKGROUP_NEGATIVE_HPP
#define UGLC_TEST_INVALID_LOCAL_WORKGROUP_NEGATIVE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidLocalWorkGroupNegativeBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(-1, 1, 1)]] InvalidLocalWorkGroupNegativePass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidLocalWorkGroupNegativeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = bindGroup->values[threadID.x] + 1u;
    }
};

#endif
