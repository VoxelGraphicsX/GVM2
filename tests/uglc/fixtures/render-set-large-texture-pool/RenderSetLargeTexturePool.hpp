#ifndef UGLC_TEST_RENDER_SET_LARGE_TEXTURE_POOL_HPP
#define UGLC_TEST_RENDER_SET_LARGE_TEXTURE_POOL_HPP

#include "UGL.h"

using namespace UGL;

static constexpr uint RenderSetLargeTexturePoolMaxTextures = 256;

struct RenderSetLargeTexturePoolVertexInput
{
    float4 pos [[Attribute0]];
};

struct RenderSetLargeTexturePoolVertexOutput
{
    float4 pos [[Position]];
    float2 debugValue [[Attribute0]];
};

struct RenderSetLargeTexturePoolFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct RenderSetLargeTexturePoolSet : public IRenderSet
{
    constructor((TextureComponent<half4, RenderSetLargeTexturePoolMaxTextures> albedo),
                BufferComponent<RenderSetLargeTexturePoolVertexInput> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

class RenderSetLargeTexturePoolPass final : public IRenderClass
{
public:
    constructor(RenderSet<RenderSetLargeTexturePoolSet> renderSet [[Slot0]])
    {
    }

private:
    RenderSetLargeTexturePoolVertexOutput vertex(uint vid [[VertexID]],
                                                 RenderSetLargeTexturePoolVertexInput inputValue [[VertexInput0]],
                                                 uint renderEntityID [[RenderEntityID]],
                                                 uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        uint width = 0;
        uint height = 0;
        auto albedoTexture = renderSet->albedo->get(renderEntityID, renderEntityInstanceID);
        albedoTexture->getDimensions(width, height);
        const float4 albedoTexel = float4(albedoTexture->read(uint2(0u, 0u), 0u));

        RenderSetLargeTexturePoolVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.debugValue = float2((width > 0u || height > 0u || albedoTexel.x >= 0.0f) ? 1.0 : 0.0, renderEntityInstanceID < 8u ? 1.0 : 0.0);
        return outputValue;
    }

    RenderSetLargeTexturePoolFrameBuffer fragment(RenderSetLargeTexturePoolVertexOutput inputValue)
    {
        RenderSetLargeTexturePoolFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.debugValue, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
