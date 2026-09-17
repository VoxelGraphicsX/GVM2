#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_STAGE_IO_SEMANTICS_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_STAGE_IO_SEMANTICS_HPP

#include "UGL.h"

using namespace UGL;

/** Provides vertex attributes used to validate symbolic stage-input semantics. */
struct ExperimentalUGLIRStageIOSemanticsVertexInput
{
    float4 position [[Attribute0]];
    float2 uv [[Attribute1]];
};

/** Carries vertex outputs and fragment inputs with position and location semantics. */
struct ExperimentalUGLIRStageIOSemanticsVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
    float4 color [[Attribute1]];
};

/** Declares color and depth outputs used by the stage-output metadata checks. */
struct ExperimentalUGLIRStageIOSemanticsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float, DepthStencilAttachmentWritePattern::Less> depth;
};

/** Emits a minimal render artifact that exercises builtin and location stage IO metadata. */
class ExperimentalUGLIRStageIOSemanticsPass final : public IRenderClass
{
public:
    /** Creates the render pass without resource bindings so stage IO is the only ABI surface. */
    constructor()
    {
    }

private:
    /** Uses VertexID, InstanceID, VertexInput0, Position, and AttributeN semantics. */
    ExperimentalUGLIRStageIOSemanticsVertexOutput vertex(uint vertexID [[VertexID]],
                                                         uint instanceID [[InstanceID]],
                                                         ExperimentalUGLIRStageIOSemanticsVertexInput inputValue [[VertexInput0]])
    {
        ExperimentalUGLIRStageIOSemanticsVertexOutput outputValue;
        const float offset = float((vertexID + instanceID) & 1u) * 0.125f;
        outputValue.position = inputValue.position + float4(offset, 0.0f, 0.0f, 0.0f);
        outputValue.uv = inputValue.uv;
        outputValue.color = float4(inputValue.uv, offset, 1.0f);
        return outputValue;
    }

    /** Writes color and depth outputs from materialized fragment stage inputs. */
    ExperimentalUGLIRStageIOSemanticsFrameBuffer fragment(ExperimentalUGLIRStageIOSemanticsVertexOutput inputValue)
    {
        ExperimentalUGLIRStageIOSemanticsFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color);
        frameBuffer.depth = float(inputValue.color.z);
        return frameBuffer;
    }
};

#endif
