#ifndef UGLC_TEST_SHADER_RESOURCE_HANDLE_BEHAVIOR_FIELDS_HPP
#define UGLC_TEST_SHADER_RESOURCE_HANDLE_BEHAVIOR_FIELDS_HPP

#include "UGL.h"

using namespace UGL;

/** Provides direct shader resource handles used by the behavior-record field fixture. */
struct ShaderResourceHandleBehaviorFieldsBindGroup final : public IBindGroup
{
    /** Captures one sampled texture, one sampler, and one writable output buffer. */
    constructor(Texture2D<TextureFormat::RGBA16Float> sceneTexture [[Binding0]],
                Sampler sceneSampler [[Binding1]],
                RWStructuredBuffer<float4> outputValues [[Binding2]])
    {
    }
};

/** Stores direct shader resource handles and uses them through non-static behavior methods. */
struct DirectResourceHandleBehavior
{
    Texture2D<TextureFormat::RGBA16Float> texture;
    Sampler sampler0;
    RWStructuredBuffer<float4> outputValues;

    /** Stores the direct resource handles selected by the compute pass. */
    void init(Texture2D<TextureFormat::RGBA16Float> inputTexture,
              Sampler inputSampler,
              RWStructuredBuffer<float4> outputBuffer)
    {
        texture = inputTexture;
        sampler0 = inputSampler;
        outputValues = outputBuffer;
    }

    /** Samples the stored texture with the stored sampler and returns the sampled color. */
    float4 sampleColor(float2 uv)
    {
        return float4(texture->sampleLevel(sampler0, uv, 0.0f));
    }

    /** Writes a value through the stored writable buffer handle. */
    void writeValue(uint index, float4 value)
    {
        outputValues[index] = value;
    }
};

/** Verifies shader-only behavior records can store direct texture, sampler, and buffer handles. */
class [[LocalWorkGroupSize(1, 1, 1)]] ShaderResourceHandleBehaviorFieldsPass final : public IComputeClass
{
public:
    /** Captures the bind group used by the compute pass. */
    constructor(BindGroup<ShaderResourceHandleBehaviorFieldsBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Samples through a behavior object that stores direct resource handles and writes the result. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        DirectResourceHandleBehavior behavior;
        behavior.init(bindGroup->sceneTexture, bindGroup->sceneSampler, bindGroup->outputValues);
        behavior.writeValue(threadID.x, behavior.sampleColor(float2(0.25f, 0.75f)));
    }
};

#endif
