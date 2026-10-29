#ifndef UGLC_TEST_TEXTURE2DARRAY_GATHER_LOWERING_HPP
#define UGLC_TEST_TEXTURE2DARRAY_GATHER_LOWERING_HPP

#include "UGL.h"

using namespace UGL;

struct Texture2DArrayGatherVertexInput
{
    float4 pos [[Attribute0]];
    float2 uv [[Attribute1]];
};

struct Texture2DArrayGatherVertexOutput
{
    float4 pos [[Position]];
    float2 uv [[Attribute0]];
};

struct Texture2DArrayGatherBindGroup final : public IBindGroup
{
    constructor(Texture2DArray<float4> textureArray [[Binding0]],
                Sampler sampler0 [[Binding1]])
    {
    }
};

struct Texture2DArrayGatherFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class Texture2DArrayGatherPass final : public IRenderClass
{
public:
    constructor(BindGroup<Texture2DArrayGatherBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    Texture2DArrayGatherVertexOutput vertex(uint vid [[VertexID]], Texture2DArrayGatherVertexInput inputValue [[VertexInput0]])
    {
        Texture2DArrayGatherVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.uv = inputValue.uv;
        return outputValue;
    }

    Texture2DArrayGatherFrameBuffer fragment(Texture2DArrayGatherVertexOutput inputValue)
    {
        Texture2DArrayGatherFrameBuffer frameBuffer;
        frameBuffer.color = half4(bindGroup->textureArray->gatherGreen(bindGroup->sampler0, inputValue.uv, 1u, int2(1, 2)));
        return frameBuffer;
    }
};

#endif
