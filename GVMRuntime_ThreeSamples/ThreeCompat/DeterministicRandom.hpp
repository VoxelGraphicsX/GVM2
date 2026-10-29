#pragma once

#include <cstdint>

namespace GVM::ThreeSamples::ThreeCompat
{
    /** Generates the shared deterministic xorshift32 stream used by Three reference and GVM scenes. */
    class DeterministicRandom final
    {
    public:
        /** Creates a stream from one explicit seed, normalizing zero to a stable nonzero state. */
        explicit DeterministicRandom(uint32_t seed);

        /** Restarts the stream from one explicit seed using the same zero-state normalization. */
        void reset(uint32_t seed);

        /** Returns the next full-width xorshift32 value. */
        [[nodiscard]] uint32_t nextUint32();

        /** Returns the next value in [0, 1) using the upper 24 deterministic bits. */
        [[nodiscard]] float nextFloat();

        /** Returns the current internal state for deterministic scene snapshots. */
        [[nodiscard]] uint32_t getState() const;

    private:
        static constexpr uint32_t ZeroSeedState = 0x6d2b79f5u;
        uint32_t state = ZeroSeedState;
    };
} // namespace GVM::ThreeSamples::ThreeCompat
