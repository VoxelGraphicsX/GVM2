#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INTRINSIC_CALL_KINDS_COMPUTE_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INTRINSIC_CALL_KINDS_COMPUTE_HPP

#include "UGL.h"

using namespace UGL;

/** Binds writable values used by the compute intrinsic-call metadata fixture. */
struct ExperimentalUGLIRIntrinsicCallKindsComputeBindGroup final : public IBindGroup
{
    /** Declares the storage buffer that receives intrinsic-call results. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Exercises compute-safe intrinsic calls that should lower through IntrinsicCallKind metadata. */
class [[LocalWorkGroupSize(8, 1, 1)]] ExperimentalUGLIRIntrinsicCallKindsComputePass final : public IComputeClass
{
public:
    /** Stores the bind group consumed by the compute intrinsic fixture. */
    constructor(BindGroup<ExperimentalUGLIRIntrinsicCallKindsComputeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Emits math, min/max, bitcast, barrier, atomic, all, sign, and sincos calls. */
    void compute(uint3 threadID [[DispatchThreadID]], uint groupIndex [[GroupIndex]])
    {
        GroupShared<uint> sharedCounter;
        if (groupIndex == 0u)
        {
            atomicStore(sharedCounter, 0u);
        }

        GroupMemoryBarrierWithGroupSync();
        const uint atomicBase = atomicAdd(sharedCounter, 1u);
        DeviceMemoryBarrier();

        float sineValue = 0.0f;
        float cosineValue = 0.0f;
        sincos(ceil(0.25f), sineValue, cosineValue);

        const uint bitPattern = asuint(max(sineValue, cosineValue));
        const float restored = asfloat(bitPattern);
        const float polarAngle = atan2(restored, 0.5f);
        const bool2 boolPair = bool2(restored >= 0.0f, atomicBase < 16u);
        const half3 signedHalf = sign(half3(half(-1.0f), half(0.0f), half(1.0f)));

        uint result = atomicBase + uint(min(restored, 8.0f));
        if (all(boolPair))
        {
            result += uint(signedHalf.z > half(0.0f)) +
                      uint(polarAngle >= 0.0f);
        }
        bindGroup->values[threadID.x] = result;
    }
};

#endif
