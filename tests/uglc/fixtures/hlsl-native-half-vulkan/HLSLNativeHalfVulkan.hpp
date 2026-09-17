#ifndef UGLC_TEST_HLSL_NATIVE_HALF_VULKAN_HPP
#define UGLC_TEST_HLSL_NATIVE_HALF_VULKAN_HPP

#include "UGL.h"

using namespace UGL;

struct HLSLNativeHalfVulkanBindGroup final : public IBindGroup
{
    constructor(Texture2D<half4> inputTexture [[Binding0]],
                Sampler textureSampler [[Binding1]],
                RWTexture2D<TextureFormat::RGBA16Float> outputTexture [[Binding2]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] HLSLNativeHalfVulkanPass final : public IComputeClass
{
public:
    constructor(BindGroup<HLSLNativeHalfVulkanBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const float2 uv = float2(0.25f, 0.75f);
        half4 sampled = half4(bindGroup->inputTexture->sampleLevel(bindGroup->textureSampler, uv, 0.0f));
        half4 scaled = sampled * half(0.5f) + half4(half(0.125f), half(0.25f), half(0.5f), half(0.0f));
        bindGroup->outputTexture->write(threadID.xy, scaled);
    }
};

#endif
