#ifndef GVM_TEST_RHI_EXPERIMENTAL_UGLIR_HEADLESS_READBACK_SUITE_HPP
#define GVM_TEST_RHI_EXPERIMENTAL_UGLIR_HEADLESS_READBACK_SUITE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores deterministic parameters consumed by the experimental UGLIR headless readback shader. */
struct ExperimentalUGLIRHeadlessReadbackParams
{
    uint addend;
    uint selectSecondBuffer;
};

/** Binds input buffers, a uniform block, writable output storage, and an atomic counter. */
struct ExperimentalUGLIRHeadlessReadbackBindGroup final : public IBindGroup
{
    /** Creates the bind group used by the headless readback validation pass. */
    constructor(UniformBuffer<ExperimentalUGLIRHeadlessReadbackParams> params [[Binding0]],
                StructuredBuffer<uint> valuesA [[Binding1]],
                StructuredBuffer<uint> valuesB [[Binding2]],
                RWStructuredBuffer<uint> output [[Binding3]],
                RWStructuredBuffer<uint> counter [[Binding4]])
    {
    }
};

/** Validates compute intrinsics, resource aliasing, atomics, barriers, and groupshared storage through readback. */
class [[LocalWorkGroupSize(4, 1, 1)]] ExperimentalUGLIRHeadlessReadbackPass final : public IComputeClass
{
public:
    /** Creates the compute pass with the deterministic readback bind group. */
    constructor(BindGroup<ExperimentalUGLIRHeadlessReadbackBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes deterministic values that are compared against a CPU reference by the RHI test. */
    void compute(uint3 threadID [[DispatchThreadID]], uint3 groupThreadID [[GroupThreadID]])
    {
        const uint lane = groupThreadID.x;
        if (lane >= 4u)
        {
            return;
        }

        StructuredBuffer<uint> selectedValues = bindGroup->valuesA;
        if (bindGroup->params->selectSecondBuffer != 0u)
        {
            selectedValues = bindGroup->valuesB;
        }

        const uint selected = selectedValues[lane];
        GroupShared<uint> laneValues[4];
        laneValues[lane] = selected + bindGroup->params->addend;
        GroupMemoryBarrierWithGroupSync();

        bindGroup->output[lane] = laneValues[3u - lane];
        atomicAdd(bindGroup->counter[0], selected);
        DeviceMemoryBarrierWithGroupSync();

        if (lane == 0u)
        {
            bindGroup->output[4] = atomicLoad(bindGroup->counter[0]);

            bool4 comparisons = bool4(bindGroup->valuesA[0] < bindGroup->valuesB[0],
                                      bindGroup->valuesA[1] < bindGroup->valuesB[1],
                                      bindGroup->valuesA[2] < bindGroup->valuesB[2],
                                      bindGroup->valuesA[3] < bindGroup->valuesB[3]);

            half3 signedValues = half3(half(float(bindGroup->valuesB[0])) - half(13.0f),
                                       half(float(bindGroup->valuesA[1])) - half(4.0f),
                                       half(float(bindGroup->valuesA[2])) - half(5.0f));
            half3 signs = sign(signedValues);
            uint signMask = 0u;
            if (signs.x < half(0.0f))
            {
                signMask = signMask + 1u;
            }
            if (signs.y == half(0.0f))
            {
                signMask = signMask + 2u;
            }
            if (signs.z > half(0.0f))
            {
                signMask = signMask + 4u;
            }

            float sinValue = 0.0f;
            float cosValue = 0.0f;
            sincos(0.0f, sinValue, cosValue);
            const uint trigValue = uint((sinValue + cosValue) * 10.0f + 0.5f);
            bindGroup->output[5] = (all(comparisons) ? 100u : 0u) + signMask + trigValue;

            const float bitcastValue = asfloat(0x3f800000u);
            bindGroup->output[6] = asuint(bitcastValue) == 0x3f800000u ? uint(bitcastValue * 9.0f) : 0u;
        }
    }
};

#endif
