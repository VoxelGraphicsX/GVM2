#ifndef UGLC_TEST_INVALID_WAVE_BUILTINS_IN_RENDER_HPP
#define UGLC_TEST_INVALID_WAVE_BUILTINS_IN_RENDER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidWaveRenderVertexOutput
{
    float4 pos [[Position]];
};

struct InvalidWaveRenderFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidWaveBuiltinsInRenderPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidWaveRenderVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidWaveRenderVertexOutput outputValue;
        outputValue.pos = float4(float(WaveGetLaneIndex() + vid), 0.0f, 0.0f, 1.0f);
        return outputValue;
    }

    InvalidWaveRenderFrameBuffer fragment(InvalidWaveRenderVertexOutput inputValue)
    {
        InvalidWaveRenderFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.x, 0.0f, 0.0f, 1.0f);
        return frameBuffer;
    }
};

#endif
