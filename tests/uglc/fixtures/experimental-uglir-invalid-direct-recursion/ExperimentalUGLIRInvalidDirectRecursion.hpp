#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_DIRECT_RECURSION_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_DIRECT_RECURSION_HPP

#include "UGL.h"

using namespace UGL;

inline uint experimentalUGLIRDirectRecursion(uint value)
{
    return value == 0u ? 0u : experimentalUGLIRDirectRecursion(value - 1u);
}

struct ExperimentalUGLIRInvalidDirectRecursionBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidDirectRecursionPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidDirectRecursionBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = experimentalUGLIRDirectRecursion(threadID.x);
    }
};

#endif
