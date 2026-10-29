#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_NON_SQUARE_MATRIX_MATRIX_MUL_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_NON_SQUARE_MATRIX_MATRIX_MUL_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the output storage used by the non-square matrix-matrix diagnostic fixture. */
struct ExperimentalUGLIRInvalidNonSquareMatrixMatrixMulBindGroup final : public IBindGroup
{
    /** Creates the bind group with one writable output buffer. */
    constructor(RWStructuredBuffer<uint> output [[Binding0]])
    {
    }
};

/** Exercises the cross-backend diagnostic for a non-square matrix-matrix multiplication. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidNonSquareMatrixMatrixMulPass final : public IComputeClass
{
public:
    /** Binds the output storage used by the diagnostic fixture. */
    constructor(BindGroup<ExperimentalUGLIRInvalidNonSquareMatrixMatrixMulBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Reaches a non-square matrix-matrix overload that cannot share the shader ABI across backends. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const float3x2 lhsMatrix = float3x2(float2(1.0f, 2.0f), float2(3.0f, 4.0f), float2(5.0f, 6.0f));
        const float2x4 rhsMatrix = float2x4(float4(7.0f, 8.0f, 9.0f, 10.0f), float4(11.0f, 12.0f, 13.0f, 14.0f));
        const float3x4 product = mul(lhsMatrix, rhsMatrix);
        bindGroup->output[threadID.x] = uint(product[0][0]);
    }
};

#endif
