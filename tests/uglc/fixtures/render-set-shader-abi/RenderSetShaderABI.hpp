#ifndef UGLC_TEST_RENDER_SET_SHADER_ABI_HPP
#define UGLC_TEST_RENDER_SET_SHADER_ABI_HPP

#include "UGL.h"

using namespace UGL;

static constexpr uint RenderSetShaderABIMaxTextures = 4;

struct RenderSetShaderABIVertexInput
{
    float4 pos [[Attribute0]];
};

struct RenderSetShaderABIVertexOutput
{
    float4 pos [[Position]];
    float2 debugValue [[Attribute0]];
};

struct RenderSetShaderABIFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct RenderSetShaderABISet : public IRenderSet
{
    constructor((TextureComponent<half4, RenderSetShaderABIMaxTextures> albedo),
                BufferComponent<RenderSetShaderABIVertexInput> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

class RenderSetShaderABIPass final : public IRenderClass
{
public:
    constructor(RenderSet<RenderSetShaderABISet> renderSet [[Slot0]])
    {
    }

private:
    RenderSetShaderABIVertexOutput vertex(uint vid [[VertexID]],
                                          RenderSetShaderABIVertexInput inputValue [[VertexInput0]],
                                          uint renderEntityID [[RenderEntityID]],
                                          uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        uint width = 0;
        uint height = 0;
        auto albedoTexture = renderSet->albedo->get(renderEntityID, renderEntityInstanceID);
        albedoTexture->getDimensions(width, height);
        const float4 albedoTexel = float4(albedoTexture->read(uint2(0u, 0u), 0u));
        uint instanceCount = renderSet->getRenderEntityInstanceCount(renderEntityID);
        uint infoIndexCount = 0;
        uint infoInstanceCount = 0;
        uint infoFirstIndex = 0;
        int infoVertexOffset = 0;
        uint infoGlobalInstanceBase = 0;
        renderSet->getRenderEntityInfo(renderEntityID, infoIndexCount, infoInstanceCount, infoFirstIndex, infoVertexOffset, infoGlobalInstanceBase);
        uint directGlobalInstanceBase = renderSet->getRenderEntityGlobalInstanceBase(renderEntityID);
        uint entityVersion = renderSet->getRenderEntityVersion(renderEntityID);
        bool renderEntityValid = renderSet->checkValid(renderEntityID);

        RenderSetShaderABIVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.debugValue = float2((renderEntityValid || width > 0u || height > 0u || entityVersion > 0u || infoFirstIndex > 0u || albedoTexel.x >= 0.0f) ? 1.0 : 0.0,
                                        (instanceCount > 0u || infoInstanceCount > 0u || infoIndexCount > 0u || infoGlobalInstanceBase > 0u || directGlobalInstanceBase > 0u || infoVertexOffset > 0) ? 1.0 : 0.0);
        return outputValue;
    }

    RenderSetShaderABIFrameBuffer fragment(RenderSetShaderABIVertexOutput inputValue)
    {
        RenderSetShaderABIFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.debugValue, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
