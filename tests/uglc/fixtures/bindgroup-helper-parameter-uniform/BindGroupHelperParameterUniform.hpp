#ifndef UGLC_TEST_BINDGROUP_HELPER_PARAMETER_UNIFORM_HPP
#define UGLC_TEST_BINDGROUP_HELPER_PARAMETER_UNIFORM_HPP

#include "UGL.h"

using namespace UGL;

namespace BindGroupHelperParameterUniformTypes
{
    struct Params
    {
        float4 packedValue;
    };

    inline uint ExtractPackedZ(Params params)
    {
        return uint(params.packedValue.z);
    }
} // namespace BindGroupHelperParameterUniformTypes

struct BindGroupHelperParameterUniformBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<BindGroupHelperParameterUniformTypes::Params> params [[Binding0]],
                RWStructuredBuffer<uint> values [[Binding1]])
    {
    }
};

inline uint BindGroupHelperParameterUniformTopLevel(BindGroup<BindGroupHelperParameterUniformBindGroup> bg)
{
    return uint(bg->params->packedValue.y) + BindGroupHelperParameterUniformTypes::ExtractPackedZ(bg->params->read());
}

namespace BindGroupHelperParameterUniformHelpers
{
    inline uint LoadPackedX(BindGroup<BindGroupHelperParameterUniformBindGroup> bg)
    {
        return uint(bg->params->packedValue.x);
    }

    inline void WriteFirst(BindGroup<BindGroupHelperParameterUniformBindGroup> bg)
    {
        bg->values[0] = LoadPackedX(bg) + BindGroupHelperParameterUniformTopLevel(bg);
    }
} // namespace BindGroupHelperParameterUniformHelpers

class [[LocalWorkGroupSize(1, 1, 1)]] BindGroupHelperParameterUniformPass final : public IComputeClass
{
public:
    constructor(BindGroup<BindGroupHelperParameterUniformBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0)
        {
            BindGroupHelperParameterUniformHelpers::WriteFirst(bindGroup);
        }
    }
};

#endif
