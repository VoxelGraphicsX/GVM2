#ifndef UGLC_TEST_CONSTRUCTOR_ARGUMENTS_VERTEX_INPUT_ROOT_INDEX_HPP
#define UGLC_TEST_CONSTRUCTOR_ARGUMENTS_VERTEX_INPUT_ROOT_INDEX_HPP

#include "UGL.h"

using namespace UGL;

/** Supplies one vertex input record so the render class also binds a Metal vertex buffer. */
struct RootIndexVertexInput final
{
    float4 pos [[Attribute0]];
};

/** Carries the transformed vertex position into the fragment stage. */
struct RootIndexVertexOutput final
{
    float4 pos [[Position]];
};

/** Defines the existing single-color framebuffer used by the render contract. */
struct RootIndexFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Uses one Args value while consuming vertex input so the Metal Args root must stay at its fixed index. */
class RootIndexVertexMesh final : public IRenderClass
{
public:
    /** Creates the pipeline with one tint argument. */
    constructor(float4 tint [[Args0]])
    {
        setPrimitiveTopology(PrimitiveTopology::TriangleList);
    }

private:
    /** Passes vertex input through without reading the argument. */
    RootIndexVertexOutput vertex(RootIndexVertexInput inputValue [[VertexInput0]])
    {
        RootIndexVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }

    /** Writes the tint argument to the framebuffer. */
    RootIndexFrameBuffer fragment(RootIndexVertexOutput inputValue)
    {
        (void)inputValue;
        RootIndexFrameBuffer frameBuffer;
        frameBuffer.color = half4(tint);
        return frameBuffer;
    }
};

/** Uses Args from compute so its Metal Args root uses the same fixed index as render. */
class [[LocalWorkGroupSize(1, 1, 1)]] RootIndexCompute final : public IComputeClass
{
public:
    /** Creates the pipeline with an output buffer and one scale argument. */
    constructor(RWStructuredBuffer<uint> output [[Args0]], float scale [[Args1]]) {}

private:
    /** Writes the scaled value into the bound output buffer. */
    void compute(uint3 id [[DispatchThreadID]]) { output[id.x] = uint(scale); }
};

#endif
