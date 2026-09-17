#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_NULLPTR_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_NULLPTR_HPP

#include "UGL.h"

using namespace UGL;

struct ExperimentalUGLIRInvalidNullptrBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidNullptrPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidNullptrBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        auto rawNull = nullptr;
        (void)rawNull;
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
