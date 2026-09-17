#ifndef TRIANGLE_HEADER1_HPP
#define TRIANGLE_HEADER1_HPP

#include "UGL.h"

using namespace UGL;

struct VertexInput
{
    float4 pos;
    float4 color;
};

struct VertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};
struct TriangleBindGroup : public UGL::IBindGroup
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
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class Triangle final : public IRenderClass
{
    constructor()
    {
    }

private:
    VertexOutput vertex(uint vid [[VertexID]])
    {
        VertexOutput vertexOut = {};
        // Clockwise winding order
        if (vid == 0)
        {
            // Middle top of screen.
            vertexOut.pos = float4(0.0, 1.0, 0.0, 1.0);
            vertexOut.color = float4(1.0, 0.3, 0.3, 1.0);
        }
        else if (vid == 1)
        {
            // Bottom right
            vertexOut.pos = float4(1.0, -1.0, 0.0, 1.0);
            vertexOut.color = float4(0.3, 1.0, 0.3, 1.0);
        }
        else if (vid == 2)
        {
            // Bottom left
            vertexOut.pos = float4(-1.0, -1.0, 0.0, 1.0);
            vertexOut.color = float4(0.3, 0.3, 1.0, 1.0);
        }
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
