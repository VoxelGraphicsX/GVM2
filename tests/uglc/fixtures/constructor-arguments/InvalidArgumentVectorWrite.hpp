#pragma once
#include "UGL.h"
using namespace UGL;

/** Attempts a vector subscript write through a readonly Args field. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentVectorWrite final : public IComputeClass
{
public:
    /** Receives one readonly vector input. */
    constructor(float4 values [[Args0]]) {}
private:
    /** Requires Clang to reject the vector's const element access. */
    void compute(uint3 id [[DispatchThreadID]]) { values[0] = float(id.x); }
};
