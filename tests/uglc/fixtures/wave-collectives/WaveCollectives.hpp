#ifndef UGLC_TEST_WAVE_COLLECTIVES_HPP
#define UGLC_TEST_WAVE_COLLECTIVES_HPP

#include "UGL.h"

using namespace UGL;

namespace WaveCollectiveHelpers
{
    inline uint ComputeWaveSummary(uint value)
    {
        const bool isEven = (value & 1u) == 0u;
        const uint4 ballotMask = WaveActiveBallot(isEven);
        const uint activeEvenCount = WaveActiveCountBits(isEven);
        const uint prefixEvenCount = WavePrefixCountBits(isEven);
        const uint prefixValue = WavePrefixSum(value);
        const uint firstLaneValue = WaveReadLaneFirst(value);
        const uint4 matchMask = WaveMatch(value & 3u);
        return ballotMask.x + activeEvenCount + prefixEvenCount + prefixValue + firstLaneValue + matchMask.x;
    }
} // namespace WaveCollectiveHelpers

struct WaveCollectivesBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(32, 1, 1)]] WaveCollectivesPass final : public IComputeClass
{
public:
    constructor(BindGroup<WaveCollectivesBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = WaveCollectiveHelpers::ComputeWaveSummary(threadID.x);
    }
};

#endif
