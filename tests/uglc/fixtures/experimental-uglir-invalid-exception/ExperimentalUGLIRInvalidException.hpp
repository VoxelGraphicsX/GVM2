#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_EXCEPTION_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_EXCEPTION_HPP

#include "UGL.h"

using namespace UGL;

inline uint experimentalUGLIRThrowWhenPositive(uint value)
{
    if (value > 0u)
    {
        throw value;
    }
    return value;
}

struct ExperimentalUGLIRInvalidExceptionBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidExceptionPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidExceptionBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = experimentalUGLIRThrowWhenPositive(threadID.x);
    }
};

#endif
