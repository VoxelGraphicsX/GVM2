#ifndef UGLC_TEST_INVALID_MISSING_LOCAL_WORKGROUP_HPP
#define UGLC_TEST_INVALID_MISSING_LOCAL_WORKGROUP_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidLocalWorkGroupBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class InvalidMissingLocalWorkGroupPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidLocalWorkGroupBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = bindGroup->values[threadID.x] + 1u;
    }
};

#endif
