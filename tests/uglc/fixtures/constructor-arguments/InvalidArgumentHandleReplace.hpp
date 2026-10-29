#pragma once
#include "UGL.h"
using namespace UGL;


/** Exercises a forbidden write or output destination for readonly constructor inputs. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentHandleReplace final : public IComputeClass
{
public:
    /** Declares the readonly input involved in this diagnostic. */
    constructor(RWStructuredBuffer<uint> output [[Args0]]) {}
private:
    /** Intentionally performs the invalid operation so its source location is diagnosed. */
    void compute(uint3 id [[DispatchThreadID]]) { output = RWStructuredBuffer<uint>(); }
};
