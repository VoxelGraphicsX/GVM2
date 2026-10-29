#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Intentionally returns a reference to immutable constant storage. */
inline const uint &readArgumentReference() { static const uint value = 7; return value; }

/** Demonstrates that even read-only reference-returning helpers are forbidden. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidReferenceReturn final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally obtains a value through a forbidden reference return type. */
    void compute(uint3 id [[DispatchThreadID]]) { const uint copy = readArgumentReference() + value; }
};
