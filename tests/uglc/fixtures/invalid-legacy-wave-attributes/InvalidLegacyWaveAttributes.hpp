#ifndef UGLC_TEST_INVALID_LEGACY_WAVE_ATTRIBUTES_HPP
#define UGLC_TEST_INVALID_LEGACY_WAVE_ATTRIBUTES_HPP

#include "UGL.h"

using namespace UGL;

class [[LocalWorkGroupSize(8, 1, 1)]] InvalidLegacyWaveAttributesPass final : public IComputeClass
{
public:
    constructor()
    {
    }

private:
    void compute(uint laneIndex [[WaveLaneIndex]], uint laneCount [[WaveLaneCount]], uint3 threadID [[DispatchThreadID]])
    {
        (void)laneIndex;
        (void)laneCount;
        (void)threadID;
    }
};

#endif
