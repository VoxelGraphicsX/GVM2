#pragma once
#include "UGL.h"
using namespace UGL;

/** Binds runtime input so a 64-bit shift cannot be hidden by constant folding. */
struct IntegerWidthBindGroup final : IBindGroup
{
    /** Creates input and output bindings for unsupported-width diagnostics. */
    constructor(StructuredBuffer<uint> input [[Binding0]], RWStructuredBuffer<uint> output [[Binding1]]) {}
};

/** Exercises an integer width that must be diagnosed instead of silently reduced to 32 bits. */
class [[LocalWorkGroupSize(1, 1, 1)]] IntegerWidthPass final : public IComputeClass
{
public:
    /** Creates the diagnostic fixture's compute pass. */
    constructor(BindGroup<IntegerWidthBindGroup> bindGroup [[Slot0]]) {}
private:
    /** Uses a runtime 64-bit shift whose value changes if the backend silently narrows it. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        unsigned long long wide = bindGroup->input[threadID.x];
        wide = (wide << 32u) | 7ull;
        bindGroup->output[threadID.x] = uint(wide >> 32u);
    }
};
