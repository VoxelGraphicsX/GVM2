#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_VIRTUAL_DISPATCH_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_VIRTUAL_DISPATCH_HPP

#include "UGL.h"

using namespace UGL;

struct ExperimentalUGLIRVirtualDispatchOps
{
    virtual uint value(uint input)
    {
        return input + 1u;
    }
};

struct ExperimentalUGLIRInvalidVirtualDispatchBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidVirtualDispatchPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidVirtualDispatchBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        ExperimentalUGLIRVirtualDispatchOps ops;
        bindGroup->values[threadID.x] = ops.value(threadID.x);
    }
};

#endif
