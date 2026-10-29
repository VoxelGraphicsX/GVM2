#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_FUNCTION_POINTER_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_FUNCTION_POINTER_HPP

#include "UGL.h"

using namespace UGL;

inline uint experimentalUGLIRFunctionPointerAdd(uint value)
{
    return value + 1u;
}

struct ExperimentalUGLIRInvalidFunctionPointerBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidFunctionPointerPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidFunctionPointerBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint (*fn)(uint) = experimentalUGLIRFunctionPointerAdd;
        bindGroup->values[threadID.x] = fn(threadID.x);
    }
};

#endif
