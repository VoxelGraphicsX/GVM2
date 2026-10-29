#ifndef UGLC_TEST_HLSL_TEXTURE_HELPER_PARAMETER_SAMPLE_LEVEL_HPP
#define UGLC_TEST_HLSL_TEXTURE_HELPER_PARAMETER_SAMPLE_LEVEL_HPP

#include "UGL.h"

using namespace UGL;

/** Provides sampled scene resources and writable output storage for standalone texture-helper parameter regression. */
struct HLSLTextureHelperParameterSampleLevelBindGroup final : public IBindGroup
{
    /** Captures the sampled texture, sampler, and output buffer consumed by the compute pass. */
    constructor(Texture2D<TextureFormat::RGBA16Float> sceneColor [[Binding0]],
                Sampler sceneSampler [[Binding1]],
                RWStructuredBuffer<float4> outputValues [[Binding2]])
    {
    }
};

/** Samples a standalone texture helper parameter through Texture2D::sampleLevel. */
inline float4 sampleColor(Texture2D<TextureFormat::RGBA16Float> texture, Sampler sampler, float2 uv)
{
    return float4(texture->sampleLevel(sampler, uv, 0.0f));
}

/** Keeps an unused but valid standalone texture helper in the fixture so current unused-helper handling is covered. */
inline float4 unusedSampleColor(Texture2D<TextureFormat::RGBA16Float> texture, Sampler sampler, float2 uv)
{
    const float2 clampedUv = clamp(uv, float2(0.0f), float2(1.0f));
    return float4(texture->sampleLevel(sampler, clampedUv, 1.0f));
}

/** Calls a standalone texture helper parameter from a compute shader entry. */
class [[LocalWorkGroupSize(1, 1, 1)]] HLSLTextureHelperParameterSampleLevelPass final : public IComputeClass
{
public:
    /** Captures the bind group used by the compute shader. */
    constructor(BindGroup<HLSLTextureHelperParameterSampleLevelBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes one sampled color through the standalone texture helper. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const float2 uv = float2(0.25f, 0.75f);
        bindGroup->outputValues[threadID.x] = sampleColor(bindGroup->sceneColor, bindGroup->sceneSampler, uv);
    }
};

#endif
