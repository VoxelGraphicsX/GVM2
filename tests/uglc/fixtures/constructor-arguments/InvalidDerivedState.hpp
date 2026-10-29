#pragma once
#include "UGL.h"
using namespace UGL;
/** Reads constructor-derived state that cannot change with later dispatch arguments. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidDerivedState final : public IComputeClass
{
    uint derived;
public:
    /** Calculates instance state once at creation. */
    constructor(uint value [[Args0]]) { derived = value + 1; }
private:
    /** Intentionally reads creation-only state from the shader. */
    void compute(uint3 id [[DispatchThreadID]]) { const uint result = derived; }
};
