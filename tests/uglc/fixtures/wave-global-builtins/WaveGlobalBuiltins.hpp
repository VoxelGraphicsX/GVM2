#ifndef UGLC_TEST_WAVE_GLOBAL_BUILTINS_HPP
#define UGLC_TEST_WAVE_GLOBAL_BUILTINS_HPP

#include "UGL.h"

using namespace UGL;

namespace WaveHelpers
{
    inline uint AccumulateWaveInfo(uint baseValue)
    {
        return baseValue + WaveGetLaneIndex() + WaveGetLaneCount();
    }
} // namespace WaveHelpers

struct WaveGlobalBuiltinsBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(32, 1, 1)]] WaveGlobalBuiltinsPass final : public IComputeClass
{
public:
    constructor(BindGroup<WaveGlobalBuiltinsBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint __uglc_hidden_wave_lane_index = 11u;
        const uint __uglc_hidden_wave_lane_count = 22u;
        bindGroup->values[threadID.x] = WaveHelpers::AccumulateWaveInfo(threadID.x);
    }
};

#endif
