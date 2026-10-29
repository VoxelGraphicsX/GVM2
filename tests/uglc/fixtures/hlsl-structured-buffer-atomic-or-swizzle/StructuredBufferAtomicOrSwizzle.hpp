#ifndef UGLC_TEST_HLSL_STRUCTURED_BUFFER_ATOMIC_OR_SWIZZLE_HPP
#define UGLC_TEST_HLSL_STRUCTURED_BUFFER_ATOMIC_OR_SWIZZLE_HPP

#include "UGL.h"

using namespace UGL;

/**
 * Stores atomic regression state inside a structured-buffer element.
 *
 * Use this element shape to verify HLSL lowering for scalar lvalues selected from vector fields.
 */
struct StructuredBufferAtomicOrSwizzleCell final
{
    uint4 stateInfo;
    uint scalarMask;
    uint scalarMaskPadding0;
    uint scalarMaskPadding1;
    uint scalarMaskPadding2;
};

/**
 * Binds the writable cell buffer used by the structured-buffer atomic swizzle regression.
 *
 * The buffer is intentionally structured so atomic targets include both vector-field swizzles and scalar fields.
 */
struct StructuredBufferAtomicOrSwizzleBindGroup final : public IBindGroup
{
    /**
     * Binds the writable cells that receive InterlockedOr operations.
     */
    constructor(RWStructuredBuffer<StructuredBufferAtomicOrSwizzleCell> cells [[Binding0]])
    {
    }
};

/**
 * Exercises HLSL InterlockedOr lowering on RWStructuredBuffer struct-field swizzle lvalues.
 *
 * The pass covers both ignored and consumed atomic return values so generated temporary result types stay valid HLSL.
 */
class [[LocalWorkGroupSize(8, 1, 1)]] StructuredBufferAtomicOrSwizzlePass final : public IComputeClass
{
public:
    /**
     * Binds the regression cell buffer.
     */
    constructor(BindGroup<StructuredBufferAtomicOrSwizzleBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /**
     * Publishes one bit through a uint4 field swizzle and consumes one returned old value.
     */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint cellIndex = threadID.x;
        const uint bit = 1u << (threadID.x & 31u);

        atomicOr(bindGroup->cells[cellIndex].stateInfo.y, bit);
        const uint previous = atomicOr(bindGroup->cells[cellIndex].stateInfo.z, bit);
        if ((previous & bit) == 0u)
        {
            atomicOr(bindGroup->cells[cellIndex].scalarMask, bit);
        }
    }
};

#endif
