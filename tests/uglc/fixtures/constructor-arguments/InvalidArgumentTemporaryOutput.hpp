#pragma once
#include "UGL.h"
using namespace UGL;

/** Requires a real output destination despite the value-spelled declaration. */
inline void writeTemporaryOutput(uint value OUT) { value = 1; }

/** Exercises a forbidden write or output destination for readonly constructor inputs. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentTemporaryOutput final : public IComputeClass
{
public:
    /** Declares the readonly input involved in this diagnostic. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally performs the invalid operation so its source location is diagnosed. */
    void compute(uint3 id [[DispatchThreadID]]) { writeTemporaryOutput(uint(value)); }
};
