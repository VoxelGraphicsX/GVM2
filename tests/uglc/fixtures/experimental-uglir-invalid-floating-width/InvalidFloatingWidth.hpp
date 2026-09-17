#pragma once
#include "UGL.h"
using namespace UGL;

/** Binds dynamic values for a floating-point width contract regression. */
struct FloatingWidthBindings final : public IBindGroup
{
    /** Creates input and output bindings for unsupported-width diagnostics. */
    constructor(StructuredBuffer<uint> input [[Binding0]], RWStructuredBuffer<uint> output [[Binding1]]) {}
};

/** Exposes precision loss when dynamic binary64 arithmetic is silently lowered to binary32. */
class [[LocalWorkGroupSize(1, 1, 1)]] FloatingWidthPass final : public IComputeClass
{
public:
    /** Binds the floating-point width regression resources. */
    constructor(BindGroup<FloatingWidthBindings> bindings [[Slot0]]) {}
private:
    /** Produces one for input one under binary64 arithmetic, requiring a diagnostic until that width is supported. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        double wide = double(bindings->input[threadID.x]);
        wide += 16777216.0;
        wide -= 16777216.0;
        bindings->output[threadID.x] = uint(wide);
    }
};
