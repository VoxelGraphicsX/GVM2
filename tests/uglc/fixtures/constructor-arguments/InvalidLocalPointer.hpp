#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Demonstrates that raw pointers remain forbidden even for writable invocation-local storage. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidLocalPointer final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Takes a local copy's address so const assignment does not mask the DSL pointer diagnostic. */
    void compute(uint3 id [[DispatchThreadID]]) { uint local = value; auto *address = &local; *address = id.x; }
};
