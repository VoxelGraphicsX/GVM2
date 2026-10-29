#ifndef UGLC_TEST_RENDER_DEPTH_WRITE_ENABLED_REGRESSION_HPP
#define UGLC_TEST_RENDER_DEPTH_WRITE_ENABLED_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

/// Provides the vertex input used by the explicit depth write regression pass.
struct RenderDepthWriteEnabledVertexInput
{
    float4 pos [[Attribute0]];
};

/// Carries clip-space position from the vertex shader to the fragment shader.
struct RenderDepthWriteEnabledVertexOutput
{
    float4 pos [[Position]];
};

/// Defines the render target layout used to verify explicit depth write pipeline state.
struct RenderDepthWriteEnabledFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float, DepthStencilAttachmentWritePattern::Less> depth;
};

/// Renders a depth target while disabling pipeline depth writes through the explicit API.
class RenderDepthWriteEnabledPass final : public IRenderClass
{
public:
    /// Configures depth compare and then disables pipeline depth writes before pipeline creation.
    constructor()
    {
        setDepthCompareFunction(CompareFunction::LessEqual);
        setDepthWriteEnabled(false);
    }

private:
    /// Forwards the clip-space position to the fragment shader for deterministic depth output.
    RenderDepthWriteEnabledVertexOutput vertex(uint vid [[VertexID]],
                                               RenderDepthWriteEnabledVertexInput inputValue [[VertexInput0]])
    {
        RenderDepthWriteEnabledVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }

    /// Writes color and native depth so the fixture exercises shader depth-output semantics.
    RenderDepthWriteEnabledFrameBuffer fragment(RenderDepthWriteEnabledVertexOutput inputValue)
    {
        RenderDepthWriteEnabledFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.xy, 0.0, 1.0);
        frameBuffer.depth = float(inputValue.pos.z);
        return frameBuffer;
    }
};

#endif
