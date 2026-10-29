#pragma once
#include "UGL.h"
using namespace UGL;
/** Declares a reference parameter that cannot own the delayed call value in the initial API. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidReference final : public IComputeClass
{
public:
    /** Intentionally uses a reference instead of a value constructor argument. */
    constructor(const uint &value [[Args0]]) {}
private:
    /** Supplies a valid entry for the reference-contract diagnostic. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};
