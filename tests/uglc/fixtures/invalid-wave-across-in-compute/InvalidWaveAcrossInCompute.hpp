#ifndef UGLC_TEST_INVALID_WAVE_ACROSS_IN_COMPUTE_HPP
#define UGLC_TEST_INVALID_WAVE_ACROSS_IN_COMPUTE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidWaveAcrossComputeBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(8, 1, 1)]] InvalidWaveAcrossInComputePass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidWaveAcrossComputeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = WaveReadAcrossX(threadID.x);
    }
};

#endif
