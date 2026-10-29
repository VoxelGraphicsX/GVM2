#pragma once
#include "UGL.h"
using namespace UGL;
/** Mixes an Args parameter with an unannotated creation-only parameter. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidMissingAnnotation final : public IComputeClass
{
public:
    /** Intentionally omits the required second argument annotation. */
    constructor(uint first [[Args0]], uint second) {}
private:
    /** Supplies a valid entry for the missing-annotation diagnostic. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};
