#ifndef UGLC_TEST_IMPLICIT_THIS_SHADER_HELPER_REGRESSION_HPP
#define UGLC_TEST_IMPLICIT_THIS_SHADER_HELPER_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

struct ImplicitThisShaderHelperRegressionBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(8, 1, 1)]] ImplicitThisShaderHelperRegressionPass final : public IComputeClass
{
public:
    constructor(BindGroup<ImplicitThisShaderHelperRegressionBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    uint loadValue(uint index)
    {
        return bindGroup->values[index];
    }

    uint increment(uint value)
    {
        return value + 1u;
    }

    void storeValue(uint index, uint value)
    {
        bindGroup->values[index] = value;
    }

    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = threadID.x;
        storeValue(index, increment(loadValue(index)));
    }
};

#endif
