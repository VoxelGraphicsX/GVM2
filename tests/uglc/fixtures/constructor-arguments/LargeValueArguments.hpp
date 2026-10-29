#pragma once
#include "UGL.h"
using namespace UGL;

/** Exercises the heap-allocated capture path for Args value layouts larger than the inline limit. */
class [[LocalWorkGroupSize(1, 1, 1)]] LargeValueArguments final : public IComputeClass
{
public:
    /** Receives a value payload whose encoded layout exceeds four kilobytes. */
    constructor((std::array<float4, 260> values [[Args0]]), RWStructuredBuffer<uint> output [[Args1]])
    {
    }
private:
    /** Reads boundary elements so every captured scalar remains live in the layout. */
    void compute()
    {
        output[0] = uint(values[0].x + values[259].w);
    }
};
