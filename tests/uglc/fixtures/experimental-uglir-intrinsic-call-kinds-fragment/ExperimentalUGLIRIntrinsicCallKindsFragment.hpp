#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INTRINSIC_CALL_KINDS_FRAGMENT_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INTRINSIC_CALL_KINDS_FRAGMENT_HPP

#include "UGL.h"

using namespace UGL;

/** Provides vertex attributes for the fragment intrinsic-call render fixture. */
struct ExperimentalUGLIRIntrinsicCallKindsFragmentVertexInput
{
    float4 position [[Attribute0]];
    float2 uv [[Attribute1]];
};

/** Carries interpolated values into the fragment intrinsic-call shader. */
struct ExperimentalUGLIRIntrinsicCallKindsFragmentVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Binds textures and a sampler used by fragment texture intrinsic calls. */
struct ExperimentalUGLIRIntrinsicCallKindsFragmentBindGroup final : public IBindGroup
{
    /** Declares sampled textures and a sampler for texture call-kind lowering. */
    constructor(Texture2D<TextureFormat::RGBA8Unorm> colorTexture [[Binding0]],
                Texture2DArray<TextureFormat::RGBA16Float> arrayTexture [[Binding1]],
                Sampler sampler0 [[Binding2]])
    {
    }
};

/** Declares the color target used by the fragment intrinsic-call fixture. */
struct ExperimentalUGLIRIntrinsicCallKindsFragmentFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Emits fragment-only intrinsic calls such as derivatives, sampling, gather, dimensions, and discard. */
class ExperimentalUGLIRIntrinsicCallKindsFragmentPass final : public IRenderClass
{
public:
    /** Stores the bind group consumed by the fragment intrinsic fixture. */
    constructor(BindGroup<ExperimentalUGLIRIntrinsicCallKindsFragmentBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Forwards vertex input to fragment input so texture coordinates are stage materialized. */
    ExperimentalUGLIRIntrinsicCallKindsFragmentVertexOutput vertex(uint vertexID [[VertexID]],
                                                                   ExperimentalUGLIRIntrinsicCallKindsFragmentVertexInput inputValue [[VertexInput0]])
    {
        ExperimentalUGLIRIntrinsicCallKindsFragmentVertexOutput outputValue;
        outputValue.position = inputValue.position;
        outputValue.uv = inputValue.uv + float2(float(vertexID & 1u) * 0.0f, 0.0f);
        return outputValue;
    }

    /** Emits texture, derivative, getDimensions, and discard calls in one fragment entry. */
    ExperimentalUGLIRIntrinsicCallKindsFragmentFrameBuffer fragment(ExperimentalUGLIRIntrinsicCallKindsFragmentVertexOutput inputValue)
    {
        uint width = 0u;
        uint height = 0u;
        bindGroup->colorTexture->getDimensions(width, height);

        const float2 dx = ddx(inputValue.uv);
        const float2 dy = ddy(inputValue.uv);
        const float4 sampled = float4(bindGroup->colorTexture->sample(bindGroup->sampler0, inputValue.uv));
        const float4 levelSampled = float4(bindGroup->colorTexture->sampleLevel(bindGroup->sampler0, inputValue.uv, 0.0f));
        const float4 gradSampled = float4(bindGroup->colorTexture->sampleGrad(bindGroup->sampler0, inputValue.uv, dx, dy));
        const float4 gathered = float4(bindGroup->colorTexture->gatherRed(bindGroup->sampler0, inputValue.uv));
        const float4 loaded = float4(bindGroup->colorTexture->read(uint2(width & 1u, height & 1u), 0u));
        const float4 arraySampled = float4(bindGroup->arrayTexture->sampleLevel(bindGroup->sampler0, inputValue.uv, 0u, 0.0f));

        if (inputValue.uv.x < -1.0f)
        {
            discard_fragment();
        }
        UGL::clip(sampled.x - 0.25f);
        clip(float4(sampled.x - 0.1f, sampled.y - 0.1f, sampled.z, sampled.w));

        ExperimentalUGLIRIntrinsicCallKindsFragmentFrameBuffer frameBuffer;
        frameBuffer.color = half4(sampled + levelSampled + gradSampled + gathered + loaded + arraySampled);
        return frameBuffer;
    }
};

#endif
