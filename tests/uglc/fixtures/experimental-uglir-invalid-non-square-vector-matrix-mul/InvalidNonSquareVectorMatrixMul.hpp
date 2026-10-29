#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_NON_SQUARE_VECTOR_MATRIX_MUL_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_NON_SQUARE_VECTOR_MATRIX_MUL_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the output storage used by the non-square vector-matrix diagnostic fixture. */
struct ExperimentalUGLIRInvalidNonSquareVectorMatrixMulBindGroup final : public IBindGroup
{
    /** Creates the bind group with one writable output buffer. */
    constructor(RWStructuredBuffer<uint> output [[Binding0]])
    {
    }
};

/** Exercises the cross-backend diagnostic for a non-square vector-matrix multiplication. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidNonSquareVectorMatrixMulPass final : public IComputeClass
{
public:
    /** Binds the output storage used by the diagnostic fixture. */
    constructor(BindGroup<ExperimentalUGLIRInvalidNonSquareVectorMatrixMulBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Reaches a non-square vector-matrix overload that cannot share the shader ABI across backends. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const float3x2 matrixValue = float3x2(float2(1.0f, 2.0f), float2(3.0f, 4.0f), float2(5.0f, 6.0f));
        const float3 vectorValue = float3(1.0f, 2.0f, 3.0f);
        const float2 product = mul(vectorValue, matrixValue);
        bindGroup->output[threadID.x] = uint(product.x);
    }
};

#endif
