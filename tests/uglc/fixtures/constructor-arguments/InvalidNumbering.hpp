#pragma once
#include "UGL.h"
using namespace UGL;
/** Declares a gap in the constructor argument numbering. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidNumbering final : public IComputeClass
{
public:
    /** Intentionally skips Args1 in the constructor signature. */
    constructor(uint first [[Args0]], uint second [[Args2]]) {}
private:
    /** Supplies a valid entry so only the argument declaration fails. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};
