#pragma once
#include "UGL.h"
using namespace UGL;

/** Retains the existing rejection of incompatible float3 buffer element layouts on the Args path. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentBufferLayout final : public IComputeClass
{
public:
    /** Declares a buffer whose 12-byte host stride is incompatible with the native Metal float3 stride. */
    constructor(RWStructuredBuffer<float3> output [[Args0]]) {}
private:
    /** Makes the buffer element type reachable by native shader validation. */
    void compute(uint3 id [[DispatchThreadID]]) { output[id.x] = float3(1); }
};
