#ifndef QUAD_HPP
#define QUAD_HPP
#include "UGL.h"
using namespace UGL;
struct QuadVertexOutput
{
    float4 pos [[Position]];
    float2 texCoord [[Attribute0]];
};

struct QuadFrameBuffer : public IFrameBuffer
{
    ColorAttachment<UGL::TextureFormat::RGBA8Unorm> color;
};

struct QuadBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

class Quad final : public IRenderClass
{
public:
    constructor(BindGroup<QuadBindGroup> quadBindGroup [[Slot0]])
    {
    }

private:
    QuadVertexOutput vertex(uint vid [[VertexID]])
    {
        float2 output_uv = float2((vid << 1) & 2, vid & 2);
        QuadVertexOutput voutput;
        voutput.pos = float4(output_uv * 2.0f - 1.0f, .0f, 1.0f);
        voutput.texCoord = output_uv;
        return voutput;
    }
    QuadFrameBuffer fragment(QuadVertexOutput vertexIn)
    {
        QuadFrameBuffer foutput;

        float4 res = quadBindGroup->texture0->sample(quadBindGroup->sampler0, vertexIn.texCoord);
        foutput.color = half4(res);
        return foutput;
    }
};

#endif // QUAD_HPP
