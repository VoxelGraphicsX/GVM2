#pragma once

#include "UGL.h"

using namespace UGL;

/** Binds dynamic branch and fmod inputs plus exact integer observations for evaluation-order tests. */
struct EvaluationSemanticsBindGroup final : public IBindGroup
{
    /** Creates the resources shared by the legacy and experimental evaluation tests. */
    constructor(StructuredBuffer<uint> conditions [[Binding0]], RWStructuredBuffer<uint> output [[Binding1]],
                StructuredBuffer<float4> fmodInputs [[Binding2]]) {}
};

/** Stores an aggregate result used to test conditional operand evaluation. */
struct EvaluationPair
{
    uint value;
    uint tag;
};

/** Exercises per-element construction of local records with nontrivial vector members. */
struct EvaluationArrayElement
{
    float4 vector;
    uint marker = 7u;
};

/** Produces an aggregate and records the selected branch's single evaluation. */
inline EvaluationPair evaluationAggregate(uint counter INOUT, uint tag)
{
    counter = counter + 1u;
    EvaluationPair result;
    result.value = counter;
    result.tag = tag;
    return result;
}

/** Returns its argument so callers can observe postfix values passed to functions. */
inline uint evaluationIdentity(uint value) { return value; }

/** Mutates an array element passed through the DSL's explicit reference convention. */
inline void evaluationWrite(uint value INOUT) { value = 50u; }

/** Produces a vector while recording exactly one call in the supplied counter. */
inline float4 evaluationVector(uint counter INOUT)
{
    counter = counter + 1u;
    return float4(float(counter), 2.0f, 3.0f, 4.0f);
}

/** Produces two components while recording exactly one call in the supplied counter. */
inline float2 evaluationVectorPair(uint counter INOUT)
{
    counter = counter + 1u;
    return float2(float(counter), 2.0f);
}

/** Distinguishes unsigned overload selection in shader calls. */
inline uint evaluationOverload(uint value) { return value + 100u; }

/** Distinguishes signed overload selection in shader calls. */
inline uint evaluationOverload(int value) { return uint(value + 20); }

/** Requires the caller's implicit unsigned-to-signed conversion. */
inline uint evaluationSignedArgument(int value) { return evaluationOverload(value); }

/** Requires scalar-to-boolean conversion at a function argument boundary. */
inline uint evaluationBooleanArgument(bool value) { return value ? 1u : 0u; }

/** Exercises a concrete function-template call with an integral argument. */
template <uint Variant>
inline uint evaluationTemplateArgument(uint value)
{
    return value + Variant;
}

/** Observes scalar, aggregate, array, vector, and matrix evaluation on both dynamic branch outcomes. */
class [[LocalWorkGroupSize(1, 1, 1)]] EvaluationSemanticsPass final : public IComputeClass
{
public:
    /** Creates a pass that writes 68 integer observations per invocation. */
    constructor(BindGroup<EvaluationSemanticsBindGroup> bindGroup [[Slot0]]) {}

private:
    /** Writes observations with independent, exact CPU expectations in the RHI test. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint lane = threadID.x;
        if (lane >= 2u) { return; }
        const uint base = lane * 71u;
        const bool condition = bindGroup->conditions[lane] != 0u;
        uint counter = 0u;
        bool selected = false && (++counter > 0u);
        bindGroup->output[base + 0u] = counter;
        selected = true || (++counter > 0u);
        bindGroup->output[base + 1u] = counter;
        counter = 0u;
        selected = condition && (++counter > 0u);
        bindGroup->output[base + 2u] = counter;
        counter = 0u;
        selected = condition || (++counter > 0u);
        bindGroup->output[base + 3u] = counter;
        counter = 0u;
        selected = (condition && (++counter > 0u)) || (!condition && (++counter > 0u));
        bindGroup->output[base + 4u] = counter;
        counter = 0u;
        uint value = condition ? ++counter : ++counter;
        bindGroup->output[base + 5u] = counter * 100u + value;
        counter = 0u;
        value = condition ? (false ? ++counter : ++counter) : (true ? ++counter : ++counter);
        bindGroup->output[base + 6u] = counter * 100u + value;
        counter = 0u;
        uint previous = counter++;
        bindGroup->output[base + 7u] = previous * 100u + counter;
        counter = 0u;
        previous = ++counter;
        bindGroup->output[base + 8u] = previous * 100u + counter;
        counter = 2u;
        previous = counter--;
        bindGroup->output[base + 9u] = previous * 100u + counter;
        counter = 2u;
        previous = --counter;
        bindGroup->output[base + 10u] = previous * 100u + counter;

        uint values[2] = {10u, 20u};
        uint index = bindGroup->conditions[lane] & 1u;
        values[index] = 30u;
        bindGroup->output[base + 11u] = values[0];
        values[0] = 40u;
        bindGroup->output[base + 12u] = values[0];
        evaluationWrite(values[0]);
        bindGroup->output[base + 13u] = values[0];
        counter = 7u;
        const uint snapshot[2] = {counter, 20u};
        counter = 9u;
        bindGroup->output[base + 14u] = snapshot[0];
        const uint constants[2] = {10u, 20u};
        bindGroup->output[base + 15u] = constants[0];
        counter = 5u;
        value = evaluationIdentity(counter++);
        bindGroup->output[base + 16u] = value * 100u + counter;
        index = 0u;
        value = values[index++];
        bindGroup->output[base + 17u] = value * 100u + index;

        counter = 0u;
        half4 converted = half4(evaluationVector(counter));
        bindGroup->output[base + 18u] = counter;
        bindGroup->output[base + 19u] = uint(converted.x * half(100.0f) + converted.y * half(10.0f) + converted.z);
        counter = 0u;
        half4 mixed = half4(half2(evaluationVectorPair(counter)), half(7.0f), half(8.0f));
        bindGroup->output[base + 20u] = counter;
        bindGroup->output[base + 21u] = uint(mixed.x * half(100.0f) + mixed.y * half(10.0f) + mixed.z);
        counter = 0u;
        float4 vectorSelection = condition ? evaluationVector(counter) : evaluationVector(counter);
        bindGroup->output[base + 22u] = counter;
        bindGroup->output[base + 23u] = uint(vectorSelection.x);
        counter = 0u;
        EvaluationPair pair = condition ? evaluationAggregate(counter, 11u) : evaluationAggregate(counter, 22u);
        bindGroup->output[base + 24u] = counter;
        bindGroup->output[base + 25u] = pair.value * 100u + pair.tag;
        counter = 0u;
        selected = condition && (condition ? ++counter : ++counter) > 0u;
        bindGroup->output[base + 26u] = counter;
        counter = 0u;
        value = (condition || (++counter > 0u)) ? ++counter : ++counter;
        bindGroup->output[base + 27u] = counter;
        counter = 0u;
        for (uint i = 0u; i < 4u && counter < 3u; i++) { counter++; }
        bindGroup->output[base + 28u] = counter;
        counter = 0u;
        while (counter < 2u && (++counter > 0u)) {}
        bindGroup->output[base + 29u] = counter;
        counter = 0u;
        do { counter++; } while (counter < 2u && condition);
        bindGroup->output[base + 30u] = counter;
        bindGroup->output[base + 31u] = selected ? 1u : 0u;
        index = 2u;
        selected = (condition && !condition) && values[index++] > 0u;
        bindGroup->output[base + 32u] = index;
        index = 2u;
        selected = (condition || !condition) || values[index++] > 0u;
        bindGroup->output[base + 33u] = index;
        counter = 0u;
        value = true ? counter++ : counter++;
        bindGroup->output[base + 34u] = counter * 100u + value;
        counter = 2u;
        value = false ? counter-- : counter--;
        bindGroup->output[base + 35u] = counter * 100u + value;

        float scalarConstant = condition ? 0.25 : 0.5;
        if (!(scalarConstant < 10000.0 && -2.5 <= scalarConstant)) { scalarConstant = 99.0f; }
        float4 constantVector = float4(1.25, -2.5, 1.5 + 2.25, condition ? 4.0 : 8.0);
        half2 halfConstants = half2(0.5, condition ? 1.0 : 2.0);
        bindGroup->output[base + 36u] = uint(constantVector.x * 4.0f);
        bindGroup->output[base + 37u] = uint(-constantVector.y * 2.0f);
        bindGroup->output[base + 38u] = uint(constantVector.z * 4.0f);
        bindGroup->output[base + 39u] = uint(constantVector.w + scalarConstant * 4.0f + float(halfConstants.x) * 2.0f + float(halfConstants.y));

        value = (counter = 7u);
        bindGroup->output[base + 40u] = value;
        bindGroup->output[base + 41u] = counter;
        value = (counter += 3u);
        bindGroup->output[base + 42u] = value;
        bindGroup->output[base + 43u] = counter;
        index = 0u;
        value = (values[index++] = 6u);
        bindGroup->output[base + 44u] = value;
        bindGroup->output[base + 45u] = index;
        uint4 assignedVector = uint4(10u, 20u, 30u, 40u);
        uint2 swizzleResult = (assignedVector.xy += uint2(3u, 4u));
        bindGroup->output[base + 46u] = swizzleResult.x * 100u + swizzleResult.y;
        bindGroup->output[base + 47u] = assignedVector.x * 100u + assignedVector.y;
        int signedValue = 0;
        int signedAssigned = (signedValue = -3.75f);
        bindGroup->output[base + 48u] = uint(signedAssigned + 4);
        float2x2 assignedMatrix = float2x2(1.0f, 2.0f, 3.0f, 4.0f);
        float matrixAssigned = (assignedMatrix[0][1] = 7.0f);
        bindGroup->output[base + 49u] = uint(matrixAssigned);
        bindGroup->output[base + 50u] = uint(assignedMatrix[0][1]);
        uint second = 0u;
        value = (counter = second = 9u);
        bindGroup->output[base + 51u] = counter * 100u + second * 10u + value;
        value = (bindGroup->output[base + 52u] = 5u);
        atomicAdd(bindGroup->output[base + 52u], 1u);
        bindGroup->output[base + 53u] = value;
        const uint semanticInput = bindGroup->conditions[lane];
        bindGroup->output[base + 54u] = evaluationOverload(semanticInput + 7u);
        bindGroup->output[base + 55u] = evaluationSignedArgument(semanticInput);
        bindGroup->output[base + 56u] = evaluationTemplateArgument<7u>(semanticInput);
        bindGroup->output[base + 57u] = evaluationBooleanArgument(semanticInput);

        const float4 scalarAndVectorNumerators = bindGroup->fmodInputs[lane * 2u];
        const float4 vectorDivisors = bindGroup->fmodInputs[lane * 2u + 1u];
        const float scalarRemainder = fmod(scalarAndVectorNumerators.x, scalarAndVectorNumerators.y);
        const float2 vectorRemainders = fmod(float2(scalarAndVectorNumerators.zw), float2(vectorDivisors.xy));
        bindGroup->output[base + 58u] = asuint(scalarRemainder);
        bindGroup->output[base + 59u] = asuint(vectorRemainders.x);
        bindGroup->output[base + 60u] = asuint(vectorRemainders.y);

        uint4 indexedVector = uint4(10u, 20u, 30u, 40u);
        uint indexedVectorIndex = lane;
        indexedVector[indexedVectorIndex] = 50u + indexedVectorIndex;
        bindGroup->output[base + 61u] = indexedVector[indexedVectorIndex];
        indexedVector[indexedVectorIndex] += 2u;
        bindGroup->output[base + 62u] = indexedVector[indexedVectorIndex];
        const uint indexedVectorPostfix = indexedVector[indexedVectorIndex]++;
        bindGroup->output[base + 63u] = indexedVectorPostfix * 100u + indexedVector[indexedVectorIndex];
        const uint indexedVectorPrefix = ++indexedVector[indexedVectorIndex];
        bindGroup->output[base + 64u] = indexedVectorPrefix * 100u + indexedVector[indexedVectorIndex];
        uint4 sideEffectVector = uint4(10u, 20u, 30u, 40u);
        uint sideEffectIndex = 0u;
        const uint sideEffectResult = (sideEffectVector[sideEffectIndex++] += 1u);
        bindGroup->output[base + 65u] = sideEffectResult * 100u + sideEffectIndex;

        float2x2 compoundMatrix = float2x2(1.0f, 2.0f, 3.0f, 4.0f);
        compoundMatrix[0][1] = 5.0f;
        const float matrixCompoundResult = (compoundMatrix[0][1] += 2.0f);
        bindGroup->output[base + 66u] = uint(matrixCompoundResult);
        bindGroup->output[base + 67u] = uint(compoundMatrix[0][1]);

        EvaluationArrayElement localArray[2];
        EvaluationArrayElement localGrid[2][2];
        localArray[0].vector = float4(float(semanticInput + 3u));
        localArray[1].vector = float4(float(semanticInput + 5u));
        localGrid[1][1].vector = float4(float(semanticInput + 9u));
        bindGroup->output[base + 68u] = localArray[0].marker + localArray[1].marker;
        bindGroup->output[base + 69u] = uint(localArray[0].vector.x + localArray[1].vector.x);
        bindGroup->output[base + 70u] = localGrid[0][1].marker + localGrid[1][0].marker + uint(localGrid[1][1].vector.x);
    }
};
