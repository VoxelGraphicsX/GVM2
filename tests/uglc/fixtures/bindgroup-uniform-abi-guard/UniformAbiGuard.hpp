#ifndef UGLC_TEST_BINDGROUP_UNIFORM_ABI_GUARD_HPP
#define UGLC_TEST_BINDGROUP_UNIFORM_ABI_GUARD_HPP

#include "UGL.h"

using namespace UGL;

struct UniformAbiGuardParams
{
    float4 packedValue;
};

struct UniformAbiGuardBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<UniformAbiGuardParams> params [[Binding0]],
                RWStructuredBuffer<uint> values [[Binding1]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] UniformAbiGuardPass final : public IComputeClass
{
public:
    constructor(BindGroup<UniformAbiGuardBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0)
        {
            bindGroup->values[0] = bindGroup->values[0];
        }
    }
};

#endif
