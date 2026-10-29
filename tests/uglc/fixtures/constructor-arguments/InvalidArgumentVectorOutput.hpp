#pragma once
#include "UGL.h"
using namespace UGL;

/** Uses the existing value-spelled output mode. */
inline void writeVectorOutput(float value OUT) { value = 1; }

/** Exercises a forbidden write or output destination for readonly constructor inputs. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentVectorOutput final : public IComputeClass
{
public:
    /** Declares the readonly input involved in this diagnostic. */
    constructor(float4 values [[Args0]]) {}
private:
    /** Intentionally performs the invalid operation so its source location is diagnosed. */
    void compute(uint3 id [[DispatchThreadID]]) { writeVectorOutput(values[0]); }
};
