#pragma once
#include "ArgumentDiagnosticBase.hpp"
/** Attempts to modify the shared ordinary input argument from a shader invocation. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentWrite final : public IComputeClass
{
public:
    /** Requires an immutable settings value. */
    constructor(DiagnosticSettings settings [[Args0]]) {}
private:
    /** Intentionally writes the ordinary root argument. */
    void compute(uint3 id [[DispatchThreadID]]) { settings.value = id.x; }
};
