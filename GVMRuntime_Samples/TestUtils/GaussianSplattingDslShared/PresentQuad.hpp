#ifndef GAUSSIAN_SPLATTING_SHARED_PRESENT_QUAD_HPP
#define GAUSSIAN_SPLATTING_SHARED_PRESENT_QUAD_HPP

#include "UGL.h"
using namespace UGL;

struct PresentQuadVertexOutput
{
    float4 pos [[Position]];
    float2 texCoord [[Attribute0]];
};

struct PresentQuadFrameBuffer : public IFrameBuffer
{
    ColorAttachment<UGL::TextureFormat::RGBA8Unorm> color;
};

struct PresentQuadBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> sceneTexture [[Binding0]], Sampler sceneSampler [[Binding1]])
    {
    }
};

class PresentQuad final : public IRenderClass
{
public:
    constructor(BindGroup<PresentQuadBindGroup> presentBindGroup [[Slot0]])
    {
    }

private:
    PresentQuadVertexOutput vertex(uint vid [[VertexID]])
    {
        float2 uv = float2((vid << 1) & 2, vid & 2);
        PresentQuadVertexOutput output;
        output.pos = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
        output.texCoord = uv;
        return output;
    }

    PresentQuadFrameBuffer fragment(PresentQuadVertexOutput input)
    {
        PresentQuadFrameBuffer output;
        float4 sampled = presentBindGroup->sceneTexture->sample(presentBindGroup->sceneSampler, input.texCoord);
        float3 sceneColor = sampled.xyz;
        sceneColor = max(sceneColor, float3(0.0f));
        sceneColor = pow(sceneColor, float3(1.0f / 2.2f));
        output.color = half4(sceneColor, sampled.w);
        return output;
    }
};

#endif
