#ifndef UGLC_TEST_INVALID_HLSL_TEXTURE_HELPER_PARAMETER_OUT_HPP
#define UGLC_TEST_INVALID_HLSL_TEXTURE_HELPER_PARAMETER_OUT_HPP

#include "UGL.h"

using namespace UGL;

/** Provides sampled texture handles and output storage for invalid standalone helper-parameter diagnostics. */
struct InvalidHLSLTextureHelperParameterOutBindGroup final : public IBindGroup
{
    /** Captures the sampled texture, sampler, and output buffer used by the invalid compute pass. */
    constructor(Texture2D<TextureFormat::RGBA16Float> texture0 [[Binding0]],
                Sampler sampler0 [[Binding1]],
                RWStructuredBuffer<float4> outputValues [[Binding2]])
    {
    }
};

namespace InvalidHLSLTextureHelperParameterOutHelpers
{
    /** Intentionally declares a standalone sampled texture helper parameter as INOUT so HLSL rejects the handle ABI. */
    inline float4 sampleWithInOutTexture(Texture2D<TextureFormat::RGBA16Float> texture INOUT, Sampler sampler, float2 uv)
    {
        return float4(texture->sample(sampler, uv));
    }
} // namespace InvalidHLSLTextureHelperParameterOutHelpers

/** Calls the invalid helper so UGLC must validate the standalone sampled texture helper ABI. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidHLSLTextureHelperParameterOutPass final : public IComputeClass
{
public:
    /** Captures the bind group used by the invalid compute shader. */
    constructor(BindGroup<InvalidHLSLTextureHelperParameterOutBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Attempts to pass a texture handle to an INOUT helper parameter, which is not a valid HLSL ABI. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const float2 uv = float2(0.25f, 0.75f);
        bindGroup->outputValues[threadID.x] = InvalidHLSLTextureHelperParameterOutHelpers::sampleWithInOutTexture(bindGroup->texture0, bindGroup->sampler0, uv);
    }
};

#endif
