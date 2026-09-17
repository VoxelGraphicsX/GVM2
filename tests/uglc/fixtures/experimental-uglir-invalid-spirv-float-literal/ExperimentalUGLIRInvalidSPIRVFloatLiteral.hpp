#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_SPIRV_BINARY_OPERATOR_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_SPIRV_BINARY_OPERATOR_HPP

#include "UGL.h"

using namespace UGL;

struct ExperimentalUGLIRInvalidSPIRVBinaryOperatorBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidSPIRVBinaryOperatorPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidSPIRVBinaryOperatorBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = threadID.x;
        const uint value = (index, 3u);
        bindGroup->values[index] = value;
    }
};

#endif
