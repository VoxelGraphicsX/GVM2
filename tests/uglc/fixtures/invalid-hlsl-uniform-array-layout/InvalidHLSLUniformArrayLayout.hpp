#ifndef UGLC_TEST_INVALID_HLSL_UNIFORM_ARRAY_LAYOUT_HPP
#define UGLC_TEST_INVALID_HLSL_UNIFORM_ARRAY_LAYOUT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidHLSLUniformArrayLayoutParams
{
    float exposure;
    float fogPad[3];
    float intensity;
};

struct InvalidHLSLUniformArrayLayoutBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<InvalidHLSLUniformArrayLayoutParams> params [[Binding0]],
                RWStructuredBuffer<uint> values [[Binding1]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidHLSLUniformArrayLayoutPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidHLSLUniformArrayLayoutBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0)
        {
            bindGroup->values[0] = uint(bindGroup->params->intensity + bindGroup->params->exposure);
        }
    }
};

#endif
