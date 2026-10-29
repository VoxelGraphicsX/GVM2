#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Intentionally accepts an ordinary mutable reference instead of an explicit output parameter. */
inline void mutateArgumentReference(uint &value) { value = 7; }

/** Demonstrates that shader helpers cannot hide forbidden reference parameters. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidHelperReference final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Passes a mutable copy so the ordinary reference signature still reaches DSL verification. */
    void compute(uint3 id [[DispatchThreadID]]) { uint local = value; mutateArgumentReference(local); }
};
