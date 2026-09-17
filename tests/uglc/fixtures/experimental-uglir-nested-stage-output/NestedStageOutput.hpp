#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_NESTED_STAGE_OUTPUT_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_NESTED_STAGE_OUTPUT_HPP

#include "UGL.h"

using namespace UGL;

/** Provides vertex attributes used to validate symbolic stage-input semantics. */
struct NestedStageVertexInput
{
    float4 position [[Attribute0]];
    float2 uv [[Attribute1]];
};

/** Declares color and depth outputs used by the stage-output metadata checks. */
struct NestedStageFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float, DepthStencilAttachmentWritePattern::Less> depth;
};

/** Preserves stage fields when a template member record has not been instantiated by host C++. */
template <uint Variant>
class NestedStagePass final : public IRenderClass
{
public:
    /** Creates the render pass without resource bindings so stage IO is the only ABI surface. */
    constructor()
    {
    }

private:
    /** Carries vertex outputs and fragment inputs with position and location semantics. */
    struct NestedStageVertexOutput
    {
        float4 position [[Position]];
        float2 uv [[Attribute0]];
        float4 color [[Attribute1]];
    };

    /** Uses VertexID, InstanceID, VertexInput0, Position, and AttributeN semantics. */
    NestedStageVertexOutput vertex(uint vertexID [[VertexID]],
                                                         uint instanceID [[InstanceID]],
                                                         NestedStageVertexInput inputValue [[VertexInput0]])
    {
        NestedStageVertexOutput outputValue;
        const float offset = float((vertexID + instanceID) & 1u) * 0.125f;
        outputValue.position = inputValue.position + float4(offset, 0.0f, 0.0f, 0.0f);
        outputValue.uv = inputValue.uv;
        outputValue.color = float4(inputValue.uv, offset, 1.0f);
        return outputValue;
    }

    /** Writes color and depth outputs from materialized fragment stage inputs. */
    NestedStageFrameBuffer fragment(NestedStageVertexOutput inputValue)
    {
        NestedStageFrameBuffer frameBuffer;
        if (inputValue.uv.x < 1.0f && inputValue.uv.y >= 0.0f)
        {
            frameBuffer.depth = 0.25f;
        }
        if constexpr (Variant == 0u)
        {
            const float componentSum = inputValue.color.x + inputValue.color.y;
            frameBuffer.color = half4(componentSum);
        }
        else
        {
            // C++ permits this pointer, but the discarded branch is outside the shader DSL.
            uint discardedValue = 0u;
            uint *discardedPointer = &discardedValue;
        }
        frameBuffer.depth = 0.5f;
        return frameBuffer;
    }
};

using NestedStagePassZero = NestedStagePass<0>;
/** Declares a host factory whose return type identifies the concrete shader variant. */
RenderClass<NestedStagePassZero> createNestedStagePass();

/** Discovers the shader variant from a factory call without a wrapper variable declaration. */
inline void referenceNestedStagePass()
{
    (void)createNestedStagePass();
}
#endif
