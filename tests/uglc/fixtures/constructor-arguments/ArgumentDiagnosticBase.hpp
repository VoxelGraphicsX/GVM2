#pragma once
#include "UGL.h"
using namespace UGL;

/** Supplies an ordinary argument type distinct from resource and operation control types. */
struct DiagnosticSettings { uint value; };

/** Defines the constructor signature shared by execution-call diagnostic fixtures. */
class [[LocalWorkGroupSize(1, 1, 1)]] DiagnosticCompute final : public IComputeClass
{
public:
    /** Requires a storage resource followed by an ordinary settings value. */
    constructor(StructuredBuffer<float> input [[Args0]], DiagnosticSettings settings [[Args1]]) {}
private:
    /** Reads both constructor members to preserve the actual shader argument interface. */
    void compute(uint3 id [[DispatchThreadID]]) { const float value = input[id.x] + float(settings.value); }
};

using DiagnosticStorage = Buffer<uint, BufferUsage<Storage>>;
using DiagnosticUniform = Buffer<uint, BufferUsage<Uniform>>;
