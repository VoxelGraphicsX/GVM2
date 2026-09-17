#ifndef UGLC_TEST_BINDGROUP_HELPER_PARAMETER_MULTI_HPP
#define UGLC_TEST_BINDGROUP_HELPER_PARAMETER_MULTI_HPP

#include "UGL.h"

using namespace UGL;

struct BindGroupHelperParameterMultiParams
{
    float4 packedValue;
};

struct BindGroupHelperParameterMultiSampledBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding0]],
                Sampler sampler0 [[Binding1]])
    {
    }
};

struct BindGroupHelperParameterMultiControlBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<BindGroupHelperParameterMultiParams> params [[Binding0]],
                RWStructuredBuffer<uint> values [[Binding1]])
    {
    }
};

namespace BindGroupHelperParameterMultiHelpers
{
    inline uint ComputeValue(BindGroup<BindGroupHelperParameterMultiSampledBindGroup> sampledBg,
                             uint index,
                             BindGroup<BindGroupHelperParameterMultiControlBindGroup> controlBg)
    {
        return uint(sampledBg->texture0->sample(sampledBg->sampler0, controlBg->params->packedValue.xy).x * 255.0f)
               + uint(controlBg->params->packedValue.z)
               + index;
    }

    inline void WriteValue(BindGroup<BindGroupHelperParameterMultiSampledBindGroup> sampledBg,
                           uint index,
                           BindGroup<BindGroupHelperParameterMultiControlBindGroup> controlBg)
    {
        controlBg->values[index] = ComputeValue(sampledBg, index, controlBg);
    }
} // namespace BindGroupHelperParameterMultiHelpers

class [[LocalWorkGroupSize(1, 1, 1)]] BindGroupHelperParameterMultiPass final : public IComputeClass
{
public:
    constructor(BindGroup<BindGroupHelperParameterMultiSampledBindGroup> sampledBindGroup [[Slot0]],
                BindGroup<BindGroupHelperParameterMultiControlBindGroup> controlBindGroup [[Slot1]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        BindGroupHelperParameterMultiHelpers::WriteValue(sampledBindGroup, threadID.x, controlBindGroup);
    }
};

#endif
