#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Writes an explicitly declared output parameter. */
inline void writeArgumentOutput(uint &value OUT) { value = 7; }

/** Demonstrates that output modes cannot make an Args value writable. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidOutputArgument final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally uses the immutable argument as an output destination. */
    void compute(uint3 id [[DispatchThreadID]]) { writeArgumentOutput(value); }
};
