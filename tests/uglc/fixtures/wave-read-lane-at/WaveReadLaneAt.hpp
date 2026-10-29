#ifndef UGLC_TEST_WAVE_READ_LANE_AT_HPP
#define UGLC_TEST_WAVE_READ_LANE_AT_HPP

#include "UGL.h"

using namespace UGL;

namespace WaveLaneAtHelpers
{
    inline uint BroadcastLaneZero(uint value)
    {
        return WaveReadLaneAt(value, 0u);
    }
} // namespace WaveLaneAtHelpers

struct WaveReadLaneAtBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(32, 1, 1)]] WaveReadLaneAtPass final : public IComputeClass
{
public:
    constructor(BindGroup<WaveReadLaneAtBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = WaveLaneAtHelpers::BroadcastLaneZero(threadID.x);
    }
};

#endif
