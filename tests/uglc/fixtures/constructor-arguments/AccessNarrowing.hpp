#pragma once
#include "UGL.h"
using namespace UGL;

/** Reads one writable buffer and writes another so their declared accesses diverge. */
class [[LocalWorkGroupSize(1, 1, 1)]] AccessNarrowingBuffers final : public IComputeClass
{
public:
    /** Receives a read-only input and a written output, both declared writable. */
    constructor(RWStructuredBuffer<uint> input [[Args0]], RWStructuredBuffer<uint> output [[Args1]]) {}
private:
    /** Copies the input into the output without mutating the input buffer. */
    void compute(uint3 id [[DispatchThreadID]]) { output[id.x] = input[id.x] + 1u; }
};

/** Reads one storage texture and writes another so their layout accesses diverge. */
class [[LocalWorkGroupSize(1, 1, 1)]] AccessNarrowingTextures final : public IComputeClass
{
public:
    /** Receives a read-only source and a written destination storage texture. */
    constructor(RWTexture2D<TextureFormat::RGBA8Unorm> source [[Args0]],
                RWTexture2D<TextureFormat::RGBA8Unorm> destination [[Args1]]) {}
private:
    /** Copies the source texel into the destination without writing the source. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        const half4 color = half4(source->read(uint2(id.x, id.y)));
        destination->write(uint2(id.x, id.y), color);
    }
};

/** Writes a marker through a passed resource handle so callers stay writable. */
void fillNarrowingValues(RWStructuredBuffer<uint> target, uint index)
{
    target[index] = 9u;
}

/** Passes a writable buffer to a helper that writes it so the Args resource stays writable. */
class [[LocalWorkGroupSize(1, 1, 1)]] AccessNarrowingHelper final : public IComputeClass
{
public:
    /** Receives the values buffer filled through the helper. */
    constructor(RWStructuredBuffer<uint> values [[Args0]]) {}
private:
    /** Defers the buffer write to the helper so callsite analysis keeps it writable. */
    void compute(uint3 id [[DispatchThreadID]]) { fillNarrowingValues(values, id.x); }
};
