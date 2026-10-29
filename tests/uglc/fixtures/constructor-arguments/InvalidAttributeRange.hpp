#pragma once
#include "UGL.h"
using namespace UGL;
/** Declares an argument position outside the constructor macro's supported range. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidAttributeRange final : public IComputeClass
{
public:
    /** Intentionally uses an unregistered argument attribute. */
    constructor(uint value [[Args64]]) {}
private:
    /** Supplies a valid entry for attribute-range validation. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};
