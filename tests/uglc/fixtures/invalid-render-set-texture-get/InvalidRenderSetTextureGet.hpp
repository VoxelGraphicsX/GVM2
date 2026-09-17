#ifndef UGLC_TEST_INVALID_RENDER_SET_TEXTURE_GET_HPP
#define UGLC_TEST_INVALID_RENDER_SET_TEXTURE_GET_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderSetTextureGetVertexInput
{
    float4 pos [[Attribute0]];
};

struct InvalidRenderSetTextureGetVertexOutput
{
    float4 pos [[Position]];
};

struct InvalidRenderSetTextureGetFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct InvalidRenderSetTextureGetSet : public IRenderSet
{
    constructor((TextureComponent<half4, 4> albedo),
                BufferComponent<InvalidRenderSetTextureGetVertexInput> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

class InvalidRenderSetTextureGetPass final : public IRenderClass
{
public:
    constructor(RenderSet<InvalidRenderSetTextureGetSet> renderSet [[Slot0]])
    {
    }

private:
    InvalidRenderSetTextureGetVertexOutput vertex(InvalidRenderSetTextureGetVertexInput inputValue [[VertexInput0]],
                                                  uint renderEntityID [[RenderEntityID]],
                                                  uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        auto texture = renderSet->albedo->get(renderEntityID, renderEntityInstanceID);
        uint width = 0;
        uint height = 0;
        texture->getDimensions(width, height);

        InvalidRenderSetTextureGetVertexOutput outputValue;
        outputValue.pos = inputValue.pos + float4(float(width + height) * 0.0, 0.0, 0.0, 0.0);
        return outputValue;
    }

    InvalidRenderSetTextureGetFrameBuffer fragment(InvalidRenderSetTextureGetVertexOutput inputValue)
    {
        InvalidRenderSetTextureGetFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos);
        return frameBuffer;
    }
};

#endif
