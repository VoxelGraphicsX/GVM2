#ifndef GVM_TEST_RHI_NATIVE_HALF_COMPUTE_HPP
#define GVM_TEST_RHI_NATIVE_HALF_COMPUTE_HPP

#include "UGL.h"

using namespace UGL;

struct NativeHalfComputeBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] NativeHalfComputePass final : public IComputeClass
{
public:
    constructor(BindGroup<NativeHalfComputeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        half value = half(float(threadID.x)) + half(1.5f);
        half scaled = value * half(2.0f) + half(0.25f);
        bindGroup->values[threadID.x] = uint(float(scaled) * 16.0f);
    }
};

#endif
