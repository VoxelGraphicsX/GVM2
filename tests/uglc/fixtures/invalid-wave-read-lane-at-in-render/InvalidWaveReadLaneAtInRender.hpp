#ifndef UGLC_TEST_INVALID_WAVE_READ_LANE_AT_IN_RENDER_HPP
#define UGLC_TEST_INVALID_WAVE_READ_LANE_AT_IN_RENDER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidWaveLaneAtRenderVertexOutput
{
    float4 pos [[Position]];
};

struct InvalidWaveLaneAtRenderFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidWaveReadLaneAtInRenderPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidWaveLaneAtRenderVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidWaveLaneAtRenderVertexOutput outputValue;
        outputValue.pos = float4(float(vid) * 0.5f - 0.5f, 0.0f, 0.0f, 1.0f);
        return outputValue;
    }

    InvalidWaveLaneAtRenderFrameBuffer fragment(InvalidWaveLaneAtRenderVertexOutput inputValue)
    {
        InvalidWaveLaneAtRenderFrameBuffer frameBuffer;
        frameBuffer.color = half4(WaveReadLaneAt(inputValue.pos, 0u));
        return frameBuffer;
    }
};

#endif
