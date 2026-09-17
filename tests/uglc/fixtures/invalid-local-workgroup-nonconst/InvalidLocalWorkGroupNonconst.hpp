#ifndef UGLC_TEST_INVALID_LOCAL_WORKGROUP_NONCONST_HPP
#define UGLC_TEST_INVALID_LOCAL_WORKGROUP_NONCONST_HPP

#include "UGL.h"

using namespace UGL;

static uint InvalidLocalWorkGroupNonconstSize = 4u;

struct InvalidLocalWorkGroupNonconstBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(InvalidLocalWorkGroupNonconstSize, 1, 1)]] InvalidLocalWorkGroupNonconstPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidLocalWorkGroupNonconstBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = bindGroup->values[threadID.x] + 1u;
    }
};

#endif
