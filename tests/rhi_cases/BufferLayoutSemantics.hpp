#pragma once
#include "UGL.h"

using namespace UGL;

/** Combines 16-bit storage with scalar and vector fields in a 16-byte payload. */
struct BufferLayoutLeaf
{
    half2 halves;
    uint marker;
    float2 pair;
};

/** Exercises nested array stride and non-square matrix storage between sentinel vectors. */
struct BufferLayoutPayload
{
    float4 leading;
    BufferLayoutLeaf leaves[2];
    float2x4 rectangle;
    float4x2 narrow;
    float4 trailing;
};

/** Stores nested values and matrix arrays with identical Host, Metal, and Vulkan Uniform layouts. */
struct BufferLayoutUniformPayload
{
    BufferLayoutLeaf leaf;
    float2x4 rectangles[2];
    float3x4 tall;
    uint marker;
    uint padding0;
    uint padding1;
    uint padding2;
};

/** Binds the same accepted layout to storage and uniform access paths. */
struct BufferLayoutBindGroup final : public IBindGroup
{
    /** Creates resources used to compare independent host bytes with shader field reads. */
    constructor(StructuredBuffer<BufferLayoutPayload> storage [[Binding0]],
                UniformBuffer<BufferLayoutUniformPayload> uniformData [[Binding1]],
                RWStructuredBuffer<uint> output [[Binding2]]) {}
};

/** Reads distinctive payload fields from consecutive structured-buffer records. */
class [[LocalWorkGroupSize(1, 1, 1)]] BufferLayoutPass final : public IComputeClass
{
public:
    /** Creates the pass used by both compiler paths and both GPU backends. */
    constructor(BindGroup<BufferLayoutBindGroup> bindGroup [[Slot0]]) {}

private:
    /** Writes exact integer observations of field offsets, array stride, and matrix orientation. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint lane = threadID.x;
        if (lane >= 2u) { return; }
        const uint base = lane * 21u;
        bindGroup->output[base] = uint(bindGroup->storage[lane].leading.x);
        bindGroup->output[base + 1u] = uint(bindGroup->storage[lane].leaves[0].halves.x);
        bindGroup->output[base + 2u] = bindGroup->storage[lane].leaves[0].marker;
        bindGroup->output[base + 3u] = uint(bindGroup->storage[lane].leaves[0].pair.y);
        bindGroup->output[base + 4u] = uint(bindGroup->storage[lane].leaves[1].halves.y);
        bindGroup->output[base + 5u] = bindGroup->storage[lane].leaves[1].marker;
        bindGroup->output[base + 6u] = uint(bindGroup->storage[lane].rectangle[0][1]);
        bindGroup->output[base + 7u] = uint(bindGroup->storage[lane].rectangle[1][3]);
        bindGroup->output[base + 8u] = uint(bindGroup->storage[lane].trailing.w);
        bindGroup->output[base + 9u] = uint(bindGroup->uniformData->leaf.halves.x);
        bindGroup->output[base + 10u] = bindGroup->uniformData->leaf.marker;
        bindGroup->output[base + 11u] = uint(bindGroup->uniformData->leaf.pair.y);
        bindGroup->output[base + 12u] = uint(bindGroup->storage[lane].narrow[2][1]);
        bindGroup->output[base + 13u] = uint(bindGroup->storage[lane].narrow[3][0]);
        bindGroup->output[base + 14u] = uint(bindGroup->uniformData->rectangles[0][0][1]);
        bindGroup->output[base + 15u] = uint(bindGroup->uniformData->rectangles[1][1][3]);
        bindGroup->output[base + 16u] = uint(bindGroup->uniformData->tall[2][3]);
        bindGroup->output[base + 17u] = bindGroup->uniformData->marker;
        uint matrixIndex = lane;
        bindGroup->output[base + 18u] = uint(bindGroup->uniformData->rectangles[matrixIndex++][1][2]);
        bindGroup->output[base + 19u] = bindGroup->storage[lane].leaves[lane].marker;
        bindGroup->output[base + 20u] = matrixIndex;
    }
};
