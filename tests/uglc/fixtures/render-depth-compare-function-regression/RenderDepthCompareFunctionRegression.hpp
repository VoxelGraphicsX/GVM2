#ifndef UGLC_TEST_RENDER_DEPTH_COMPARE_FUNCTION_REGRESSION_HPP
#define UGLC_TEST_RENDER_DEPTH_COMPARE_FUNCTION_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

/// Provides the vertex input used by the explicit depth compare regression pass.
struct RenderDepthCompareFunctionVertexInput
{
    float4 pos [[Attribute0]];
};

/// Carries clip-space position from the vertex shader to the fragment shader.
struct RenderDepthCompareFunctionVertexOutput
{
    float4 pos [[Position]];
};

/// Defines the render target layout used to verify depth pattern and compare-state decoupling.
struct RenderDepthCompareFunctionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float, DepthStencilAttachmentWritePattern::Less> depth;
};

/// Renders a depth-writing target while setting pipeline depth compare through the explicit API.
class RenderDepthCompareFunctionPass final : public IRenderClass
{
public:
    /// Configures the pipeline compare function independently from the depth write pattern.
    constructor()
    {
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /// Forwards the clip-space position to the fragment shader for deterministic depth output.
    RenderDepthCompareFunctionVertexOutput vertex(uint vid [[VertexID]],
                                                  RenderDepthCompareFunctionVertexInput inputValue [[VertexInput0]])
    {
        RenderDepthCompareFunctionVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }

    /// Writes color and native depth so the fixture exercises shader depth-output semantics.
    RenderDepthCompareFunctionFrameBuffer fragment(RenderDepthCompareFunctionVertexOutput inputValue)
    {
        RenderDepthCompareFunctionFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.xy, 0.0, 1.0);
        frameBuffer.depth = float(inputValue.pos.z);
        return frameBuffer;
    }
};

#endif
