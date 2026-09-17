#ifndef UGLC_TEST_RENDER_OVERLOAD_ENTRY_SELECTION_HPP
#define UGLC_TEST_RENDER_OVERLOAD_ENTRY_SELECTION_HPP

#include "UGL.h"

using namespace UGL;

struct RenderOverloadVertexInput
{
    float4 pos [[Attribute0]];
    float2 uv [[Attribute1]];
};

struct RenderOverloadVertexOutput
{
    float4 pos [[Position]];
    float2 uv [[Attribute0]];
};

struct RenderOverloadBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

struct RenderOverloadFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

namespace RenderOverloadEntrySelectionHelpers
{
    inline RenderOverloadVertexOutput buildVertexOutput(RenderOverloadVertexInput inputValue)
    {
        RenderOverloadVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.uv = inputValue.uv;
        return outputValue;
    }

    inline RenderOverloadFrameBuffer buildFrameBuffer(BindGroup<RenderOverloadBindGroup> bindGroup, float2 uv)
    {
        RenderOverloadFrameBuffer frameBuffer;
        frameBuffer.color = half4(bindGroup->texture0->sample(bindGroup->sampler0, uv));
        return frameBuffer;
    }
} // namespace RenderOverloadEntrySelectionHelpers

class RenderOverloadEntrySelectionPass final : public IRenderClass
{
public:
    uint constructorMarker = 0u;

    constructor(BindGroup<RenderOverloadBindGroup> bindGroup [[Slot0]])
    {
        this->constructorMarker = 17u;
    }

private:
    RenderOverloadVertexOutput vertex(uint vid [[VertexID]], RenderOverloadVertexInput inputValue [[VertexInput0]])
    {
        return RenderOverloadEntrySelectionHelpers::buildVertexOutput(inputValue);
    }

    RenderOverloadFrameBuffer fragment(RenderOverloadVertexOutput inputValue)
    {
        return RenderOverloadEntrySelectionHelpers::buildFrameBuffer(bindGroup, inputValue.uv);
    }
};

#endif
