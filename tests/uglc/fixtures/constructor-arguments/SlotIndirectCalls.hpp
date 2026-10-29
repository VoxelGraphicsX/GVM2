#pragma once
#include "UGL.h"
using namespace UGL;

/** Supplies a conventional Slot binding for both indirect execution entry points. */
struct SlotIndirectResources final : IBindGroup
{
    constructor(RWStructuredBuffer<uint> output [[Binding0]]) {}
};

/** Supplies the existing position semantic for a Slot render pipeline. */
struct SlotIndirectVertex
{
    float4 position [[Position]];
};

/** Supplies a conventional framebuffer for a Slot render pipeline. */
struct SlotIndirectFrame final : IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Retains constructor Slot bindings when indirect controls select the Args placeholder overload. */
class SlotIndirectRender final : public IRenderClass
{
public:
    /** Creates a render pipeline with the existing binding model. */
    constructor(BindGroup<SlotIndirectResources> resources [[Slot0]]) {}
private:
    /** Reads a bound resource without adding a per-draw Args prefix. */
    SlotIndirectVertex vertex(uint id [[VertexID]])
    {
        SlotIndirectVertex result;
        result.position = float4(float(resources->output[id]), 0.0f, 0.0f, 1.0f);
        return result;
    }

    /** Writes the conventional color attachment. */
    SlotIndirectFrame fragment(SlotIndirectVertex input)
    {
        SlotIndirectFrame result;
        result.color = half4(1.0f);
        return result;
    }
};

/** Retains constructor Slot bindings when dispatch uses either indirect overload. */
class [[LocalWorkGroupSize(1, 1, 1)]] SlotIndirectCompute final : public IComputeClass
{
public:
    /** Creates a compute pipeline with the existing binding model. */
    constructor(BindGroup<SlotIndirectResources> resources [[Slot0]]) {}
private:
    /** Writes resource contents through the existing bind group. */
    void compute(uint3 id [[DispatchThreadID]]) { resources->output[id.x] = id.x; }
};

using SlotIndirectBuffer = Buffer<uint, BufferUsage<Indirect>>;
using SlotIndirectRange = BufferRange<uint, BufferUsage<Indirect>>;
using SlotIndexBuffer = Buffer<uint, BufferUsage<Index>>;

/** Exercises literal, sizeof, lvalue, and range controls without opting the receiver into Args. */
inline void validateSlotIndirectCalls(RenderClass<SlotIndirectRender> render,
                                      SlotIndirectBuffer commands, SlotIndirectRange range, SlotIndexBuffer indices)
{
    const size_t stride = sizeof(uint) * 4;
    render->drawIndirect(commands, 1u, 16u);
    render->drawIndirect(commands, 1u, sizeof(uint) * 4);
    render->drawIndirect(commands, size_t(1), stride);
    render->drawIndirect(range, size_t(1), stride);
    render->drawIndirectCount(commands, range, 8u);
    render->drawIndirectCount(range, commands, 8u, 16u);
    render->drawIndexedIndirectCount(indices, IndexFormat::Uint32, range, commands, 8u);
    render->drawIndexedIndirectCount(indices, IndexFormat::Uint16, commands, range, 8u, 24u);
}
