#ifndef UGLC_TEST_BINDGROUP_HELPER_PARAMETER_MEMBER_UNIFORM_HPP
#define UGLC_TEST_BINDGROUP_HELPER_PARAMETER_MEMBER_UNIFORM_HPP

#include "UGL.h"

using namespace UGL;

struct BindGroupHelperParameterMemberUniformParams
{
    float4 packedValue;
};

struct BindGroupHelperParameterMemberUniformBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<BindGroupHelperParameterMemberUniformParams> params [[Binding0]],
                StructuredBuffer<uint> inputValues [[Binding1]],
                RWStructuredBuffer<uint> outputValues [[Binding2]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] BindGroupHelperParameterMemberUniformPass final : public IComputeClass
{
public:
    constructor(BindGroup<BindGroupHelperParameterMemberUniformBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    uint loadValue(BindGroup<BindGroupHelperParameterMemberUniformBindGroup> bg, uint index)
    {
        return uint(bg->params->packedValue.x) + bg->inputValues[index];
    }

    void storeValue(BindGroup<BindGroupHelperParameterMemberUniformBindGroup> bg, uint index)
    {
        bg->outputValues[index] = loadValue(bg, index) + uint(bg->params->packedValue.y);
    }

    void compute(uint3 threadID [[DispatchThreadID]])
    {
        storeValue(bindGroup, threadID.x);
    }
};

#endif
