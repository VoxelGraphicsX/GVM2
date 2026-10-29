#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Updates an explicitly declared value-spelled input-output parameter. */
inline void updateArgumentInputOutput(uint value INOUT) { value += 1; }

/** Demonstrates that value-spelled output modes cannot make an Args value writable. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidInputOutputArgument final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally uses the immutable argument as an input-output destination. */
    void compute(uint3 id [[DispatchThreadID]]) { updateArgumentInputOutput(value); }
};
