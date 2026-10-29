#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Demonstrates that ordinary references remain forbidden even for writable local copies. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidLocalReference final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Uses a mutable copy so Clang const binding does not mask the DSL reference prohibition. */
    void compute(uint3 id [[DispatchThreadID]]) { uint local = value; uint &alias = local; alias = id.x; }
};
