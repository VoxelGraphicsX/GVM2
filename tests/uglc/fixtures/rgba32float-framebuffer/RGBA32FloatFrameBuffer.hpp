#ifndef UGLC_TEST_RGBA32FLOAT_FRAMEBUFFER_HPP
#define UGLC_TEST_RGBA32FLOAT_FRAMEBUFFER_HPP

#include "UGL.h"

using namespace UGL;

/** Provides one clip-space position to the RGBA32Float render fixture. */
struct RGBA32FloatVertexInput final
{
    float4 position [[Attribute0]];
};

/** Carries the fixture position from the vertex stage to rasterization. */
struct RGBA32FloatVertexOutput final
{
    float4 position [[Position]];
};

/** Declares the full-precision four-channel framebuffer under regression. */
struct RGBA32FloatFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA32Float> color;
};

/** Writes a float4 payload to an RGBA32Float color attachment. */
class RGBA32FloatRenderPass final : public IRenderClass
{
public:
    /** Creates the render pass without resource bindings. */
    constructor()
    {
    }

private:
    /** Forwards the authored clip-space position to the fragment stage. */
    RGBA32FloatVertexOutput vertex(
        RGBA32FloatVertexInput inputValue [[VertexInput0]])
    {
        RGBA32FloatVertexOutput outputValue;
        outputValue.position = inputValue.position;
        return outputValue;
    }

    /** Returns a non-half-representable float4 payload for ABI validation. */
    RGBA32FloatFrameBuffer fragment(RGBA32FloatVertexOutput inputValue)
    {
        RGBA32FloatFrameBuffer frameBuffer;
        frameBuffer.color = float4(
            inputValue.position.x + 0.00000011920928955078125f,
            inputValue.position.y,
            inputValue.position.z,
            inputValue.position.w);
        return frameBuffer;
    }
};

#endif
