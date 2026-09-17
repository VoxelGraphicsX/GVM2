#ifndef UGLC_TEST_RENDER_ENTITY_BUILTIN_MULTIPLE_RENDER_SETS_HPP
#define UGLC_TEST_RENDER_ENTITY_BUILTIN_MULTIPLE_RENDER_SETS_HPP

#include "UGL.h"

using namespace UGL;

struct RenderEntityBuiltinMultiSetVertexInput
{
    float4 pos [[Attribute0]];
};

struct RenderEntityBuiltinMultiSetVertexOutput
{
    float4 pos [[Position]];
    float value [[Attribute0]];
};

struct RenderEntityBuiltinMultiSetFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct RenderEntityBuiltinFirstSet : public IRenderSet
{
    constructor(BufferComponent<RenderEntityBuiltinMultiSetVertexInput> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

struct RenderEntityBuiltinSecondSet : public IRenderSet
{
    constructor(BufferComponent<RenderEntityBuiltinMultiSetVertexInput> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

class RenderEntityBuiltinMultipleRenderSetsPass final : public IRenderClass
{
public:
    constructor(RenderSet<RenderEntityBuiltinFirstSet> firstSet [[Slot0]],
                RenderSet<RenderEntityBuiltinSecondSet> secondSet [[Slot1]])
    {
    }

private:
    RenderEntityBuiltinMultiSetVertexOutput vertex(uint vid [[VertexID]],
                                                  RenderEntityBuiltinMultiSetVertexInput inputValue [[VertexInput0]],
                                                  uint renderEntityID [[RenderEntityID]])
    {
        RenderEntityBuiltinMultiSetVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.value = float(renderEntityID + vid);
        return outputValue;
    }

    RenderEntityBuiltinMultiSetFrameBuffer fragment(RenderEntityBuiltinMultiSetVertexOutput inputValue)
    {
        RenderEntityBuiltinMultiSetFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.value, 0.0, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
