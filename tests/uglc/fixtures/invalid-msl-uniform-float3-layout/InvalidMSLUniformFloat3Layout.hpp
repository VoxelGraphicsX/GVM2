#ifndef UGLC_TEST_INVALID_MSL_UNIFORM_FLOAT3_LAYOUT_HPP
#define UGLC_TEST_INVALID_MSL_UNIFORM_FLOAT3_LAYOUT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidMSLUniformFloat3LayoutParams
{
    float3 direction;
    float strength;
};

struct InvalidMSLUniformFloat3LayoutBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<InvalidMSLUniformFloat3LayoutParams> params [[Binding0]],
                RWStructuredBuffer<uint> values [[Binding1]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidMSLUniformFloat3LayoutPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidMSLUniformFloat3LayoutBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0)
        {
            bindGroup->values[0] = uint(bindGroup->params->strength + bindGroup->params->direction.x);
        }
    }
};

#endif
