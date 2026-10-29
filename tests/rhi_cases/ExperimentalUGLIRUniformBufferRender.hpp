#ifndef GVM_TEST_RHI_EXPERIMENTAL_UGLIR_UNIFORM_BUFFER_RENDER_HPP
#define GVM_TEST_RHI_EXPERIMENTAL_UGLIR_UNIFORM_BUFFER_RENDER_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the tint consumed by the experimental UGLIR render uniform smoke test. */
struct ExperimentalUGLIRRuntimeRenderUniformParams
{
    float4 tint;
};

/** Binds the render uniform buffer used by the procedural triangle pass. */
struct ExperimentalUGLIRRuntimeRenderUniformBindGroup final : public IBindGroup
{
    /** Creates the bind group with a read-only uniform buffer. */
    constructor(UniformBuffer<ExperimentalUGLIRRuntimeRenderUniformParams> params [[Binding0]])
    {
    }
};

/** Carries the generated vertex position into the fragment stage. */
struct ExperimentalUGLIRRuntimeRenderUniformVertexOutput
{
    float4 position [[Position]];
};

/** Defines the single color attachment used by the render uniform smoke test. */
struct ExperimentalUGLIRRuntimeRenderUniformFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Draws a full-screen triangle and shades it from a UniformBuffer value. */
class ExperimentalUGLIRRuntimeRenderUniformPass final : public IRenderClass
{
public:
    /** Creates the render pass with the runtime uniform bind group. */
    constructor(BindGroup<ExperimentalUGLIRRuntimeRenderUniformBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Emits a full-screen triangle without a vertex buffer. */
    ExperimentalUGLIRRuntimeRenderUniformVertexOutput vertex(uint vertexID [[VertexID]])
    {
        ExperimentalUGLIRRuntimeRenderUniformVertexOutput output;
        if (vertexID == 0u)
        {
            output.position = float4(-1.0f, -1.0f, 0.0f, 1.0f);
        }
        else if (vertexID == 1u)
        {
            output.position = float4(3.0f, -1.0f, 0.0f, 1.0f);
        }
        else
        {
            output.position = float4(-1.0f, 3.0f, 0.0f, 1.0f);
        }
        return output;
    }

    /** Writes the uniform tint to the framebuffer color target. */
    ExperimentalUGLIRRuntimeRenderUniformFrameBuffer fragment(ExperimentalUGLIRRuntimeRenderUniformVertexOutput inputValue)
    {
        ExperimentalUGLIRRuntimeRenderUniformFrameBuffer framebuffer;
        framebuffer.color = half4(bindGroup->params->tint);
        return framebuffer;
    }
};

#endif
