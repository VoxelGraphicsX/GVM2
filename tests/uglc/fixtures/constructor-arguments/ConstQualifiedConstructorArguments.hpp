#pragma once
#include "UGL.h"
using namespace UGL;

/** Keeps explicitly const by-value parameters compatible with the same public execution signature. */
class [[LocalWorkGroupSize(1, 1, 1)]] ConstQualifiedConstructorArguments final : public IComputeClass
{
public:
    /** Uses optional explicit C++ qualifiers without requiring an additional IN marker. */
    constructor(const uint value [[Args0]], const float4 values [[Args1]]) {}
private:
    /** Reads both input values through the readonly shader view. */
    void compute(uint3 id [[DispatchThreadID]]) { const float result = values[0] + float(value + id.x); }
};
