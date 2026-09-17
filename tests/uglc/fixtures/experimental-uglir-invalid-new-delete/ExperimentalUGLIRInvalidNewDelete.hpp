#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_NEW_DELETE_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_NEW_DELETE_HPP

#include "UGL.h"

using namespace UGL;

struct ExperimentalUGLIRInvalidNewDeleteBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidNewDeletePass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidNewDeleteBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint *value = new uint(threadID.x);
        bindGroup->values[threadID.x] = *value;
        delete value;
    }
};

#endif
