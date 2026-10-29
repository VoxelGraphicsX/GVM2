#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Intentionally declares a pointer parameter without providing a function body. */
void inspectArgumentPointer(const uint *value);

/** Demonstrates that a declaration-only helper cannot hide a pointer signature. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidHelperPointer final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally calls a helper with a forbidden pointer parameter. */
    void compute(uint3 id [[DispatchThreadID]]) { inspectArgumentPointer(nullptr); }
};
