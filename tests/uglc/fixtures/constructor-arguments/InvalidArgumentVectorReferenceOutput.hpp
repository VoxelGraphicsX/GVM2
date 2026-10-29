#pragma once
#include "UGL.h"
using namespace UGL;

/** Requires writable scalar storage using an explicit output reference. */
inline void writeVectorReferenceOutput(float &value OUT) { value = 1; }

/** Exercises a forbidden write or output destination for readonly constructor inputs. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentVectorReferenceOutput final : public IComputeClass
{
public:
    /** Declares the readonly input involved in this diagnostic. */
    constructor(float4 values [[Args0]]) {}
private:
    /** Intentionally performs the invalid operation so its source location is diagnosed. */
    void compute(uint3 id [[DispatchThreadID]]) { writeVectorReferenceOutput(values[0]); }
};
