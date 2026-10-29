#pragma once
#include "UGL.h"
using namespace UGL;
/** Declares multiple argument positions on one constructor parameter. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidDuplicateAnnotation final : public IComputeClass
{
public:
    /** Intentionally attaches two Args annotations to one value. */
    constructor(uint value [[Args0]] [[Args1]]) {}
private:
    /** Supplies a valid entry for the duplicate-annotation diagnostic. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};
