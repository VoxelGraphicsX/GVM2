#ifndef TRIANGLE_HEADER1_HPP
#define TRIANGLE_HEADER1_HPP

#include "UGL.h"
using namespace UGL;
struct VertexInput
{
    float4 pos [[Attribute0]];
    float4 color [[Attribute1]];
};

struct VertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};
struct TriangleBindGroup : public IBindGroup
{

    constructor(StructuredBuffer<VertexOutput> buffer0 [[Binding0]])
    {
    }
};
struct TriangleBindGroup2 : public IBindGroup
{
    constructor(StructuredBuffer<VertexOutput> buffer0 [[Binding0]], Texture2D<float4> texture0 [[Binding1]])
    {
    }
};

struct TriangleFrameBuffer : public IFrameBuffer
{
    ColorAttachment<UGL::TextureFormat::RGBA8Unorm> color;
};

class Triangle final : public IRenderClass
{
    constructor()
    {
    }

private:
    VertexOutput vertex(uint vid [[VertexID]], VertexInput vInput [[VertexInput0]])
    {
        VertexOutput vertexOut;
        // Clockwise winding order
        vertexOut.pos = vInput.pos;
        vertexOut.color = vInput.color;
        return vertexOut;
    }
    TriangleFrameBuffer fragment(VertexOutput vertexIn)
    {
        TriangleFrameBuffer frameBuffer;
        frameBuffer.color = half4(vertexIn.color);
        return frameBuffer;
    }
};
#endif // TRIANGLE_HEADER1_HPP
