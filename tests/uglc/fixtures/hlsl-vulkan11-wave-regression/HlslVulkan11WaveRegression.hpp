#ifndef UGLC_HLSL_VULKAN11_WAVE_REGRESSION_HPP
#define UGLC_HLSL_VULKAN11_WAVE_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

struct HlslVulkan11WaveBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(32, 1, 1)]] HlslVulkan11WavePass final : public IComputeClass
{
public:
    constructor(BindGroup<HlslVulkan11WaveBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint laneIndex = WaveGetLaneIndex();
        const uint laneCount = WaveGetLaneCount();
        const uint selectedLane = laneCount == 0u ? 0u : (laneIndex % laneCount);
        bindGroup->values[threadID.x] = WaveReadLaneAt(threadID.x + laneIndex, selectedLane);
    }
};

#endif
