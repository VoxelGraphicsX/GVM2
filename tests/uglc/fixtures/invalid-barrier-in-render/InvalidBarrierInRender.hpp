#ifndef UGLC_TEST_INVALID_BARRIER_IN_RENDER_HPP
#define UGLC_TEST_INVALID_BARRIER_IN_RENDER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidBarrierRenderVertexOutput
{
    float4 pos [[Position]];
};

struct InvalidBarrierRenderFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidBarrierInRenderPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidBarrierRenderVertexOutput vertex(uint vid [[VertexID]])
    {
        UGL::DeviceMemoryBarrierWithGroupSync();

        InvalidBarrierRenderVertexOutput outputValue;
        outputValue.pos = float4(float(vid), 0.0f, 0.0f, 1.0f);
        return outputValue;
    }

    InvalidBarrierRenderFrameBuffer fragment(InvalidBarrierRenderVertexOutput inputValue)
    {
        InvalidBarrierRenderFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.x, 0.0f, 0.0f, 1.0f);
        return frameBuffer;
    }
};

#endif
