#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Demonstrates that an explicit cast cannot introduce a discarded raw pointer. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidPointerCast final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally casts an integer to a pointer without a local pointer declaration. */
    void compute(uint3 id [[DispatchThreadID]]) { (void)(uint *)0; }
};
