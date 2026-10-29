#ifndef UGLC_TEST_SHADER_BARRIER_BUILTINS_HPP
#define UGLC_TEST_SHADER_BARRIER_BUILTINS_HPP

#include "UGL.h"

using namespace UGL;

struct ShaderBarrierBuiltinsBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(8, 1, 1)]] ShaderBarrierBuiltinsPass final : public IComputeClass
{
public:
    constructor(BindGroup<ShaderBarrierBuiltinsBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]], uint groupIndex [[GroupIndex]])
    {
        const uint index = threadID.x;
        if (groupIndex == 0u)
        {
            bindGroup->values[index] = 1u;
        }

        UGL::DeviceMemoryBarrierWithGroupSync();
        UGL::AllMemoryBarrierWithGroupSync();
        UGL::GroupMemoryBarrierWithGroupSync();
        UGL::DeviceMemoryBarrier();
        UGL::GroupMemoryBarrier();

        bindGroup->values[index] = bindGroup->values[index] + 1u;
    }
};

#endif
