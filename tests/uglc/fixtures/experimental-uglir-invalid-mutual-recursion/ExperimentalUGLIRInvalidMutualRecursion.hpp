#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_MUTUAL_RECURSION_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_MUTUAL_RECURSION_HPP

#include "UGL.h"

using namespace UGL;

inline uint experimentalUGLIRMutualRecursionB(uint value);

inline uint experimentalUGLIRMutualRecursionA(uint value)
{
    return value == 0u ? 0u : experimentalUGLIRMutualRecursionB(value - 1u);
}

inline uint experimentalUGLIRMutualRecursionB(uint value)
{
    return value == 0u ? 0u : experimentalUGLIRMutualRecursionA(value - 1u);
}

struct ExperimentalUGLIRInvalidMutualRecursionBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidMutualRecursionPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidMutualRecursionBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = experimentalUGLIRMutualRecursionA(threadID.x);
    }
};

#endif
