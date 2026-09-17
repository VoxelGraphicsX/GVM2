#ifndef UGLC_TEST_RENDER_CLIP_FRAGMENT_HPP
#define UGLC_TEST_RENDER_CLIP_FRAGMENT_HPP

#include "UGL.h"

using namespace UGL;

/** Carries interpolated clip test values from the vertex shader into the fragment shader. */
struct RenderClipVertexOutput
{
    float4 pos [[Position]];
    float2 uv [[Attribute0]];
};

/** Declares the color target used by the fragment clip fixture. */
struct RenderClipFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Emits scalar and vector clip calls from a normal fragment shader. */
class RenderClipFragmentPass final : public IRenderClass
{
public:
    /** Creates a stateless render pass used by the clip lowering fixture. */
    constructor()
    {
    }

private:
    /** Generates simple clip-space positions and interpolated test coordinates. */
    RenderClipVertexOutput vertex(uint vid [[VertexID]])
    {
        RenderClipVertexOutput outputValue;
        outputValue.pos = float4(float(vid), 0.0f, 0.0f, 1.0f);
        outputValue.uv = float2(float(vid), 0.25f);
        return outputValue;
    }

    /** Clips the fragment with both qualified scalar and unqualified vector DSL calls. */
    RenderClipFrameBuffer fragment(RenderClipVertexOutput inputValue)
    {
        UGL::clip(inputValue.uv.x - 0.5f);
        clip(float4(inputValue.uv.x - 0.25f, inputValue.uv.y - 0.125f, 1.0f, 1.0f));

        RenderClipFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.uv.x, inputValue.uv.y, 0.0f, 1.0f);
        return frameBuffer;
    }
};

#endif
