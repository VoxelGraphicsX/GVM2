#ifndef UGLC_TEST_INVALID_BINDGROUP_HELPER_PARAMETER_OUT_HPP
#define UGLC_TEST_INVALID_BINDGROUP_HELPER_PARAMETER_OUT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBindGroupHelperParameterOutBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace InvalidBindGroupHelperParameterOutHelpers
{
    inline void WriteValue(BindGroup<InvalidBindGroupHelperParameterOutBindGroup> bg INOUT, uint index)
    {
        bg->values[index] = 1u;
    }
} // namespace InvalidBindGroupHelperParameterOutHelpers

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidBindGroupHelperParameterOutPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidBindGroupHelperParameterOutBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        InvalidBindGroupHelperParameterOutHelpers::WriteValue(bindGroup, threadID.x);
    }
};

#endif
