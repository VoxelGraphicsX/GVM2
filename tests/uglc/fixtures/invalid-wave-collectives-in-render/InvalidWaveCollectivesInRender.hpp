#ifndef UGLC_TEST_INVALID_WAVE_COLLECTIVES_IN_RENDER_HPP
#define UGLC_TEST_INVALID_WAVE_COLLECTIVES_IN_RENDER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidWaveCollectiveVertexOutput
{
    float4 pos [[Position]];
    float value [[Attribute0]];
};

struct InvalidWaveCollectiveFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidWaveCollectivesInRenderPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidWaveCollectiveVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidWaveCollectiveVertexOutput outputValue;
        outputValue.pos = float4(float(vid), 0.0f, 0.0f, 1.0f);
        outputValue.value = float(vid);
        return outputValue;
    }

    InvalidWaveCollectiveFrameBuffer fragment(InvalidWaveCollectiveVertexOutput inputValue)
    {
        InvalidWaveCollectiveFrameBuffer frameBuffer;
        const uint4 mask = WaveActiveBallot(inputValue.value > 0.0f);
        frameBuffer.color = half4(float(mask.x), 0.0f, 0.0f, 1.0f);
        return frameBuffer;
    }
};

#endif
