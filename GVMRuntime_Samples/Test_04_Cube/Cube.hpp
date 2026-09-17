#ifndef CUBE_HPP
#define CUBE_HPP

#include "UGL.h"
using namespace UGL;

#include "Camera.hpp"
struct CubeVertexInput
{
    float4 pos [[Attribute0]];
    float4 color [[Attribute1]];
    float2 texCoord [[Attribute2]];
};

struct CubeVertexOutput
{
    float4 pos [[Position]];
    float3 color [[Attribute0]];
    float2 texCoord [[Attribute1]];
};

struct CubeFrameBuffer : public IFrameBuffer
{
    ColorAttachment<UGL::TextureFormat::BGRA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depthStencil;
};
class CubeDraw final : public IRenderClass
{
    constructor(BindGroup<CameraBindGroup> camBindGroup [[Slot0]])
    {
    }

private:
    CubeVertexOutput vertex(uint vid [[VertexID]], CubeVertexInput vInput [[VertexInput0]])
    {
        CubeVertexOutput vertexOut;
        // Clockwise winding order
        vertexOut.pos = mul(camBindGroup->camBuffer->proj, mul(camBindGroup->camBuffer->view, float4(vInput.pos.xyz, 1.0)));
        // vertexOut.pos = mul(camBindGroup->camBuffer->view, vInput.pos);
        vertexOut.color = vInput.color.xyz;
        vertexOut.texCoord = vInput.texCoord;
        return vertexOut;
    }
    CubeFrameBuffer fragment(CubeVertexOutput vertexIn)
    {

        CubeFrameBuffer frameBuffer;
        frameBuffer.color = half4(vertexIn.color.x, vertexIn.color.y, vertexIn.color.z, 1.0f);
        return frameBuffer;
    }
};
#endif // CUBE_HPP
