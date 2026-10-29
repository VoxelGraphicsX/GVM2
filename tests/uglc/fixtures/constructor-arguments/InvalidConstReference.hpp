#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Demonstrates that const qualification does not permit ordinary DSL references. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidConstReference final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally deduces a const reference to an input argument. */
    void compute(uint3 id [[DispatchThreadID]]) { const auto &alias = value; const uint copy = alias; }
};
