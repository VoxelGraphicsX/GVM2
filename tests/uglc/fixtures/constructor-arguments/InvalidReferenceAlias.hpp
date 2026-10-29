#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Demonstrates that an unused local type alias cannot declare a reference type. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidReferenceAlias final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally names a forbidden reference type without creating a variable. */
    void compute(uint3 id [[DispatchThreadID]]) { using ArgumentReference = uint &; const uint copy = value; }
};
