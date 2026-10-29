#ifndef UGLC_TEST_BINDGROUP_HELPER_PARAMETER_MEMBER_HPP
#define UGLC_TEST_BINDGROUP_HELPER_PARAMETER_MEMBER_HPP

#include "UGL.h"

using namespace UGL;

struct BindGroupHelperParameterMemberBindGroup final : public IBindGroup
{
    constructor(StructuredBuffer<uint> inputValues [[Binding0]],
                RWStructuredBuffer<uint> outputValues [[Binding1]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] BindGroupHelperParameterMemberPass final : public IComputeClass
{
public:
    constructor(BindGroup<BindGroupHelperParameterMemberBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    uint loadValue(BindGroup<BindGroupHelperParameterMemberBindGroup> bg, uint index)
    {
        return bg->inputValues[index] + 3u;
    }

    void storeValue(BindGroup<BindGroupHelperParameterMemberBindGroup> bg, uint index)
    {
        bg->outputValues[index] = loadValue(bg, index);
    }

    void compute(uint3 threadID [[DispatchThreadID]])
    {
        storeValue(bindGroup, threadID.x);
    }
};

#endif
