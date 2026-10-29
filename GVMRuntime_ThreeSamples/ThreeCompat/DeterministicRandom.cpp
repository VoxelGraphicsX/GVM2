#include "DeterministicRandom.hpp"

namespace GVM::ThreeSamples::ThreeCompat
{
    DeterministicRandom::DeterministicRandom(uint32_t seed)
    {
        reset(seed);
    }

    void DeterministicRandom::reset(uint32_t seed)
    {
        state = seed == 0u ? ZeroSeedState : seed;
    }

    uint32_t DeterministicRandom::nextUint32()
    {
        uint32_t value = state;
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        state = value;
        return value;
    }

    float DeterministicRandom::nextFloat()
    {
        constexpr float InverseTwentyFourBitRange = 1.0f / 16777216.0f;
        return static_cast<float>(nextUint32() >> 8u) * InverseTwentyFourBitRange;
    }

    uint32_t DeterministicRandom::getState() const
    {
        return state;
    }
} // namespace GVM::ThreeSamples::ThreeCompat
