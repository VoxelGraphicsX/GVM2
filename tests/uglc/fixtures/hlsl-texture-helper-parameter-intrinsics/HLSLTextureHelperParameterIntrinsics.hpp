#ifndef UGLC_TEST_HLSL_TEXTURE_HELPER_PARAMETER_INTRINSICS_HPP
#define UGLC_TEST_HLSL_TEXTURE_HELPER_PARAMETER_INTRINSICS_HPP

#include "UGL.h"

using namespace UGL;

/** Provides sampled texture handles, one sampler, and output storage for standalone texture-helper intrinsic coverage. */
struct HLSLTextureHelperParameterIntrinsicsBindGroup final : public IBindGroup
{
    /** Captures the 2D, 2D-array, 3D, sampler, and output resources used by the compute pass. */
    constructor(Texture2D<TextureFormat::RGBA16Float> texture2D [[Binding0]],
                Texture2DArray<TextureFormat::RGBA16Float> textureArray [[Binding1]],
                Texture3D<TextureFormat::RGBA16Float> texture3D [[Binding2]],
                Sampler sampler0 [[Binding3]],
                RWStructuredBuffer<float4> outputValues [[Binding4]])
    {
    }
};

/** Samples a standalone 2D texture helper parameter with the ordinary sample intrinsic. */
inline float4 sampleTexture2D(Texture2D<TextureFormat::RGBA16Float> texture, Sampler sampler, float2 uv)
{
    return float4(texture->sample(sampler, uv));
}

/** Samples a standalone 2D texture helper parameter with explicit gradients. */
inline float4 sampleGradTexture2D(Texture2D<TextureFormat::RGBA16Float> texture, Sampler sampler, float2 uv)
{
    return float4(texture->sampleGrad(sampler, uv, float2(0.01f, 0.0f), float2(0.0f, 0.01f)));
}

/** Gathers the red channel from a standalone 2D texture helper parameter. */
inline float4 gatherRedTexture2D(Texture2D<TextureFormat::RGBA16Float> texture, Sampler sampler, float2 uv)
{
    return float4(texture->gatherRed(sampler, uv));
}

/** Samples a standalone 2D-array texture helper parameter with an explicit array layer. */
inline float4 sampleTexture2DArray(Texture2DArray<TextureFormat::RGBA16Float> texture, Sampler sampler, float2 uv, uint layer)
{
    return float4(texture->sample(sampler, uv, layer));
}

/** Gathers the green channel from a standalone 2D-array texture helper parameter with an explicit array layer. */
inline float4 gatherGreenTexture2DArray(Texture2DArray<TextureFormat::RGBA16Float> texture, Sampler sampler, float2 uv, uint layer)
{
    return float4(texture->gatherGreen(sampler, uv, layer));
}

/** Samples a standalone 3D texture helper parameter with the ordinary sample intrinsic. */
inline float4 sampleTexture3D(Texture3D<TextureFormat::RGBA16Float> texture, Sampler sampler, float3 uvw)
{
    return float4(texture->sample(sampler, uvw));
}

/** Samples a standalone 3D texture helper parameter with explicit gradients. */
inline float4 sampleGradTexture3D(Texture3D<TextureFormat::RGBA16Float> texture, Sampler sampler, float3 uvw)
{
    return float4(texture->sampleGrad(sampler, uvw, float3(0.01f, 0.0f, 0.0f), float3(0.0f, 0.01f, 0.0f)));
}

/** Calls standalone sampled texture helper parameters from a compute shader entry. */
class [[LocalWorkGroupSize(1, 1, 1)]] HLSLTextureHelperParameterIntrinsicsPass final : public IComputeClass
{
public:
    /** Captures the bind group used by the compute shader. */
    constructor(BindGroup<HLSLTextureHelperParameterIntrinsicsBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes a combined value from every standalone texture-helper intrinsic covered by this fixture. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const float2 uv = float2(0.25f, 0.75f);
        const float3 uvw = float3(0.25f, 0.5f, 0.75f);
        const uint layer = 1u;
        float4 value = sampleTexture2D(bindGroup->texture2D, bindGroup->sampler0, uv);
        value += sampleGradTexture2D(bindGroup->texture2D, bindGroup->sampler0, uv);
        value += gatherRedTexture2D(bindGroup->texture2D, bindGroup->sampler0, uv);
        value += sampleTexture2DArray(bindGroup->textureArray, bindGroup->sampler0, uv, layer);
        value += gatherGreenTexture2DArray(bindGroup->textureArray, bindGroup->sampler0, uv, layer);
        value += sampleTexture3D(bindGroup->texture3D, bindGroup->sampler0, uvw);
        value += sampleGradTexture3D(bindGroup->texture3D, bindGroup->sampler0, uvw);
        bindGroup->outputValues[threadID.x] = value;
    }
};

#endif
