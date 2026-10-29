#pragma once
#include "UGL.h"
#include <array>
using namespace UGL;

struct ResourceArgumentPayload;

/** Keeps fixed arrays of resource handles as ordinary readonly constructor inputs. */
struct ResourceArgumentPayload
{
    std::array<Texture2D<float4>, 2> images;
    std::array<Sampler, 2> samplers;
    std::array<RWStructuredBuffer<uint>, 2> outputs;
};

/** Preserves first-class resource values across a helper whose parameter shadows a root Args field. */
float4 sampleResourceArgument(Texture2D<float4> images, Sampler sampling)
{
    return images->sampleLevel(sampling, float2(0.5f), 0.0f);
}

/** Exercises fixed arrays, local copies, assignment, and invocation-varying resource selection. */
class [[LocalWorkGroupSize(1, 1, 1)]] ResourceArgumentCompute final : public IComputeClass
{
public:
    /** Requires the same resource aggregate and scalar prefix for construction and dispatch. */
    constructor(ResourceArgumentPayload resources [[Args0]], uint selection [[Args1]])
    {
    }
private:
    /** Selects descriptors through normal C++ value expressions without extra descriptor-set parameters. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        auto local = resources;
        auto image = local.images[0];
        image = local.images[id.x & 1u];
        const auto color = sampleResourceArgument(image, local.samplers[selection]);
        local.outputs[id.x & 1u][0] = uint(color.x);
    }
};

/** Preserves the existing macro spelling for a comma-containing top-level resource array parameter. */
class [[LocalWorkGroupSize(1, 1, 1)]] ResourceArgumentUniformCompute final : public IComputeClass
{
public:
    /** Wraps the comma-containing type in the constructor macro's existing parenthesis syntax. */
    constructor((std::array<Texture2D<uint4>, 2> images [[Args0]]), RWStructuredBuffer<uint> output [[Args1]], uint selection [[Args2]])
    {
    }
private:
    /** Uses a root-uniform table index without requesting the non-uniform indexing capability. */
    void compute()
    {
        output[0] = images[selection]->read(uint2(0), 0u).x;
    }
};

/** Preserves stage visibility through top-level arrays and arrays of resource-containing records. */
class [[LocalWorkGroupSize(1, 1, 1)]] ResourceArgumentTopLevelArrays final : public IComputeClass
{
public:
    /** Uses the constructor macro's parenthesized spelling for each comma-containing array type. */
    constructor((std::array<ResourceArgumentPayload, 2> records [[Args0]]), (std::array<Sampler, 2> sampling [[Args1]]),
        (std::array<RWStructuredBuffer<uint>, 2> outputs [[Args2]]))
    {
    }
private:
    /** Reaches image, sampler, and buffer resources whose flattened paths start with an array subscript. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        const auto index = id.x & 1u;
        const auto color = sampleResourceArgument(records[0].images[index], sampling[index]);
        outputs[index][0] = uint(color.x);
    }
};
