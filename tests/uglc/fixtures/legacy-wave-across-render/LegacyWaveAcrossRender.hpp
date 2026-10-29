#ifndef UGLC_TEST_LEGACY_WAVE_ACROSS_RENDER_HPP
#define UGLC_TEST_LEGACY_WAVE_ACROSS_RENDER_HPP

#include "UGL.h"

using namespace UGL;

struct LegacyWaveAcrossVertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};

struct LegacyWaveAcrossFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class LegacyWaveAcrossRenderPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    LegacyWaveAcrossVertexOutput vertex(uint vid [[VertexID]])
    {
        LegacyWaveAcrossVertexOutput outputValue;
        outputValue.pos = float4(float(vid) * 0.5f - 0.5f, 0.0f, 0.0f, 1.0f);
        outputValue.color = float4(0.1f, 0.2f, 0.3f, 1.0f);
        return outputValue;
    }

    LegacyWaveAcrossFrameBuffer fragment(LegacyWaveAcrossVertexOutput inputValue)
    {
        LegacyWaveAcrossFrameBuffer frameBuffer;
        frameBuffer.color = half4(WaveReadAcrossDiagonal(WaveReadAcrossY(WaveReadAcrossX(inputValue.color))));
        return frameBuffer;
    }
};

#endif
