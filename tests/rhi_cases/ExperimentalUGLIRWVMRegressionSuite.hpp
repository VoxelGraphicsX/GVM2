#ifndef GVM_TEST_RHI_EXPERIMENTAL_UGLIR_WVM_REGRESSION_SUITE_HPP
#define GVM_TEST_RHI_EXPERIMENTAL_UGLIR_WVM_REGRESSION_SUITE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores a small shader-visible value used to validate fixed array parameters and member method calls. */
struct ExperimentalUGLIRWVMRegressionRecord
{
    uint value;

    /** Returns the record value plus the caller-provided addend without requiring a free-function rewrite. */
    uint add(uint addend)
    {
        return value + addend;
    }
};

/** Preserves explicit and delegating constructor initialization and body side effects. */
struct ExperimentalUGLIRConstructedValue
{
    uint value;
    uint next;

    /** Initializes members in declaration order and executes the constructor body. */
    ExperimentalUGLIRConstructedValue(uint seed) : value(seed + 1u), next(value + 2u)
    {
        value += 3u;
    }

    /** Delegates initialization to the resolved one-argument constructor. */
    ExperimentalUGLIRConstructedValue(uint seed, uint extra) : ExperimentalUGLIRConstructedValue(seed)
    {
        next += extra;
    }
};

/** Distinguishes default initialization from value initialization with default members. */
struct ExperimentalUGLIRDefaultMembers
{
    float4x4 matrix;
    float4 vectors[3];
    uint ordered[3] = {3u, 5u, 7u};
    uint zero;
    uint value = 9u;
};

/** Ensures aggregate initialization overrides a const field's default initializer. */
struct ExperimentalUGLIRConstAggregate
{
    const uint value = 1u;
};

/** Distinguishes user-defined assignment, arithmetic and conversion bodies from builtin operators. */
struct ExperimentalUGLIROperatorValue
{
    uint value;
    /** Adds a visible adjustment when assigning another value record. */
    void operator=(const ExperimentalUGLIROperatorValue &other) { value = other.value + 10u; }
    /** Adds a distinct adjustment through an ordinary resolved operator overload. */
    uint operator+(uint addend) const { return value + addend + 20u; }
    /** Converts through the declared operator body instead of an inferred cast. */
    operator uint() const { return value + 30u; }
};

/** Exercises Boolean fields in a local aggregate across SPIR-V optimization and translation. */
struct ExperimentalUGLIRBooleanAggregate
{
    float4x4 matrix;
    bool2 flags;
    uint zero;
    uint padding = 1u;
};

/** Binds read-only input buffers and a writable output buffer for WVM-inspired compute regression coverage. */
struct ExperimentalUGLIRWVMRegressionComputeBindGroup final : public IBindGroup
{
    /** Creates the bind group used by the compute regression pass. */
    constructor(StructuredBuffer<uint> valuesA [[Binding0]],
                StructuredBuffer<uint> valuesB [[Binding1]],
                RWStructuredBuffer<uint> output [[Binding2]])
    {
    }
};

/** Reads one input resource without requiring the other resources in the same bind group. */
inline uint experimentalUGLIRWVMRegressionReadFirstResource(BindGroup<ExperimentalUGLIRWVMRegressionComputeBindGroup> group, uint index)
{
    return group->valuesA[index];
}

/** Forwards the input read to verify that Metal retains transitive helper resource requirements. */
inline uint experimentalUGLIRWVMRegressionForwardFirstResource(BindGroup<ExperimentalUGLIRWVMRegressionComputeBindGroup> group, uint index)
{
    return experimentalUGLIRWVMRegressionReadFirstResource(group, index);
}

/** Binds one sampled texture and sampler for derivative and explicit texture sampling regression coverage. */
struct ExperimentalUGLIRWVMRegressionTextureBindGroup final : public IBindGroup
{
    /** Creates the bind group used by the render regression pass. */
    constructor(Texture2D<float4> sourceTexture [[Binding0]],
                Sampler sourceSampler [[Binding1]])
    {
    }
};

/** Carries full-screen triangle position and texture coordinates from vertex to fragment stages. */
struct ExperimentalUGLIRWVMRegressionVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the single color attachment used by the derivative render regression pass. */
struct ExperimentalUGLIRWVMRegressionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Returns a value from a fixed-size shader array through the original array-parameter authoring shape. */
inline uint experimentalUGLIRWVMRegressionReadFixedArray(ExperimentalUGLIRWVMRegressionRecord records[3], uint index)
{
    return records[index].add(7u);
}

/** Returns the outer variable after a same-type nested shadow ends. */
inline uint experimentalUGLIRWVMRegressionSameTypeShadow(uint input)
{
    uint value = input;
    {
        uint value = 2u;
        value += 3u;
    }
    return value + 10u;
}

/** Returns the outer variable after a different-type nested shadow ends. */
inline uint experimentalUGLIRWVMRegressionDifferentTypeShadow(uint input)
{
    uint value = input;
    {
        float value = 2.0f;
        value += 3.0f;
    }
    return value + 10u;
}

/** Keeps a helper parameter distinct from a same-name variable in an inner block. */
inline uint experimentalUGLIRWVMRegressionParameterShadow(uint input)
{
    uint result = input;
    {
        uint input = 7u;
        result += input;
    }
    return result;
}

/** Keeps an outer loop variable and loop-body variable distinct after the loop. */
inline uint experimentalUGLIRWVMRegressionLoopShadow(uint input)
{
    uint index = input;
    uint result = input;
    for (uint index = 0u; index < 2u; ++index)
    {
        uint result = index + 1u;
        result += 1u;
    }
    return index + result + 9u;
}

/** Preserves a binary32 value after a nested half variable with the same spelling. */
inline uint experimentalUGLIRWVMRegressionGTAODistance(uint input)
{
    {
        half dist = half(0.0f);
        dist += half(1.0f);
    }
    float dist = float(input);
    return uint(dist);
}

static constexpr uint experimentalUGLIRWVMRegressionStaticScalar = 7u;
static const half experimentalUGLIRWVMRegressionStaticHalf = half(3.0f);
static const float2 experimentalUGLIRWVMRegressionStaticFloatVectors[2] = {
    float2(2.0f, 3.0f),
    float2(4.0f, 5.0f),
};
static const half2 experimentalUGLIRWVMRegressionStaticHalfVectors[2] = {
    half2(1.0f, 2.0f),
    half2(3.0f, 4.0f),
};

/** Returns a distinct marker for bool, signed-int, and unsigned-int auto NTTP specializations. */
template <auto Value>
inline uint experimentalUGLIRWVMRegressionAutoNttpMarker()
{
    if constexpr (__is_same(decltype(Value), bool))
    {
        return 1u;
    }
    else if constexpr (__is_same(decltype(Value), int))
    {
        return 2u;
    }
    else
    {
        return 3u;
    }
}

/** Stores a value in a record whose identity depends on an auto non-type template argument. */
template <auto Value>
struct ExperimentalUGLIRWVMRegressionAutoRecord
{
    uint value;
};

/** Assigns through a mutable reference and then reads a const alias to the same object. */
inline uint experimentalUGLIRWVMRegressionReadAlias(uint &a, const uint &b)
{
    a = 3u;
    return b;
}

/** Forwards a mutable and const alias pair without changing their source identity. */
inline uint experimentalUGLIRWVMRegressionForwardAlias(uint &a, const uint &b)
{
    return experimentalUGLIRWVMRegressionReadAlias(a, b);
}

/** Writes sine through a mutable alias and then reads the same object through a const alias. */
inline float experimentalUGLIRWVMRegressionReadSincosAlias(float &a, const float &b)
{
    float c;
    sincos(0.0f, a, c);
    return b;
}

/** Calls the mutable/const alias helper through a value parameter and returns its local mutation. */
inline uint experimentalUGLIRWVMRegressionReadValueAlias(uint value)
{
    experimentalUGLIRWVMRegressionReadAlias(value, value);
    return value;
}

/** Exercises WVM-inspired compute intrinsics and resource aliasing through direct UGLIR SPIR-V. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRWVMRegressionComputePass final : public IComputeClass
{
public:
    /** Creates the compute pass with the regression bind group. */
    constructor(BindGroup<ExperimentalUGLIRWVMRegressionComputeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes one deterministic result for each regression feature into the output buffer. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x != 0u)
        {
            return;
        }

        float sinValue = 0.0f;
        float cosValue = 0.0f;
        sincos(0.0f, sinValue, cosValue);
        bindGroup->output[0] = uint((sinValue + cosValue) * 10.0f + 0.5f);

        bool2 flags = bool2(bindGroup->valuesA[0] < bindGroup->valuesB[0],
                            bindGroup->valuesA[1] < bindGroup->valuesB[1]);
        bindGroup->output[1] = all(flags) ? 1u : 0u;

        half3 signInput = half3(half(float(experimentalUGLIRWVMRegressionForwardFirstResource(bindGroup, 0u))) - half(2.0f),
                                half(float(bindGroup->valuesA[1])) - half(2.0f),
                                half(float(bindGroup->valuesB[0])) - half(2.0f));
        half3 signValue = sign(signInput);
        uint signMask = 0u;
        if (signValue.x < half(0.0f))
        {
            signMask = signMask + 1u;
        }
        if (signValue.y == half(0.0f))
        {
            signMask = signMask + 2u;
        }
        if (signValue.z > half(0.0f))
        {
            signMask = signMask + 4u;
        }
        bindGroup->output[2] = signMask;

        StructuredBuffer<uint> selectedValues = bindGroup->valuesA;
        if (bindGroup->valuesA[2] < bindGroup->valuesB[2])
        {
            selectedValues = bindGroup->valuesB;
        }
        bindGroup->output[3] = selectedValues[2];

        ExperimentalUGLIRWVMRegressionRecord records[3];
        records[0].value = 3u;
        records[1].value = 5u;
        records[2].value = 7u;
        bindGroup->output[4] = experimentalUGLIRWVMRegressionReadFixedArray(records, bindGroup->valuesA[3] - 3u);

        const uint2 masked = uint2(bindGroup->valuesB[0], bindGroup->valuesB[1]) & uint2(3u);
        bindGroup->output[5] = masked.x + masked.y * 16u;

        uint4 vectors[2] = {uint4(10u, 20u, 30u, 40u), uint4(50u, 60u, 70u, 80u)};
        uint vectorIndex = bindGroup->valuesA[0] - 1u;
        vectors[vectorIndex++].xy += uint2(3u, 4u);
        bindGroup->output[6] = vectorIndex;
        bindGroup->output[7] = vectors[0].x;
        bindGroup->output[8] = vectors[0].y;
        bindGroup->output[9] = vectors[1].x;

        ExperimentalUGLIRConstructedValue constructed(bindGroup->valuesA[0]);
        ExperimentalUGLIRConstructedValue delegated(bindGroup->valuesA[1], 5u);
        ExperimentalUGLIRDefaultMembers defaults;
        ExperimentalUGLIRDefaultMembers zeroed = ExperimentalUGLIRDefaultMembers();
        ExperimentalUGLIRConstAggregate overridden{bindGroup->valuesA[2]};
        bindGroup->output[10] = constructed.value;
        bindGroup->output[11] = constructed.next;
        bindGroup->output[12] = delegated.value;
        bindGroup->output[13] = delegated.next;
        bindGroup->output[14] = defaults.value;
        bindGroup->output[15] = zeroed.zero;
        bindGroup->output[16] = zeroed.value;
        bindGroup->output[17] = overridden.value;
        bindGroup->output[18] = uint(zeroed.matrix[0][0]);
        bindGroup->output[19] = uint(zeroed.matrix[1][2]);
        bindGroup->output[20] = uint(zeroed.vectors[0].x);
        bindGroup->output[21] = uint(zeroed.vectors[2].w);
        uint multiplied = bindGroup->valuesA[2] + 4u;
        multiplied *= 0.5f;
        int divided = -int(bindGroup->valuesA[2] + 4u);
        divided /= 2.5f;
        uint rounded = 16777216u + bindGroup->valuesA[0];
        rounded += 0.0f;
        int unsignedDivision = -int(bindGroup->valuesA[2] + 4u);
        unsignedDivision /= 2u;
        bindGroup->output[22] = multiplied;
        bindGroup->output[23] = uint(divided + 10);
        bindGroup->output[24] = rounded;
        bindGroup->output[25] = uint(unsignedDivision);
        ExperimentalUGLIROperatorValue source{bindGroup->valuesA[0]};
        ExperimentalUGLIROperatorValue assigned{};
        assigned = source;
        bindGroup->output[26] = assigned.value;
        bindGroup->output[27] = assigned + 2u;
        bindGroup->output[28] = uint(assigned);
        bindGroup->output[29] = uint(float(half(1.00048828125f)) * 1024.0f);
        bindGroup->output[30] = uint((float(half(-1.00048828125f)) + 2.0f) * 1024.0f);
        bindGroup->output[31] = zeroed.ordered[0] + zeroed.ordered[1] + zeroed.ordered[2];
        const float roundInput = float(bindGroup->valuesA[0]) + 0.5f;
        bindGroup->output[32] = uint(round(roundInput));
        bindGroup->output[33] = uint(round(-roundInput) + 10.0f);
        GroupShared<uint> sharedValue;
        GroupShared<uint> sharedCounts[2];
        sharedValue = 3u;
        sharedCounts[0] = bindGroup->valuesA[0];
        sharedCounts[1] = bindGroup->valuesA[1];
        GroupMemoryBarrierWithGroupSync();
        atomicAdd(sharedValue, 7u);
        atomicAdd(sharedCounts[0], 3u);
        atomicAdd(sharedCounts[1], sharedCounts[0]);
        GroupMemoryBarrierWithGroupSync();
        bindGroup->output[34] = sharedValue;
        bindGroup->output[35] = sharedCounts[1];
        bindGroup->output[36] = 1u;
        atomicAdd(bindGroup->output[36], 1u);
        uint updateIndex = 36u;
        uint previous = bindGroup->output[updateIndex++]++;
        uint compoundResult = (bindGroup->output[36] *= 2u);
        bindGroup->output[37] = previous;
        bindGroup->output[38] = compoundResult;
        bindGroup->output[39] = updateIndex;
        updateIndex = 36u;
        bindGroup->output[40] = (bindGroup->output[updateIndex++] += ++updateIndex);
        bindGroup->output[41] = updateIndex;
        atomicAdd(bindGroup->output[bindGroup->output[39]], 1u);
        bindGroup->output[42] = bindGroup->output[37];
        bindGroup->output[43] = experimentalUGLIRWVMRegressionSameTypeShadow(1u);
        bindGroup->output[44] = experimentalUGLIRWVMRegressionDifferentTypeShadow(1u);
        bindGroup->output[45] = experimentalUGLIRWVMRegressionParameterShadow(1u);
        bindGroup->output[46] = experimentalUGLIRWVMRegressionLoopShadow(1u);
        bindGroup->output[47] = experimentalUGLIRWVMRegressionGTAODistance(4097u);
        bindGroup->output[48] = experimentalUGLIRWVMRegressionStaticScalar;
        bindGroup->output[49] = uint(float(experimentalUGLIRWVMRegressionStaticHalf));
        bindGroup->output[50] = uint(experimentalUGLIRWVMRegressionStaticFloatVectors[1].x +
                                     experimentalUGLIRWVMRegressionStaticFloatVectors[0].y);
        bindGroup->output[51] = uint(float(experimentalUGLIRWVMRegressionStaticHalfVectors[1].x));

        bindGroup->output[52] = experimentalUGLIRWVMRegressionAutoNttpMarker<true>();
        bindGroup->output[53] = experimentalUGLIRWVMRegressionAutoNttpMarker<1>();
        bindGroup->output[54] = experimentalUGLIRWVMRegressionAutoNttpMarker<1u>();
        ExperimentalUGLIRWVMRegressionAutoRecord<true> boolRecord{17u};
        ExperimentalUGLIRWVMRegressionAutoRecord<1> intRecord{23u};
        bindGroup->output[55] = boolRecord.value;
        bindGroup->output[56] = intRecord.value;

        uint aliasValue = 1u;
        bindGroup->output[57] = experimentalUGLIRWVMRegressionReadAlias(aliasValue, aliasValue);
        bindGroup->output[58] = aliasValue;
        uint forwardedAliasValue = 1u;
        bindGroup->output[59] = experimentalUGLIRWVMRegressionForwardAlias(forwardedAliasValue, forwardedAliasValue);
        bindGroup->output[60] = forwardedAliasValue;
        float sincosAliasValue = 1.0f;
        bindGroup->output[61] = asuint(experimentalUGLIRWVMRegressionReadSincosAlias(sincosAliasValue, sincosAliasValue));
        bindGroup->output[62] = asuint(sincosAliasValue);
        uint originalAliasValue = 1u;
        bindGroup->output[63] = experimentalUGLIRWVMRegressionReadValueAlias(originalAliasValue);
        bindGroup->output[64] = originalAliasValue;

        const float roundHalfInput = float(bindGroup->valuesA[0]) - 0.5f;
        const float roundOneHalfInput = float(bindGroup->valuesA[0]) + 0.5f;
        const float roundTwoHalfInput = float(bindGroup->valuesA[1]) + 0.5f;
        const float roundQuarterInput = float(bindGroup->valuesA[0]) + 0.25f;
        bindGroup->output[65] = (round(roundHalfInput) == 0.0f ? 1u : 0u) +
                                (round(-roundHalfInput) == 0.0f ? 2u : 0u);
        bindGroup->output[66] = (round(roundOneHalfInput) == 2.0f ? 1u : 0u) +
                                (round(-roundOneHalfInput) == -2.0f ? 2u : 0u);
        bindGroup->output[67] = (round(roundTwoHalfInput) == 2.0f ? 1u : 0u) +
                                (round(-roundTwoHalfInput) == -2.0f ? 2u : 0u);
        bindGroup->output[68] = (round(roundQuarterInput) == 1.0f ? 1u : 0u) +
                                (round(-roundQuarterInput) == -1.0f ? 2u : 0u);

        const float2 roundHalfVectorInput = float2(roundHalfInput, -roundHalfInput);
        const float2 roundOneHalfVectorInput = float2(roundOneHalfInput, -roundOneHalfInput);
        const float2 roundTwoHalfVectorInput = float2(roundTwoHalfInput, -roundTwoHalfInput);
        const float2 roundQuarterVectorInput = float2(roundQuarterInput, -roundQuarterInput);
        bindGroup->output[69] = all(round(roundHalfVectorInput) == float2(0.0f, 0.0f)) ? 1u : 0u;
        bindGroup->output[70] = all(round(roundOneHalfVectorInput) == float2(2.0f, -2.0f)) ? 1u : 0u;
        bindGroup->output[71] = all(round(roundTwoHalfVectorInput) == float2(2.0f, -2.0f)) ? 1u : 0u;
        bindGroup->output[72] = all(round(roundQuarterVectorInput) == float2(1.0f, -1.0f)) ? 1u : 0u;

        const float2 matrixRow0 = float2(float(bindGroup->valuesA[0]), float(bindGroup->valuesA[1]));
        const float2 matrixRow1 = float2(float(bindGroup->valuesA[2]), float(bindGroup->valuesA[3]));
        const float2x2 dynamicMatrix = float2x2(matrixRow0, matrixRow1);
        bindGroup->output[73] = uint(dynamicMatrix[0][0]);
        bindGroup->output[74] = uint(dynamicMatrix[0][1]);
        bindGroup->output[75] = uint(dynamicMatrix[1][0]);
        bindGroup->output[76] = uint(dynamicMatrix[1][1]);

        const float2 matrixVectorProduct = mul(dynamicMatrix, float2(1.0f, 2.0f));
        bindGroup->output[77] = uint(matrixVectorProduct.x);
        bindGroup->output[78] = uint(matrixVectorProduct.y);
        const float2 vectorMatrixProduct = mul(float2(1.0f, 2.0f), dynamicMatrix);
        bindGroup->output[79] = uint(vectorMatrixProduct.x);
        bindGroup->output[80] = uint(vectorMatrixProduct.y);

        const float scalarDiagonal = float(bindGroup->valuesA[1]);
        const float2x2 scalarMatrix = float2x2(scalarDiagonal);
        bindGroup->output[81] = uint(scalarMatrix[0][0]);
        bindGroup->output[82] = uint(scalarMatrix[0][1]);
        bindGroup->output[83] = uint(scalarMatrix[1][0]);
        bindGroup->output[84] = uint(scalarMatrix[1][1]);
        const float2 scalarMatrixProduct = mul(scalarMatrix, float2(5.0f, 7.0f));
        bindGroup->output[85] = uint(scalarMatrixProduct.x);
        bindGroup->output[86] = uint(scalarMatrixProduct.y);

        const float2 zeroMatrixProduct = mul(float2x2(), float2(5.0f, 7.0f));
        bindGroup->output[87] = uint(zeroMatrixProduct.x);
        bindGroup->output[88] = uint(zeroMatrixProduct.y);

        const float2 nonSquareRow0 = float2(float(bindGroup->valuesA[0]), float(bindGroup->valuesA[1]));
        const float2 nonSquareRow1 = float2(float(bindGroup->valuesA[2]), float(bindGroup->valuesA[3]));
        const float2 nonSquareRow2 = float2(float(bindGroup->valuesB[0] - 5u), float(bindGroup->valuesB[1] - 14u));
        const float3x2 nonSquareMatrix = float3x2(nonSquareRow0, nonSquareRow1, nonSquareRow2);
        bindGroup->output[89] = uint(nonSquareMatrix[0][1]);
        bindGroup->output[90] = uint(nonSquareMatrix[1][0]);
        bindGroup->output[91] = uint(nonSquareMatrix[2][1]);
        const float2 nonSquareMatrixProduct = mul(nonSquareMatrix, float3(1.0f, 2.0f, 3.0f));
        bindGroup->output[92] = uint(nonSquareMatrixProduct.x);
        bindGroup->output[93] = uint(nonSquareMatrixProduct.y);

        const float2x2 scalarArgumentMatrix = float2x2(float(bindGroup->valuesA[0] + 4u),
                                                       float(bindGroup->valuesA[1] + 4u),
                                                       float(bindGroup->valuesA[2] + 4u),
                                                       float(bindGroup->valuesA[3] + 4u));
        const float2x2 matrixMatrixProduct = mul(dynamicMatrix, scalarArgumentMatrix);
        bindGroup->output[94] = uint(matrixMatrixProduct[0][0]);
        bindGroup->output[95] = uint(matrixMatrixProduct[0][1]);
        bindGroup->output[96] = uint(matrixMatrixProduct[1][0]);
        bindGroup->output[97] = uint(matrixMatrixProduct[1][1]);

        const uint dynamicSquareRowIndex = bindGroup->valuesA[0];
        const float2 dynamicSquareRow = dynamicMatrix[dynamicSquareRowIndex];
        bindGroup->output[98] = uint(dynamicSquareRow.x);
        bindGroup->output[99] = uint(dynamicSquareRow.y);
        const uint dynamicNonSquareRowIndex = bindGroup->valuesA[1];
        const float2 dynamicNonSquareRow = nonSquareMatrix[dynamicNonSquareRowIndex];
        bindGroup->output[100] = uint(dynamicNonSquareRow.x);
        bindGroup->output[101] = uint(dynamicNonSquareRow.y);
        uint sideEffectSquareRowIndex = bindGroup->valuesA[0];
        const float2 sideEffectSquareRow = dynamicMatrix[sideEffectSquareRowIndex++];
        bindGroup->output[102] = uint(sideEffectSquareRow.x);
        bindGroup->output[103] = uint(sideEffectSquareRow.y);
        bindGroup->output[104] = sideEffectSquareRowIndex;

        // Source half operators have intermediate rounding; Vulkan permits contraction across it.
        const half roundingInput = half(1.0f + float(bindGroup->valuesA[0]) / 1024.0f);
        const half roundedCancellation = roundingInput * roundingInput - half(1.001953125f);
        const half roundedProduct = roundingInput * roundingInput;
        const half separatedCancellation = roundedProduct - half(1.001953125f);
        const half2 vectorCancellation = half2(roundingInput) * half2(roundingInput) - half2(1.001953125f);
        bindGroup->output[105] = uint(float(roundedCancellation) * 1048576.0f);
        bindGroup->output[106] = uint(float(separatedCancellation) * 1048576.0f);
        bindGroup->output[107] = uint(float(vectorCancellation.x) * 1048576.0f);
        bindGroup->output[108] = uint(float(vectorCancellation.y) * 1048576.0f);
        half compoundCancellation = roundingInput;
        compoundCancellation *= roundingInput;
        compoundCancellation -= half(1.001953125f);
        half2 swizzleCancellation = half2(roundingInput);
        swizzleCancellation.xy *= half2(roundingInput);
        swizzleCancellation.xy -= half2(1.001953125f);
        const half additionBoundary = (half(2048.0f) + half(float(bindGroup->valuesA[0]))) - half(2048.0f);
        bindGroup->output[109] = uint(float(compoundCancellation) * 1048576.0f);
        bindGroup->output[110] = uint(float(swizzleCancellation.x) * 1048576.0f);
        bindGroup->output[111] = uint(float(swizzleCancellation.y) * 1048576.0f);
        bindGroup->output[112] = uint(float(additionBoundary));
        half2 indexedCancellation = half2(roundingInput);
        uint componentIndex = 0u;
        indexedCancellation[componentIndex++] *= roundingInput;
        indexedCancellation.x -= half(1.001953125f);
        bindGroup->output[113] = uint(float(indexedCancellation.x) * 1048576.0f);
        bindGroup->output[114] = componentIndex;
        half arrayCancellation[2] = {roundingInput, roundingInput};
        uint arrayIndex = 0u;
        arrayCancellation[arrayIndex++] *= roundingInput;
        arrayCancellation[0] -= half(1.001953125f);
        bindGroup->output[115] = uint(float(arrayCancellation[0]) * 1048576.0f);
        bindGroup->output[116] = arrayIndex;
        bindGroup->output[117] = uint(float(half(float(bindGroup->valuesA[0]) * 5.0f) / half(3.0f)) * 1024.0f);
        bindGroup->output[118] = uint(float(half(float(bindGroup->valuesA[0]) / 16384.0f) * half(0.5f)) * 32768.0f);
        bindGroup->output[119] = uint(float(half(1.0f + float(bindGroup->valuesA[0]) / 2048.0f)) * 2048.0f);
        bindGroup->output[120] = uint(float(uint(float(bindGroup->valuesA[0]) * 1.75f)) * 4.0f);
        bindGroup->output[121] = uint(float(int(-float(bindGroup->valuesA[0]) * 1.75f)) * -4.0f);


    }
};

/** Tests Boolean aggregate writes independently so a driver failure cannot mask other semantic regressions. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRBooleanAggregatePass final : public IComputeClass
{
public:
    /** Creates the Boolean field test with the shared regression bindings. */
    constructor(BindGroup<ExperimentalUGLIRWVMRegressionComputeBindGroup> bindGroup [[Slot0]]) {}
private:
    /** Writes dynamic Boolean fields and reads them back as exact integers. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        ExperimentalUGLIRBooleanAggregate value{};
        value.flags = bool2(bindGroup->valuesA[0] > 0u, bindGroup->valuesA[1] > 0u);
        bindGroup->output[0] = value.flags.x ? 1u : 0u;
        bindGroup->output[1] = value.flags.y ? 1u : 0u;
        bindGroup->output[2] = value.padding;
        bindGroup->output[3] = uint(value.matrix[0][0]);
    }
};

/** Exercises explicit LOD sampling, gradient sampling, and derivative intrinsics in a minimal fragment pass. */
class ExperimentalUGLIRWVMRegressionTexturePass final : public IRenderClass
{
public:
    /** Creates the render pass with the texture regression bind group. */
    constructor(BindGroup<ExperimentalUGLIRWVMRegressionTextureBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Emits a full-screen triangle with normalized texture coordinates. */
    ExperimentalUGLIRWVMRegressionVertexOutput vertex(uint vertexID [[VertexID]])
    {
        ExperimentalUGLIRWVMRegressionVertexOutput output;
        if (vertexID == 0u)
        {
            output.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
            output.uv = float2(0.0f, 0.0f);
        }
        else if (vertexID == 1u)
        {
            output.position = float4(3.0f, -1.0f, 0.0f, 1.0f);
            output.uv = float2(2.0f, 0.0f);
        }
        else
        {
            output.position = float4(-1.0f, 3.0f, 0.0f, 1.0f);
            output.uv = float2(0.0f, 2.0f);
        }
        return output;
    }

    /** Samples the source texture through explicit LOD and gradient paths and uses derivatives in the result. */
    ExperimentalUGLIRWVMRegressionFrameBuffer fragment(ExperimentalUGLIRWVMRegressionVertexOutput vertexIn)
    {
        const float2 dxValue = ddx(vertexIn.uv);
        const float2 dyValue = ddy(vertexIn.uv);
        const float4 lodValue = bindGroup->sourceTexture->sampleLevel(bindGroup->sourceSampler, vertexIn.uv, 0.0f);
        const float4 gradValue = bindGroup->sourceTexture->sampleGrad(bindGroup->sourceSampler, vertexIn.uv, dxValue, dyValue);
        const float derivativeValue = (dxValue.x + dyValue.y) * 0.0625f;

        ExperimentalUGLIRWVMRegressionFrameBuffer framebuffer;
        half4 color = half4((lodValue + gradValue) * 0.5f + float4(derivativeValue, derivativeValue, derivativeValue, 0.0f));
        int2 dimensions = int2(0, 0);
        bindGroup->sourceTexture->getDimensions(dimensions.x, dimensions.y);
        color.w = (dimensions.x == 2 && dimensions.y == 2) ? half(1.0f) : half(0.0f);
        framebuffer.color = color;
        return framebuffer;
    }
};

#endif
