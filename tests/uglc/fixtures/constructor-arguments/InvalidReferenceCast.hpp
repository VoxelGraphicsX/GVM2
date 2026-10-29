#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Demonstrates that casts cannot introduce ordinary references without declarations. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidReferenceCast final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally introduces a discarded reference using an explicit cast. */
    void compute(uint3 id [[DispatchThreadID]]) { (void)static_cast<const uint &>(value); }
};
