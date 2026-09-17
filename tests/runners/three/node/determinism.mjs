export const defaultThreeRandomSeed = 0x12345678;
export const zeroThreeRandomSeedState = 0x6d2b79f5;

/** Advances one unsigned xorshift32 state shared with ThreeCompat::DeterministicRandom. */
export function advanceThreeRandomState(state) {
  let value = (Number(state) >>> 0) || zeroThreeRandomSeedState;
  value ^= value << 13;
  value ^= value >>> 17;
  value ^= value << 5;
  return value >>> 0;
}

/** Converts one xorshift32 state to the shared upper-24-bit [0, 1) value. */
export function threeRandomFloatFromState(state) {
  return (Number(state) >>> 8) / 16_777_216;
}
