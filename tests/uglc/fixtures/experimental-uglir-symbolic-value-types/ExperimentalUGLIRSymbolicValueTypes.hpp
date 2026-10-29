#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_SYMBOLIC_VALUE_TYPES_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_SYMBOLIC_VALUE_TYPES_HPP

#include "UGL.h"

using namespace UGL;

/** Binds a writable buffer that keeps symbolic value-type expressions shader-reachable. */
struct ExperimentalUGLIRSymbolicValueTypesBindGroup final : public IBindGroup
{
    /** Declares the output buffer used by the symbolic value-type fixture. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Exercises scalar, vector, matrix, swizzle, cast, and construct metadata in the experimental UGLIR path. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRSymbolicValueTypesPass final : public IComputeClass
{
public:
    /** Stores the bind group consumed by the value-type metadata compute pass. */
    constructor(BindGroup<ExperimentalUGLIRSymbolicValueTypesBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Emits representative value-type operations that must be described structurally in UGLIR. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const float4 sourceFloat = float4(1.0f, 2.0f, 3.0f, 4.0f);
        const half4 explicitHalf = half4(sourceFloat);
        const int3 signedVector = int3(-1, 2, 3);
        const uint3 unsignedVector = uint3(1u, 2u, 3u);
        const bool2 boolPair = bool2(true, threadID.x == 0u);

        float4 swizzled = float4(0.0f, 0.0f, 0.0f, 1.0f);
        swizzled.xyz = float3(sourceFloat.x, float(unsignedVector.y), float(signedVector.z));
        const float2 readSwizzle = swizzled.xy;

        const float3x3 matrixValue = float3x3(float3(1.0f, 2.0f, 3.0f),
                                              float3(4.0f, 5.0f, 6.0f),
                                              float3(7.0f, 8.0f, 9.0f));
        const float3 matrixResult = mul(matrixValue, float3(1.0f, 0.5f, 0.25f));

        uint result = uint(readSwizzle.x + matrixResult.y);
        if (all(boolPair))
        {
            result += uint(explicitHalf.w);
        }
        bindGroup->values[threadID.x] = result;
    }
};

#endif
