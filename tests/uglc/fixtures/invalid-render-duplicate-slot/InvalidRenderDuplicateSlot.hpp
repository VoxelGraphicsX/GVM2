#ifndef UGLC_TEST_INVALID_RENDER_DUPLICATE_SLOT_HPP
#define UGLC_TEST_INVALID_RENDER_DUPLICATE_SLOT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderDuplicateSlotBindGroup0 final : public IBindGroup
{
    constructor(UniformBuffer<float4> value [[Binding0]])
    {
    }
};

struct InvalidRenderDuplicateSlotBindGroup1 final : public IBindGroup
{
    constructor(UniformBuffer<float4> value [[Binding0]])
    {
    }
};

struct InvalidRenderDuplicateSlotFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct InvalidRenderDuplicateSlotVertexOutput
{
    float4 pos [[Position]];
};

class InvalidRenderDuplicateSlotPass final : public IRenderClass
{
public:
    constructor(BindGroup<InvalidRenderDuplicateSlotBindGroup0> firstBindGroup [[Slot0]],
                BindGroup<InvalidRenderDuplicateSlotBindGroup1> secondBindGroup [[Slot0]])
    {
    }

private:
    InvalidRenderDuplicateSlotVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidRenderDuplicateSlotVertexOutput outputValue = {};
        outputValue.pos = float4(vid == 0u ? -1.0 : 1.0, 0.0, 0.0, 1.0);
        return outputValue;
    }

    InvalidRenderDuplicateSlotFrameBuffer fragment(InvalidRenderDuplicateSlotVertexOutput inputValue)
    {
        InvalidRenderDuplicateSlotFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.x, 0.0, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
