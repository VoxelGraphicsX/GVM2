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
    constructor(Texture2D<float4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

struct TriangleFrameBuffer : public IFrameBuffer
{
    ColorAttachment<UGL::TextureFormat::RGBA8Unorm> color;
};
class Triangle final : public IRenderClass
{
    constructor(BindGroup<TriangleBindGroup> bindGroup [[Slot0]])
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
        float4 texCol = bindGroup->texture0->sample(bindGroup->sampler0, vertexIn.pos.xy);
        TriangleFrameBuffer frameBuffer;
        frameBuffer.color = half4(texCol);
        return frameBuffer;
    }
};
#endif // TRIANGLE_HEADER1_HPP
