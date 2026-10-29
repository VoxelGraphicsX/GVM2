#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Demonstrates that a temporary value cannot introduce an ordinary rvalue reference. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidRvalueReference final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally deduces an rvalue reference from an invocation-local temporary. */
    void compute(uint3 id [[DispatchThreadID]]) { auto &&alias = uint(value); alias = id.x; }
};
