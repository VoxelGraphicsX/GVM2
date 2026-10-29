#pragma once
#include "UGL.h"
using namespace UGL;
/** Declares incompatible Args and legacy Slot meanings for one constructor parameter. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidSlotMix final : public IComputeClass
{
public:
    /** Intentionally combines argument and descriptor-set annotations. */
    constructor(uint value [[Args0]] [[Slot0]]) {}
private:
    /** Supplies a valid entry for the annotation-conflict diagnostic. */
    void compute(uint3 id [[DispatchThreadID]]) {}
};
