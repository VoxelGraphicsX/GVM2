#ifndef UGLC_TEST_INVALID_CLIP_IN_VERTEX_HPP
#define UGLC_TEST_INVALID_CLIP_IN_VERTEX_HPP

#include "UGL.h"

using namespace UGL;

/** Carries clip-space position out of the invalid vertex shader. */
struct InvalidClipVertexOutput
{
    float4 pos [[Position]];
};

/** Declares the color target for the invalid clip fixture. */
struct InvalidClipFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Calls clip from a vertex shader to validate fragment-only diagnostics. */
class InvalidClipInVertexPass final : public IRenderClass
{
public:
    /** Creates a stateless render pass used only for diagnostic validation. */
    constructor()
    {
    }

private:
    /** Invalidly calls clip before returning a vertex output. */
    InvalidClipVertexOutput vertex(uint vid [[VertexID]])
    {
        UGL::clip(float(vid) - 1.0f);

        InvalidClipVertexOutput outputValue;
        outputValue.pos = float4(float(vid), 0.0f, 0.0f, 1.0f);
        return outputValue;
    }

    /** Emits a trivial color so the render class is otherwise complete. */
    InvalidClipFrameBuffer fragment(InvalidClipVertexOutput inputValue)
    {
        InvalidClipFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.x, 0.0f, 0.0f, 1.0f);
        return frameBuffer;
    }
};

#endif
