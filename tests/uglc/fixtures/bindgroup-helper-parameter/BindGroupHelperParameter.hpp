#ifndef UGLC_TEST_BINDGROUP_HELPER_PARAMETER_HPP
#define UGLC_TEST_BINDGROUP_HELPER_PARAMETER_HPP

#include "UGL.h"

using namespace UGL;

struct BindGroupHelperParameterBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding0]],
                Sampler sampler0 [[Binding1]],
                RWStructuredBuffer<uint> values [[Binding2]])
    {
    }
};

namespace BindGroupHelperParameterHelpers
{
    inline uint SampleRed(BindGroup<BindGroupHelperParameterBindGroup> bg, float2 uv)
    {
        return uint(bg->texture0->sample(bg->sampler0, uv).x * 255.0f);
    }

    inline void StoreValue(BindGroup<BindGroupHelperParameterBindGroup> bg, uint index, uint value)
    {
        bg->values[index] = value;
    }

    inline void SampleAndStore(BindGroup<BindGroupHelperParameterBindGroup> bg, uint index)
    {
        StoreValue(bg, index, SampleRed(bg, float2(0.5f, 0.25f)));
    }
} // namespace BindGroupHelperParameterHelpers

class [[LocalWorkGroupSize(1, 1, 1)]] BindGroupHelperParameterPass final : public IComputeClass
{
public:
    constructor(BindGroup<BindGroupHelperParameterBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        BindGroupHelperParameterHelpers::SampleAndStore(bindGroup, threadID.x);
    }
};

#endif
